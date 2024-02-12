// Base64-m encoder/decoder (C) Dmitry 'sciloaf' Solovyev, 2024
#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>

// This string was slightly modified: slash replaced with minus for better compatibility with paths
static const char base64_str[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

size_t encode(char* out, size_t maxlen, const void* in, size_t inlen)
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
            out[n++] = base64_str[t >> 2];
            b = (t << 4) | (s >> 4);
            break;
        case 1:
            b = (t << 2) | (s >> 6);
            break;
        case 2:
            b = t;
            break;
        }
        if (n < maxlen-1) out[n++] = base64_str[b & 0x3F];
    }
    out[n] = 0;
    return n;
}

size_t decode(void* out, size_t maxlen, const char* in)
{
    if (!in || !out || !maxlen) return 0;

    size_t n = 0;
    uint16_t acc = 0, c = 0;
    uint8_t* outb = (uint8_t*)out;
    const char* ptr;
    while (*in) {
        ptr = strchr(base64_str,*in);
        if (!ptr) break;
        acc <<= 6;
        acc |= (ptr - base64_str);
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
