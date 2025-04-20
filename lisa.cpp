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

#define LIBFME_IMPLEMENTED_
#include "libfme.h"

#define LISA_VERSION "0.0.6"
#define LISA_MAX_FME_MESSAGE 256

#define ERR(X,...) fprintf(stderr, "[LISA] ERROR: " X "\n", __VA_ARGS__)
#define ERRS(...) fprintf(stderr, "[LISA] ERROR: " __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[LISA DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;

const char* argstrings[] = {
    "-s file : load LSCS scheme",
    "-t time : inter-step delay value (in ms)",
    "-m file : use shutdown marker file",
    "-F file : use FME control",
    NULL
};

bool g_quit = false, g_pause = false;
string g_lscs_file, g_shutmark, g_fme_socket;
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
    while ((opt = getopt(argc,argv,"s:t:m:F:")) != -1) {
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
        case 'F':
            g_fme_socket = optarg;
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    // check'em
    if (g_lscs_file.empty()) {
        ERR("LSCS scheme file was not specified (%d args given)",argc-1);
        return 1;
    }

    return 0;
}

void recv_fme_cmd()
{
    char msg[LISA_MAX_FME_MESSAGE] = {0};
    int r = fme_receive_msg(g_fme_socket.c_str(),msg,sizeof(msg));
    if (!r) return;

    if (!strncmp(msg,"quit",LISA_MAX_FME_MESSAGE-1)) {
        g_quit = true;

    } else if (!strncmp(msg,"set_period",LISA_MAX_FME_MESSAGE-1)) {
        int p = 0;
        if (sscanf(msg,"set_period %d",&p) == 1 && p > 0) g_timeout = p;

    } else if (!strncmp(msg,"pause",LISA_MAX_FME_MESSAGE-1)) {
        g_pause = true;

    } else if (!strncmp(msg,"unpause",LISA_MAX_FME_MESSAGE-1)) {
        g_pause = false;

    } else
        ERR("Unknown FME command received: '%s'",msg);
}

int main(int argc, char* argv[])
{
    fprintf(stderr,"LISA ver. " LISA_VERSION " (brain ver. " ANNA_VERSION ") starting up\n");

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
    while (!g_quit) {
        if (sys->getState() == ANNA_ERROR) {
            fprintf(stderr,"System state switched to Error, exiting...\n");
            break;
        }
        if (!g_shutmark.empty() && fme_check_msg(g_shutmark.c_str())) break;
        if (!g_fme_socket.empty() && fme_check_msg(g_fme_socket.c_str())) {
            recv_fme_cmd();
            continue; // skip a cycle to allow faster command execution and reaction to changes
        }

        if (!g_pause) sys->Processing();
        usleep(g_timeout * 1000);
    }

    // check for errors
    if (sys->getState() == ANNA_ERROR)
        ERR("%s\n",sys->getError().c_str());

    // delete system
    delete sys;
    fprintf(stderr,"LISA ver. " LISA_VERSION " closing down\n");
    return 0;
}
