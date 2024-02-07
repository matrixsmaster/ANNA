#include "../httplib.h"
#include <iostream>

using namespace std;

// This string was slightly modified: slash replaced with minus for better compatibility with paths
const char base64_str[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

// Base64-m encoder (C) Dmitry 'sciloaf' Solovyev, 2024
void encode(char* out, size_t maxlen, const void* in, size_t inlen)
{
    if (!in || !out || !inlen || !maxlen) return;

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
}

void postfile(httplib::Client* cli, const char* fn)
{
    FILE* f = fopen(fn,"rb");
    if (!f) {
        cerr << "Unable to open file" << endl;
        return;
    }

    auto res = cli->Post("/test/1234",[f](size_t offset, httplib::DataSink &sink) {
        char buf[300], sbuf[4096];
        int r = fread(buf,1,300,f);
        if (r > 0) {
            encode(sbuf,sizeof(sbuf),buf,r);
            cout << "Sending piece: " << sbuf << endl;
            sink.os << sbuf;
        } else
            sink.done();
        return true; // return 'false' if you want to cancel the request.
    },"text/plain");

    fclose(f);

    if (res)
        cout << res->status << endl;
    else
        cerr << "Unable to POST" << endl;
}

int main()
{
    httplib::Client cli("http://127.0.0.1:8080");
    if (!cli.is_valid()) {
        cerr << "Invalid client." << endl;
        return 1;
    }
    cout << "Started." << endl;

    auto res = cli.Get("/hi/1234");
    if (res) {
        cout << res->status << endl;
        cout << res->body << endl;
    } else
        cerr << "Unable to get response" << endl;

    postfile(&cli,"/tmp/test");

    cout << "Finished." << endl;
}
