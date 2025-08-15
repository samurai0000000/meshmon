/*
 * meshmon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <mosquitto.h>
#include <libconfig.h++>
#include <iostream>
#include <vector>
#include <algorithm>
#include <MeshMonShell.hxx>
#include "MeshMon.hxx"
#include "version.h"

using namespace libconfig;

#define DEFAULT_DEVICE "/dev/ttyAMA0"

static vector<shared_ptr<MeshMon>> mons;
static shared_ptr<MeshMonShell> stdioShell;
static vector<shared_ptr<MeshMonShell>> netShells;

void sighandler(int signum)
{
    (void)(signum);

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

void cleanup(void)
{
    mosquitto_lib_cleanup();
}

static void loadLibConfig(Config &cfg, string &path)
{
    int fd;

    if (path.empty()) {
        string home;

        home = getenv("HOME");
        if (home.empty()) {
            return;
        }

        path = home + "/.meshmon";
    }

    // 'touch' to test the path validity
    fd = open(path.c_str(),
              O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK,
              0666);
    if (fd == -1) {
        goto done;
    } else {
        close(fd);
        fd = -1;
    }

    try {
        cfg.readFile(path.c_str());
    } catch (FileIOException &e) {
    } catch (ParseException &e) {
    }

done:

    return;
}

static const struct option long_options[] = {
    { "device", required_argument, NULL, 'd', },
    { "stdio", no_argument, NULL, 's', },
    { "port", required_argument, NULL, 'p', },
    { "daemon", no_argument, NULL, 'b', },
    { "verbose", no_argument, NULL, 'v', },
    { "log", no_argument, NULL, 'l', },
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
            string cfgDevice = cfgDevices[i];
            if (find(devices.begin(), devices.end(), cfgDevice) ==
                devices.end()) {
                devices.push_back(cfgDevice);
            }
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
        root.lookupValue("port", cfgPort);
        port = cfgPort;
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
            devices.push_back(string(optarg));
            break;
        case 's':
            useStdioShell = true;
            break;
        case 'p':
            port = atoi(optarg);
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
        } else if (pid  != 0) {
            exit(EXIT_SUCCESS);
        } else {
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            fdevnull = open("/dev/null", O_WRONLY);
            if (fdevnull != -1) {
                dup2(fdevnull, STDOUT_FILENO);
                dup2(fdevnull, STDERR_FILENO);
                close(fdevnull);
            }
        }
    }

    atexit(cleanup);
    signal(SIGINT, sighandler);
    signal(SIGPIPE, SIG_IGN);

    for (vector<string>::const_iterator it = devices.cbegin();
         it != devices.cend(); it++) {
        shared_ptr<MeshMon> mon = make_shared<MeshMon>();

        if (mon->attachSerial(*it) == false) {
            cerr << "Unable to attch to " << *it << endl;
            continue;
        } else {
            shared_ptr<MeshMonShell> shell;

            mon->setClient(mon);
            mon->setVerbose(verbose);
            mon->enableLogStderr(log);
            mons.push_back(mon);

            if (useStdioShell && (stdioShell == NULL)) {
                stdioShell = make_shared<MeshMonShell>();
                stdioShell->setBanner(banner);
                stdioShell->setVersion(version);
                stdioShell->setBuilt(built);
                stdioShell->setCopyright(copyright);
                stdioShell->setClient(mon);
                stdioShell->setNVM(mon);
            }

            if (port != 0) {
                shell = make_shared<MeshMonShell>();
                shell->setClient(mon);
                shell->setBanner(banner);
                shell->setVersion(version);
                shell->setBuilt(built);
                shell->setCopyright(copyright);
                shell->bindPort(port);
                shell->setNVM(mon);
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
