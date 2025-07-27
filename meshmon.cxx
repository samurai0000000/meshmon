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
#include "MeshMon.hxx"

#define DEFAULT_DEVICE "/dev/ttyAMA0"

static vector< shared_ptr<MeshMon>> mons;

static const struct option long_options[] = {
    { "device", required_argument, NULL, 'd', },
    { "verbose", no_argument, NULL, 'v', },
    { "log", no_argument, NULL, 'l', },
};

void sighandler(int signum)
{
    if (signum == SIGINT) {
        for (vector< shared_ptr<MeshMon>>::iterator it = mons.begin();
             it != mons.end(); it++) {
            (*it)->detach();
        }
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
    bool verbose = false;
    bool log = false;

    for (;;) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "d:vl",
                            long_options, &option_index);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'd':
            devices.push_back(string(optarg));
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

    for (vector<string>::const_iterator it = devices.cbegin();
         it != devices.cend(); it++) {
        shared_ptr<MeshMon> mon = make_shared<MeshMon>();
        if (mon->attachSerial(*it) == false) {
            cerr << "Unable to attch to " << *it << endl;
            continue;
        } else {
            mon->setVerbose(verbose);
            mon->enableLogStderr(log);
            mons.push_back(mon);
        }
    }

    signal(SIGINT, sighandler);

    for (vector< shared_ptr<MeshMon>>::iterator it = mons.begin();
         it != mons.end(); it++) {
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
