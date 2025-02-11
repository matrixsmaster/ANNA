/* The File-based Message Exchange (FME) Header-Only Library
 * (C) Syntheva AS, 2024-2025. All rights reserved.
 * */

#ifndef LIBFME_INCLUDED_
#define LIBFME_INCLUDED_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <alloca.h>
#include <string.h>
#include <errno.h>

#define FME_TIMEOUT_UNIT_US 1000

enum fme_send_queue_e {
    FME_SEND_QUEUE_WAIT = 0,
    FME_SEND_QUEUE_OVERWRITE = -1,
    FME_SEND_QUEUE_APPEND = -2,
};

bool fme_init(const char* fname);
bool fme_check_msg(const char* fname);
size_t fme_check_msglen(const char* fname);
bool fme_waitfor_blocking(const char* fname, unsigned long timeout);

bool fme_send_msg(const char* fname, const void* msg, int len, int queue);
int fme_receive_msg(const char* fname, void* msg, int len);

#endif /* LIBFME_INCLUDED_ */

#if (!defined LIBFME_IMPLEMENTED_) || (defined LIBFME_IMPLEMENT_NOW)

#ifndef LIBFME_IMPLEMENTED_
    #define LIBFME_IMPLEMENTED_
#endif

#ifndef ERR
    #define ERR(...) do { fprintf(stderr,"[ERROR] " __VA_ARGS__); fputc('\n',stderr); fflush(stderr); } while (0)
    #define LIBFME_ERR_LOCAL_
#endif

#ifndef DBG
    #ifdef DEBUG_ENABLED
        #define DBG(...) do { fprintf(stderr,"[DEBUG] " __VA_ARGS__); fputc('\n',stderr); fflush(stderr); } while (0)
    #else
        #define DBG(...)
    #endif
    #define LIBFME_DBG_LOCAL_
#endif

bool fme_check_msg(const char* fname)
{
    if (!fname) return false;

    struct stat dummy;
    if (stat(fname,&dummy)) return false;

    return true;
}

size_t fme_check_msglen(const char* fname)
{
    if (!fname) return 0;

    struct stat srec;
    if (stat(fname,&srec)) return 0;

    return srec.st_size;
}

bool fme_waitfor_blocking(const char* fname, unsigned long timeout)
{
    for (unsigned long i = 0; i < timeout; i++) {
        if (fme_check_msg(fname)) return true;
        usleep(FME_TIMEOUT_UNIT_US);
    }
    return false;
}

bool fme_init(const char* fname)
{
    if (!fname) {
        ERR("fme_init() called with NULL socket name\n");
        return false;
    }

    if (fme_check_msg(fname)) {
        if (unlink(fname)) {
            ERR("Unable to init FME socket %s: %s\n",fname,strerror(errno));
            return false;
        }
    }
    return true;
}

/*
 * Sending message 'msg' to 'fname'.
 * 'queue' defines the waiting timeout for the previous message to be taken:
 *      FME_SEND_QUEUE_WAIT means waiting indefinitely
 *      FME_SEND_QUEUE_OVERWRITE means overwriting message file immediately
 *      FME_SEND_QUEUE_APPEND means trying to append to the exising message file
 *      > 0 means timeout in FME_TIMEOUT_UNIT_US units
 * */
bool fme_send_msg(const char* fname, const void* msg, int len, int queue)
{
    if (!fname || !msg || len < 1) return false;

    // Queuing support
    if (queue >= FME_SEND_QUEUE_WAIT) {
        while (1) {
            if (!fme_check_msg(fname)) break;
            if (queue && queue-- == 1) break;
            usleep(FME_TIMEOUT_UNIT_US);
        }
    }

    // Prepare temporary filename
    char* tmpname = (char*)alloca(strlen(fname)+3);
    strcpy(tmpname,fname);
    strcat(tmpname,".t");

    // If Append operation requested, yank the existing file first
    if (queue == FME_SEND_QUEUE_APPEND && fme_check_msg(fname)) {
        if (rename(fname,tmpname)) // allow safe failure to work around potential race condition
            queue = FME_SEND_QUEUE_OVERWRITE;
    }

    // Write down the message
    FILE* f = fopen(tmpname,((queue == FME_SEND_QUEUE_APPEND)? "ab":"wb"));
    if (!f || !fwrite(msg,len,1,f)) {
        if (f) fclose(f);
        unlink(tmpname);
        return false;
    }
    fclose(f);

    // Atomically replace the filename - the message is now ready
    return !rename(tmpname,fname);
}

int fme_receive_msg(const char* fname, void* msg, int len)
{
    if (!fname) return 0;

    // Prepare temporary filename
    char* tmpname = (char*)alloca(strlen(fname)+3);
    strcpy(tmpname,fname);
    strcat(tmpname,".r");

    // Atomically get the message file by renaming it -
    // the original name could now be used to send another message
    if (rename(fname,tmpname)) return 0;

    // Read the message contents
    int r = 0;
    if (msg && len > 0) {
        FILE* f = fopen(tmpname,"rb");
        if (!f) {
            unlink(tmpname);
            return 0;
        }
        r = fread(msg,1,len,f);
        fclose(f);
    }

    // Delete the used-up file
    unlink(tmpname);
    return r;
}

#ifdef LIBFME_ERR_LOCAL_
    #undef LIBFME_ERR_LOCAL_
    #undef ERR
#endif

#ifdef LIBFME_DBG_LOCAL_
    #undef LIBFME_DBG_LOCAL_
    #undef DBG
#endif

#endif /* LIBFME_IMPLEMENTED_ */
