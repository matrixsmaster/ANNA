/* LISA - LSCS Inference System Application
 * (C) Dmitry 'MatrixS_Master' Solovyev, 2023-2025
 * */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <string>
#include "lscs.h"

#define LISA_VERSION "0.0.1"

#define ERR(X,...) fprintf(stderr, "[LISA] ERROR: " X "\n", __VA_ARGS__)
#define ERRS(...) fprintf(stderr, "[LISA] ERROR: " __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[LISA DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;

const char* argstrings[] = {
    "<-s LSCS scheme file> : load scheme",
    "[-t <time>] : inter-step delay value (in ms)",
    "[-m <file>] : use shutdown marker file",
    NULL
};

string g_lscs_file, g_shutmark;
int g_timeout = 100;

void usage(const char* sname)
{
    fprintf(stderr,"\nUsage: %s [OPTIONS]\n\nAvailable options:\n",sname);
    const char** p = argstrings;
    while (*p) fprintf(stderr,"\t%s\n",*p++);
    fprintf(stderr,"\n\n");
}

int set_params(int argc, char* argv[])
{
    int opt;

    // parse params
    while ((opt = getopt(argc,argv,"s:t:m:")) != -1) {
        switch (opt) {
        case 's':
            g_lscs_file = optarg;
            break;
        case 't':
            g_timeout = atoi(optarg);
            break;
        case 'm':
            g_shutmark = optarg;
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    // check'em
    if (g_lscs_file.empty()) return 1;

    return 0;
}

int main(int argc, char* argv[])
{
    fprintf(stderr,"LISA version " LISA_VERSION " starting up\n");

    // get CLI arguments
    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }
    if (set_params(argc,argv)) return -1;

    // create system
    AnnaLSCS* sys = new AnnaLSCS(g_lscs_file);
    if (!sys) return 10;
    if (sys->getState() == ANNA_ERROR) {
        ERR("Unable to create system: %s\n",sys->getError().c_str());
        return 11;
    }

    // run main loop
    while (sys->getState() != ANNA_ERROR) {
        if (!g_shutmark.empty()) {
            struct stat dummy;
            if (!stat(g_shutmark.c_str(),&dummy)) break;
        }

        sys->Processing();
        usleep(g_timeout * 1000);
    }

    // delete system
    delete sys;
    fprintf(stderr,"LISA version " LISA_VERSION " closing down\n");
    return 0;
}
