// Base64-m encoder/decoder (C) Dmitry 'sciloaf' Solovyev, 2024
#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define BASE64M_LIGHT

#ifndef BASE64M_LIGHT
    // This string was slightly modified: slash replaced with minus for better compatibility with paths
    static const char base64_str[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    static int* base64_decode_lut = NULL;
    #define BASE64M_TOCHAR(X) (base64_str[(X)])
#else
    // Even more "modified" encoding - fast to encode and decode, but not compatible with "classic" ASCII armoring
    #define BASE64M_START '.'
    #define BASE64M_TOCHAR(X) (X) + (BASE64M_START)
#endif


static size_t encode(char* out, size_t maxlen, const void* in, size_t inlen)
{
    if (!in || !out || !inlen || !maxlen) return 0;

    size_t n = 0;
    uint8_t b;
    const uint8_t* ina = (const uint8_t*)in;
    for (size_t i = 0; i < inlen && n < maxlen-1; i++) {
        uint8_t t = ina[i];
        uint8_t s = (i < inlen-1)? ina[i+1] : 0;
        switch (i % 3) {
        case 0:
            out[n++] = BASE64M_TOCHAR(t >> 2);
            b = (t << 4) | (s >> 4);
            break;
        case 1:
            b = (t << 2) | (s >> 6);
            break;
        case 2:
            b = t;
            break;
        }
        if (n < maxlen-1) out[n++] = BASE64M_TOCHAR(b & 0x3F);
    }
    out[n] = 0;
    return n;
}

#ifndef BASE64M_LIGHT
static void base64_mktab()
{
    if (base64_decode_lut) return;
    base64_decode_lut = (int*)malloc(128*sizeof(int));
    if (!base64_decode_lut) abort();

    for (int i = 0; i < 128; i++) {
        const char* ptr = (i < 32)? NULL : strchr(base64_str,i);
        base64_decode_lut[i] = ptr? (ptr - base64_str) : -1;
    }
}
#endif

static size_t decode(void* out, size_t maxlen, const char* in)
{
    if (!in || !out || !maxlen) return 0;

#ifndef BASE64M_LIGHT
    base64_mktab();
#endif

    size_t n = 0;
    uint16_t acc = 0, c = 0;
    uint8_t* outb = (uint8_t*)out;
    while (*in) {

#ifndef BASE64M_LIGHT
        int ex = base64_decode_lut[(*in) & 0x7F];
        if (ex < 0) break;
#else
        if ((*in) < BASE64M_START || (*in) >= (BASE64M_START+64)) break;
        uint8_t ex = (*in) - BASE64M_START;
#endif

        acc <<= 6;
        acc |= ex;
        c += 6;
        if (c >= 8) {
            outb[n++] = acc >> (c-8);
            c -= 8;
        }
        if (n >= maxlen) break;
        in++;
    }
    return n;
}
