/*
 * meshmon.cxx
 *
 * Copyright (C) 2025, Charles Chiou
 */

#include <signal.h>
#include <getopt.h>
#include <mosquitto.h>
#include <iostream>
#include <vector>
#include <MeshShell.hxx>
#include "MeshMon.hxx"

#define DEFAULT_DEVICE "/dev/ttyAMA0"

static vector<shared_ptr<MeshMon>> mons;
static shared_ptr<MeshShell> stdioShell;
static vector<shared_ptr<MeshShell>> netShells;

static const struct option long_options[] = {
    { "device", required_argument, NULL, 'd', },
    { "stdio", no_argument, NULL, 's', },
    { "port", required_argument, NULL, 'p', },
    { "verbose", no_argument, NULL, 'v', },
    { "log", no_argument, NULL, 'l', },
};

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
    for (vector< shared_ptr<MeshShell>>::iterator it = netShells.begin();
         it != netShells.end(); it++) {
        (*it)->detach();
    }
}

void cleanup(void)
{
    mosquitto_lib_cleanup();
}

int main(int argc, char **argv)
{
    int ret = 0;
    vector<string> devices;
    bool useStdioShell = false;
    uint16_t port = 0;
    bool verbose = false;
    bool log = false;

    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "d:sp:vl",
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

    atexit(cleanup);
    signal(SIGINT, sighandler);

    for (vector<string>::const_iterator it = devices.cbegin();
         it != devices.cend(); it++) {
        shared_ptr<MeshMon> mon = make_shared<MeshMon>();

        if (mon->attachSerial(*it) == false) {
            cerr << "Unable to attch to " << *it << endl;
            continue;
        } else {
            shared_ptr<MeshShell> shell;

            mon->setClient(mon);
            mon->setVerbose(verbose);
            mon->enableLogStderr(log);
            mons.push_back(mon);

            if (useStdioShell && (stdioShell == NULL)) {
                stdioShell = make_shared<MeshShell>();
                stdioShell->setClient(mon);
                stdioShell->setNVM(mon);
            }

            if (port != 0) {
                shell = make_shared<MeshShell>();
                shell->setClient(mon);
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
    for (vector< shared_ptr<MeshShell>>::iterator it = netShells.begin();
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
