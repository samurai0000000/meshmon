/*
 * meshmon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <pwd.h>
#include <mosquitto.h>
#include <curl/curl.h>
#include <libconfig.h++>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <cctype>
#include <MeshMonShell.hxx>
#include "MeshMon.hxx"
#include "GeminiChat.hxx"
#include "Calibration.hxx"
#include "version.h"

using namespace libconfig;

#define DEFAULT_DEVICE "/dev/ttyAMA0"

static vector<shared_ptr<MeshMon>> mons;
static shared_ptr<MeshMonShell> stdioShell;
static vector<shared_ptr<MeshMonShell>> netShells;
static volatile sig_atomic_t g_stop = 0;
static int g_stop_pipe[2] = { -1, -1 };

void sighandler(int signum)
{
    char c = 1;

    (void)(signum);
    g_stop = 1;
    if (g_stop_pipe[1] != -1) {
        (void) write(g_stop_pipe[1], &c, 1);
    }
}

static void requestStop(void)
{
    for (vector< shared_ptr<MeshMon>>::iterator it = mons.begin();
         it != mons.end(); it++) {
        (*it)->detach();
    }
    if (stdioShell) {
        stdioShell->detach();
    }
    for (vector< shared_ptr<MeshMonShell>>::iterator it = netShells.begin();
         it != netShells.end(); it++) {
        (*it)->detach();
    }
}

static void stopWatcher(void)
{
    char c;

    while (!g_stop) {
        ssize_t n = read(g_stop_pipe[0], &c, 1);
        if (n > 0) {
            break;
        }
        if ((n < 0) && (errno == EINTR)) {
            continue;
        }
        break;
    }
    requestStop();
}

static bool parsePort(const char *s, uint16_t &port)
{
    char *end = NULL;
    unsigned long v;

    if ((s == NULL) || (s[0] == '\0')) {
        return false;
    }

    errno = 0;
    v = strtoul(s, &end, 10);
    if ((errno != 0) || (end == s) || (*end != '\0') || (v > 65535)) {
        return false;
    }

    port = (uint16_t) v;
    return true;
}

static void addDevice(vector<string> &devices, const string &device)
{
    if (find(devices.begin(), devices.end(), device) == devices.end()) {
        devices.push_back(device);
    }
}

static void releaseMeshMons(void)
{
    stdioShell.reset();
    netShells.clear();
    for (vector< shared_ptr<MeshMon>>::iterator it = mons.begin();
         it != mons.end(); it++) {
        (*it)->setClient(NULL);
        (*it)->setNvm(NULL);
        (*it)->setCalibration(NULL);
    }
    mons.clear();
}

static int g_lockFd = -1;
static string g_lockPath;

static string getLockFilePath(void)
{
    const char *homedir = getenv("HOME");
    if ((homedir != NULL) && (homedir[0] != '\0')) {
        return string(homedir) + "/.meshmon.lock";
    }

    struct passwd *pw = getpwuid(getuid());
    if ((pw != NULL) && (pw->pw_dir != NULL) && (pw->pw_dir[0] != '\0')) {
        return string(pw->pw_dir) + "/.meshmon.lock";
    }

    return "/tmp/.meshmon.lock";
}

static bool acquirePidLock(void)
{
    g_lockPath = getLockFilePath();

    g_lockFd = open(g_lockPath.c_str(), O_RDWR | O_CREAT, 0644);
    if (g_lockFd == -1) {
        cerr << g_lockPath << ": " << strerror(errno) << endl;
        return false;
    }

    fcntl(g_lockFd, F_SETFD, FD_CLOEXEC);

    if (flock(g_lockFd, LOCK_EX | LOCK_NB) == -1) {
        if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
            char buf[64];
            memset(buf, 0, sizeof(buf));
            ssize_t n = read(g_lockFd, buf, sizeof(buf) - 1);
            if (n > 0) {
                while ((n > 0) &&
                       ((buf[n - 1] == '\n') || (buf[n - 1] == '\r') ||
                        (buf[n - 1] == ' '))) {
                    buf[--n] = '\0';
                }
                cerr << "Another instance of meshmon is already running (PID "
                     << buf << ")" << endl;
            } else {
                cerr << "Another instance of meshmon is already running"
                     << endl;
            }
        } else {
            cerr << "Failed to lock " << g_lockPath << ": "
                 << strerror(errno) << endl;
        }
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }

    if ((ftruncate(g_lockFd, 0) == -1) ||
        (lseek(g_lockFd, 0, SEEK_SET) == -1)) {
        cerr << "Failed to truncate " << g_lockPath << ": "
             << strerror(errno) << endl;
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }

    string pidStr = to_string((long) getpid()) + "\n";
    if (write(g_lockFd, pidStr.c_str(), pidStr.size()) == -1) {
        cerr << "Failed to write PID to " << g_lockPath << ": "
             << strerror(errno) << endl;
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }
    fsync(g_lockFd);

    return true;
}

static void updatePidLock(void)
{
    if (g_lockFd != -1) {
        if ((ftruncate(g_lockFd, 0) != -1) &&
            (lseek(g_lockFd, 0, SEEK_SET) != -1)) {
            string pidStr = to_string((long) getpid()) + "\n";
            ssize_t n = write(g_lockFd, pidStr.c_str(), pidStr.size());
            (void) n;
            fsync(g_lockFd);
        }
    }
}

static void releasePidLock(void)
{
    if (g_lockFd != -1) {
        if (!g_lockPath.empty()) {
            unlink(g_lockPath.c_str());
        }
        flock(g_lockFd, LOCK_UN);
        close(g_lockFd);
        g_lockFd = -1;
    }
}

void cleanup(void)
{
    releasePidLock();
    mosquitto_lib_cleanup();
    curl_global_cleanup();
}

static void loadLibConfig(Config &cfg, string &path)
{
    int fd;

    if (path.empty()) {
        const char *homedir;

        homedir = getenv("HOME");
        if ((homedir == NULL) || (homedir[0] == '\0')) {
            return;
        }

        path = string(homedir) + "/.meshmon";
    }

    // 'touch' to test the path validity
    fd = open(path.c_str(),
              O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK,
              S_IRUSR | S_IWUSR);
    if (fd == -1) {
        cerr << path << ": " << strerror(errno) << endl;
        return;
    }
    if (fchmod(fd, S_IRUSR | S_IWUSR) == -1) {
        cerr << path << ": " << strerror(errno) << endl;
        close(fd);
        return;
    }
    close(fd);

    {
        struct stat st;

        if ((stat(path.c_str(), &st) == 0) && (st.st_size == 0)) {
            return;
        }
    }

    try {
        cfg.readFile(path.c_str());
    } catch (const FileIOException &) {
        cerr << "Unable to read config " << path << endl;
        exit(EXIT_FAILURE);
    } catch (ParseException &e) {
        cerr << "Parse error in " << e.getFile()
             << " line " << e.getLine() << ": " << e.getError() << endl;
        exit(EXIT_FAILURE);
    }
}

static void mqttCfgFail(const string &path, const string &msg)
{
    cerr << (path.empty() ? string("~/.meshmon") : path) << ": " << msg << endl;
    exit(EXIT_FAILURE);
}

static bool readOwnMqttConfig(Config &cfg, const string &cfgfile,
                              string &server, uint16_t &port,
                              string &user, string &password,
                              string &topic, bool &tls)
{
    Setting &root = cfg.getRoot();
    int cfgPort = 0;

    if (!root.exists("mqtt")) {
        return false;
    }

    try {
        Setting &mqtt = root["mqtt"];

        if (!mqtt.exists("server") || !mqtt.lookupValue("server", server)) {
            mqttCfgFail(cfgfile, "mqtt.server missing or not a string");
        }
        if (server.empty()) {
            mqttCfgFail(cfgfile, "mqtt.server is empty");
        }
        if (!mqtt.exists("port") || !mqtt.lookupValue("port", cfgPort)) {
            mqttCfgFail(cfgfile, "mqtt.port missing or not an integer");
        }
        if ((cfgPort < 1) || (cfgPort > 65535)) {
            mqttCfgFail(cfgfile, "mqtt.port out of range");
        }
        if (!mqtt.exists("user") || !mqtt.lookupValue("user", user)) {
            mqttCfgFail(cfgfile, "mqtt.user missing or not a string");
        }
        if (!mqtt.exists("password") || !mqtt.lookupValue("password", password)) {
            mqttCfgFail(cfgfile, "mqtt.password missing or not a string");
        }
        if (!mqtt.exists("topic") || !mqtt.lookupValue("topic", topic)) {
            mqttCfgFail(cfgfile, "mqtt.topic missing or not a string");
        }
        if (topic.empty()) {
            mqttCfgFail(cfgfile, "mqtt.topic is empty");
        }
        if (!mqtt.exists("tls") || !mqtt.lookupValue("tls", tls)) {
            mqttCfgFail(cfgfile, "mqtt.tls missing or not a boolean");
        }
    } catch (const SettingTypeException &) {
        mqttCfgFail(cfgfile, "mqtt is not a group");
    }

    port = (uint16_t) cfgPort;
    return true;
}

static void geminiCfgFail(const string &path, const string &msg)
{
    cerr << (path.empty() ? string("~/.meshmon") : path) << ": " << msg << endl;
    exit(EXIT_FAILURE);
}

static bool validGeminiModel(const string &model)
{
    size_t i;

    if (model.empty()) {
        return false;
    }

    for (i = 0; i < model.size(); i++) {
        unsigned char c = static_cast<unsigned char>(model[i]);

        if (!isalnum(c) && (c != '-') && (c != '_') && (c != '.')) {
            return false;
        }
    }

    return true;
}

static bool readGeminiConfig(Config &cfg, const string &cfgfile,
                             string &apiKey, string &model,
                             uint32_t &timeout, bool &search,
                             uint32_t &maxOutputTokens,
                             int32_t &thinkingBudget,
                             float &temperature,
                             float &topP,
                             int32_t &topK,
                             string &systemInstruction,
                             size_t &maxHistory,
                             int32_t &maxContextTurns,
                             uint32_t &idleTimeout,
                             bool &enabled)
{
    Setting &root = cfg.getRoot();

    if (!root.exists("gemini")) {
        return false;
    }

    try {
        Setting &gemini = root["gemini"];

        enabled = true;
        if (gemini.exists("enabled")) {
            if (!gemini.lookupValue("enabled", enabled)) {
                geminiCfgFail(cfgfile, "gemini.enabled is not a boolean");
            }
        }

        if (!gemini.exists("api_key") ||
            !gemini.lookupValue("api_key", apiKey)) {
            geminiCfgFail(cfgfile, "gemini.api_key missing or not a string");
        }
        if (apiKey.empty()) {
            geminiCfgFail(cfgfile, "gemini.api_key is empty");
        }

        model = "gemini-3.5-flash";
        if (gemini.exists("model")) {
            if (!gemini.lookupValue("model", model)) {
                geminiCfgFail(cfgfile, "gemini.model is not a string");
            }
            if (!validGeminiModel(model)) {
                geminiCfgFail(cfgfile, "gemini.model is empty or invalid");
            }
        }

        timeout = 30;
        int timeoutInt = 30;
        if (gemini.exists("timeout")) {
            if (!gemini.lookupValue("timeout", timeoutInt)) {
                geminiCfgFail(cfgfile, "gemini.timeout is not an integer");
            }
        } else if (gemini.exists("timeout_s")) {
            if (!gemini.lookupValue("timeout_s", timeoutInt)) {
                geminiCfgFail(cfgfile, "gemini.timeout_s is not an integer");
            }
        }
        if ((timeoutInt <= 0) || (timeoutInt > 300)) {
            geminiCfgFail(cfgfile, "gemini.timeout must be between 1 and 300 seconds");
        }
        timeout = (uint32_t) timeoutInt;

        search = true;
        if (gemini.exists("search")) {
            if (!gemini.lookupValue("search", search)) {
                geminiCfgFail(cfgfile, "gemini.search is not a boolean");
            }
        } else if (gemini.exists("web_search")) {
            if (!gemini.lookupValue("web_search", search)) {
                geminiCfgFail(cfgfile, "gemini.web_search is not a boolean");
            }
        } else if (gemini.exists("google_search")) {
            if (!gemini.lookupValue("google_search", search)) {
                geminiCfgFail(cfgfile, "gemini.google_search is not a boolean");
            }
        }

        maxOutputTokens = 512;
        int maxTokensInt = 512;
        if (gemini.exists("max_output_tokens")) {
            if (!gemini.lookupValue("max_output_tokens", maxTokensInt)) {
                geminiCfgFail(cfgfile, "gemini.max_output_tokens is not an integer");
            }
        } else if (gemini.exists("max_tokens")) {
            if (!gemini.lookupValue("max_tokens", maxTokensInt)) {
                geminiCfgFail(cfgfile, "gemini.max_tokens is not an integer");
            }
        }
        if ((maxTokensInt < 16) || (maxTokensInt > 8192)) {
            geminiCfgFail(cfgfile, "gemini.max_output_tokens must be between 16 and 8192");
        }
        maxOutputTokens = (uint32_t) maxTokensInt;

        thinkingBudget = 0;
        if (gemini.exists("thinking_budget")) {
            int tbInt = 0;
            if (gemini.lookupValue("thinking_budget", tbInt)) {
                if (tbInt < -1 || tbInt > 65536) {
                    geminiCfgFail(cfgfile, "gemini.thinking_budget must be >= -1");
                }
                thinkingBudget = tbInt;
            } else {
                bool tbBool = false;
                if (gemini.lookupValue("thinking_budget", tbBool)) {
                    thinkingBudget = tbBool ? -1 : 0;
                } else {
                    geminiCfgFail(cfgfile, "gemini.thinking_budget is invalid");
                }
            }
        } else if (gemini.exists("thinking")) {
            bool tbBool = false;
            if (gemini.lookupValue("thinking", tbBool)) {
                thinkingBudget = tbBool ? -1 : 0;
            } else {
                int tbInt = 0;
                if (gemini.lookupValue("thinking", tbInt)) {
                    thinkingBudget = tbInt;
                } else {
                    geminiCfgFail(cfgfile, "gemini.thinking is invalid");
                }
            }
        }

        temperature = -1.0f;
        if (gemini.exists("temperature")) {
            double tempDbl = 0.0;
            if (gemini.lookupValue("temperature", tempDbl)) {
                if (tempDbl < 0.0 || tempDbl > 2.0) {
                    geminiCfgFail(cfgfile, "gemini.temperature must be between 0.0 and 2.0");
                }
                temperature = static_cast<float>(tempDbl);
            } else {
                int tempInt = 0;
                if (gemini.lookupValue("temperature", tempInt)) {
                    temperature = static_cast<float>(tempInt);
                } else {
                    geminiCfgFail(cfgfile, "gemini.temperature is invalid");
                }
            }
        } else if (gemini.exists("temp")) {
            double tempDbl = 0.0;
            if (gemini.lookupValue("temp", tempDbl)) {
                temperature = static_cast<float>(tempDbl);
            }
        }

        topP = -1.0f;
        if (gemini.exists("top_p")) {
            double topPDbl = 0.0;
            if (gemini.lookupValue("top_p", topPDbl)) {
                if (topPDbl < 0.0 || topPDbl > 1.0) {
                    geminiCfgFail(cfgfile, "gemini.top_p must be between 0.0 and 1.0");
                }
                topP = static_cast<float>(topPDbl);
            } else {
                geminiCfgFail(cfgfile, "gemini.top_p is invalid");
            }
        }

        topK = -1;
        if (gemini.exists("top_k")) {
            int topKInt = 0;
            if (gemini.lookupValue("top_k", topKInt)) {
                if (topKInt < 1 || topKInt > 100) {
                    geminiCfgFail(cfgfile, "gemini.top_k must be between 1 and 100");
                }
                topK = topKInt;
            } else {
                geminiCfgFail(cfgfile, "gemini.top_k is not an integer");
            }
        }

        systemInstruction.clear();
        if (gemini.exists("system_instruction")) {
            gemini.lookupValue("system_instruction", systemInstruction);
        } else if (gemini.exists("instruction")) {
            gemini.lookupValue("instruction", systemInstruction);
        } else if (gemini.exists("prompt")) {
            gemini.lookupValue("prompt", systemInstruction);
        }

        maxHistory = 10;
        int maxHistInt = 10;
        if (gemini.exists("max_history")) {
            if (gemini.lookupValue("max_history", maxHistInt) && maxHistInt >= 1 && maxHistInt <= 100) {
                maxHistory = static_cast<size_t>(maxHistInt);
            }
        } else if (gemini.exists("history_max")) {
            if (gemini.lookupValue("history_max", maxHistInt) && maxHistInt >= 1 && maxHistInt <= 100) {
                maxHistory = static_cast<size_t>(maxHistInt);
            }
        }

        maxContextTurns = 6;
        int maxContextInt = 6;
        if (gemini.exists("max_context_turns")) {
            if (gemini.lookupValue("max_context_turns", maxContextInt) && maxContextInt >= 1 && maxContextInt <= 100) {
                maxContextTurns = maxContextInt;
            }
        } else if (gemini.exists("max_context")) {
            if (gemini.lookupValue("max_context", maxContextInt) && maxContextInt >= 1 && maxContextInt <= 100) {
                maxContextTurns = maxContextInt;
            }
        } else if (gemini.exists("context_turns")) {
            if (gemini.lookupValue("context_turns", maxContextInt) && maxContextInt >= 1 && maxContextInt <= 100) {
                maxContextTurns = maxContextInt;
            }
        }

        idleTimeout = 300;
        int idleInt = 300;
        if (gemini.exists("idle_timeout")) {
            if (gemini.lookupValue("idle_timeout", idleInt) && idleInt >= 10 && idleInt <= 86400) {
                idleTimeout = static_cast<uint32_t>(idleInt);
            }
        } else if (gemini.exists("idle_timeout_s")) {
            if (gemini.lookupValue("idle_timeout_s", idleInt) && idleInt >= 10 && idleInt <= 86400) {
                idleTimeout = static_cast<uint32_t>(idleInt);
            }
        }
    } catch (const SettingTypeException &) {
        geminiCfgFail(cfgfile, "gemini is not a group");
    }

    return true;
}

static const struct option long_options[] = {
    { "device", required_argument, NULL, 'd', },
    { "stdio", no_argument, NULL, 's', },
    { "port", required_argument, NULL, 'p', },
    { "daemon", no_argument, NULL, 'b', },
    { "verbose", no_argument, NULL, 'v', },
    { "log", no_argument, NULL, 'l', },
    { 0, 0, 0, 0 },
};

int main(int argc, char **argv)
{
    int ret = 0;
    Config cfg;
    string cfgfile;
    vector<string> devices;
    bool useStdioShell = false;
    uint16_t port = 0;
    bool daemon = false;
    bool verbose = false;
    bool log = false;
    string banner;
    string version;
    string built;
    string copyright;

    banner = "The MeshMon Application";
    version = string("Version: ") + string(MYPROJECT_VERSION_STRING);
    built = string("Built: ") + string(MYPROJECT_WHOAMI) + string("@") +
        string(MYPROJECT_HOSTNAME) + string(" ") + string(MYPROJECT_DATE);
    copyright = string("Copyright (C) 2025, Charles Chiou");

    loadLibConfig(cfg, cfgfile);

    shared_ptr<Calibration> calibration = make_shared<Calibration>();
    calibration->loadFile();

    try {
        Setting &root = cfg.getRoot();
        Setting &cfgDevices = root["devices"];
        for (int i = 0; i < cfgDevices.getLength(); i++) {
            addDevice(devices, cfgDevices[i]);
        }
    } catch (SettingNotFoundException &e) {
    } catch (SettingTypeException &e) {
    }

    try {
        int cfgStdioShell = 0;
        Setting &root = cfg.getRoot();
        root.lookupValue("stdioShell", cfgStdioShell);
        useStdioShell = cfgStdioShell != 0 ? true : false;
    } catch (SettingNotFoundException &e) {
    } catch (SettingTypeException &e) {
    }

    try {
        int cfgDeviceLog = 0;
        Setting &root = cfg.getRoot();
        root.lookupValue("deviceLog", cfgDeviceLog);
        log = cfgDeviceLog != 0 ? true : false;
    } catch (SettingNotFoundException &e) {
    } catch (SettingTypeException &e) {
    }

    try {
        int cfgPort = 0;
        Setting &root = cfg.getRoot();
        if (root.lookupValue("port", cfgPort)) {
            if ((cfgPort < 0) || (cfgPort > 65535)) {
                cerr << "Invalid config port: " << cfgPort << endl;
                exit(EXIT_FAILURE);
            }
            port = (uint16_t) cfgPort;
        }
    } catch (SettingNotFoundException &e) {
    } catch (SettingTypeException &e) {
    }

    try {
        bool cfgDaemon = 0;
        Setting &root = cfg.getRoot();
        root.lookupValue("daemon", cfgDaemon);
        daemon = cfgDaemon;
    } catch (SettingNotFoundException &e) {
    } catch (SettingTypeException &e) {
    }

    bool haveOwnMqtt = false;
    string mqttServer;
    string mqttUser;
    string mqttPassword;
    string mqttTopic;
    uint16_t mqttPort = 0;
    bool mqttTls = false;

    haveOwnMqtt = readOwnMqttConfig(cfg, cfgfile, mqttServer, mqttPort,
                                    mqttUser, mqttPassword, mqttTopic,
                                    mqttTls);

    bool haveGemini = false;
    string geminiApiKey;
    string geminiModel;
    uint32_t geminiTimeout = 30;
    bool geminiSearch = true;
    uint32_t geminiMaxOutputTokens = 512;
    int32_t geminiThinkingBudget = 0;
    float geminiTemperature = -1.0f;
    float geminiTopP = -1.0f;
    int32_t geminiTopK = -1;
    string geminiSystemInstruction;
    size_t geminiMaxHistory = 10;
    int32_t geminiMaxContext = 6;
    uint32_t geminiIdleTimeout = 300;
    bool geminiEnabled = true;

    haveGemini = readGeminiConfig(cfg, cfgfile, geminiApiKey, geminiModel,
                                  geminiTimeout, geminiSearch,
                                  geminiMaxOutputTokens,
                                  geminiThinkingBudget,
                                  geminiTemperature,
                                  geminiTopP,
                                  geminiTopK,
                                  geminiSystemInstruction,
                                  geminiMaxHistory,
                                  geminiMaxContext,
                                  geminiIdleTimeout,
                                  geminiEnabled);

    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "d:sp:bvl",
                            long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'd':
            addDevice(devices, string(optarg));
            break;
        case 's':
            useStdioShell = true;
            break;
        case 'p':
            if (!parsePort(optarg, port)) {
                cerr << "Invalid port: " << optarg << endl;
                exit(EXIT_FAILURE);
            }
            break;
        case 'b':
            daemon = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'l':
            log = true;
            break;
        default:
            fprintf(stderr, "Unrecognized argument specified!\n");
            exit(EXIT_FAILURE);
            break;
        }
    }

    if (devices.empty()) {
        devices.push_back(string(DEFAULT_DEVICE));
    }

    if (!acquirePidLock()) {
        exit(EXIT_FAILURE);
    }

    ret = mosquitto_lib_init();
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mosquitto_lib_init failed (%d)!\n", ret);
        releasePidLock();
        exit(EXIT_FAILURE);
    }

    ret = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (ret != CURLE_OK) {
        fprintf(stderr, "curl_global_init failed (%d)!\n", ret);
        releasePidLock();
        exit(EXIT_FAILURE);
    }

    if (daemon) {
        pid_t pid;
        int fdevnull;

        useStdioShell = false;
        verbose = false;
        if (port == 0) {
            port = 16876;
        }

        pid = fork();
        if (pid == -1) {
            cerr << "fork failed!" << endl;
            releasePidLock();
            exit(EXIT_FAILURE);
        } else if (pid != 0) {
            exit(EXIT_SUCCESS);
        }

        updatePidLock();

        if (setsid() == -1) {
            releasePidLock();
            exit(EXIT_FAILURE);
        }

        fdevnull = open("/dev/null", O_RDWR);
        if (fdevnull != -1) {
            dup2(fdevnull, STDIN_FILENO);
            dup2(fdevnull, STDOUT_FILENO);
            dup2(fdevnull, STDERR_FILENO);
            if (fdevnull > STDERR_FILENO) {
                close(fdevnull);
            }
        }
    }

    atexit(cleanup);

    if (pipe(g_stop_pipe) == -1) {
        cerr << "pipe failed: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    fcntl(g_stop_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(g_stop_pipe[1], F_SETFD, FD_CLOEXEC);
    {
        int flags = fcntl(g_stop_pipe[1], F_GETFL, 0);
        if (flags != -1) {
            fcntl(g_stop_pipe[1], F_SETFL, flags | O_NONBLOCK);
        }
    }

    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    thread stopThread(stopWatcher);

    for (vector<string>::const_iterator it = devices.cbegin();
         it != devices.cend(); it++) {
        shared_ptr<MeshMon> mon;

        if (g_stop) {
            break;
        }

        mon = make_shared<MeshMon>();
        mon->setBanner(banner);
        mon->setVersion(version);
        mon->setBuilt(built);
        mon->setCopyright(copyright);

        if (mon->attachSerial(*it) == false) {
            cerr << "Unable to attach to " << *it << endl;
            continue;
        }

        if (g_stop) {
            mon->detach();
            mon->join();
            break;
        }

        mon->setClient(mon);
        mon->setNvm(mon);
        mon->setVerbose(verbose);
        mon->enableLogStderr(log);
        if (haveOwnMqtt) {
            mon->setOwnMqtt(mqttServer, mqttPort, mqttUser, mqttPassword,
                            mqttTopic, mqttTls);
        }
        if (haveGemini) {
            shared_ptr<GeminiChat> bot = make_shared<GeminiChat>(geminiApiKey,
                                                                 geminiModel,
                                                                 geminiSearch,
                                                                 geminiTimeout,
                                                                 geminiMaxOutputTokens,
                                                                 geminiThinkingBudget);
            bot->setEnabled(geminiEnabled);
            if (geminiTemperature >= 0.0f) {
                bot->setTemperature(geminiTemperature);
            }
            if (geminiTopP >= 0.0f) {
                bot->setTopP(geminiTopP);
            }
            if (geminiTopK >= 0) {
                bot->setTopK(geminiTopK);
            }
            if (!geminiSystemInstruction.empty()) {
                bot->setCustomInstruction(geminiSystemInstruction);
            }
            bot->setMaxHistoryTurns(geminiMaxHistory);
            bot->setMaxContextTurns(geminiMaxContext);
            bot->setIdleTimeout(geminiIdleTimeout);
            bot->loadTasksFromFile();
            mon->setChatBot(bot);
        }
        mon->setCalibration(calibration);
        mons.push_back(mon);

        if (useStdioShell && (stdioShell == NULL)) {
            stdioShell = make_shared<MeshMonShell>();
            stdioShell->setClient(mon);
            stdioShell->setNvm(mon);
        }

        if (port != 0) {
            shared_ptr<MeshMonShell> shell = make_shared<MeshMonShell>();

            shell->setClient(mon);
            shell->setNvm(mon);
            if (shell->bindPort(port)) {
                netShells.push_back(shell);
            }
            if (port < 65535) {
                port++;
            } else {
                port = 0;
            }
        }
    }

    if (g_stop) {
        requestStop();
    }

    if (stdioShell && !g_stop) {
        // Attach last to let net shells print to stdout before we output
        // the prompt on stdio
        stdioShell->attachStdio();
    }

    if (mons.empty()) {
        cerr << "No devices attached" << endl;
        ret = EXIT_FAILURE;
    } else {
        for (vector< shared_ptr<MeshMon>>::iterator it = mons.begin();
             it != mons.end(); it++) {
            (*it)->join();
        }
        if (stdioShell) {
            stdioShell->join();
        }
        for (vector< shared_ptr<MeshMonShell>>::iterator it = netShells.begin();
             it != netShells.end(); it++) {
            (*it)->join();
        }
        cout << "Good-bye!" << endl;
    }

    g_stop = 1;
    if (g_stop_pipe[1] != -1) {
        char c = 1;
        (void) write(g_stop_pipe[1], &c, 1);
    }
    if (stopThread.joinable()) {
        stopThread.join();
    }
    if (g_stop_pipe[0] != -1) {
        close(g_stop_pipe[0]);
        g_stop_pipe[0] = -1;
    }
    if (g_stop_pipe[1] != -1) {
        close(g_stop_pipe[1]);
        g_stop_pipe[1] = -1;
    }

    releaseMeshMons();

    return ret;
}

/*
 * Local variables:
 * mode: C++
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
