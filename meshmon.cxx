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
#include <mosquitto.h>
#include <libconfig.h++>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <MeshMonShell.hxx>
#include "MeshMon.hxx"
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
    }
    mons.clear();
}

void cleanup(void)
{
    mosquitto_lib_cleanup();
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
              0666);
    if (fd == -1) {
        cerr << path << ": " << strerror(errno) << endl;
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

    ret = mosquitto_lib_init();
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "mosquitto_lib_init failed (%d)!\n", ret);
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
            exit(EXIT_FAILURE);
        } else if (pid != 0) {
            exit(EXIT_SUCCESS);
        }

        if (setsid() == -1) {
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
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    for (vector<string>::const_iterator it = devices.cbegin();
         it != devices.cend(); it++) {
        shared_ptr<MeshMon> mon = make_shared<MeshMon>();
        mon->setBanner(banner);
        mon->setVersion(version);
        mon->setBuilt(built);
        mon->setCopyright(copyright);

        if (mon->attachSerial(*it) == false) {
            cerr << "Unable to attach to " << *it << endl;
            continue;
        } else {
            shared_ptr<MeshMonShell> shell;

            mon->setClient(mon);
            mon->setNvm(mon);
            mon->setVerbose(verbose);
            mon->enableLogStderr(log);
            mons.push_back(mon);

            if (useStdioShell && (stdioShell == NULL)) {
                stdioShell = make_shared<MeshMonShell>();
                stdioShell->setClient(mon);
                stdioShell->setNvm(mon);
            }

            if (port != 0) {
                shell = make_shared<MeshMonShell>();
                shell->setClient(mon);
                shell->bindPort(port);
                shell->setNvm(mon);
                netShells.push_back(shell);
                port++;
            }
        }
    }

    if (stdioShell) {
        // Attach last to let net shells print to stdout before we output
        // the prompt on stdio
        stdioShell->attachStdio();
    }

    /* ------- */

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

    thread stopThread(stopWatcher);

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

    cout << "Good-bye!" << endl;

    return 0;
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
