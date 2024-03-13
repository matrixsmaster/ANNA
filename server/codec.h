#pragma once

// TODO: this file is intended for implementing encryption/decryption services for Anna server and client sides
// for now it contains only basic functions and placeholder code, as it's yet to be decided which encryption library
// we will use (openssl, libressl, etc)

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <random>

#define ANNA_CLIENT_MASK 0x3FFFFFFFUL

#define ERR(...) do { fprintf(stderr,"[CODEC] ERROR: " __VA_ARGS__); fflush(stderr); } while (0)

static uint32_t make_clid()
{
    auto rng = std::mt19937(time(NULL));
    uint32_t clid = (uint32_t)rng() & ANNA_CLIENT_MASK;
    return clid;
}

static void reclaim_clid(uint32_t /*clid*/)
{
    // TODO
}

static void codec_infill_str(char* str, size_t len)
{
    if (!str || !len) return;
    size_t l = strlen(str);
    for (size_t i = l + 1; i < len; i++)
        str[i] = rand();
}

static void codec_forward(uint32_t /*clid*/, void* /*data*/, size_t /*len*/)
{
    // TODO
}

static void codec_backward(uint32_t /*clid*/, void* /*data*/, size_t /*len*/)
{
    // TODO
}
