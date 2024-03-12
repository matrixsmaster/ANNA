// Large File Support for multiple platforms
// borrowed from llama.cpp::llama_file
#pragma once

#include <cstdio>
#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

static size_t mtell(FILE* fp)
{
#ifdef _WIN32
    __int64 ret = _ftelli64(fp);
#else
    long ret = std::ftell(fp);
#endif
    return (size_t)ret;
}

static void mseek(FILE* fp, size_t offset, int whence)
{
#ifdef _WIN32
    _fseeki64(fp, (__int64) offset, whence);
#else
    std::fseek(fp, (long) offset, whence);
#endif
}
