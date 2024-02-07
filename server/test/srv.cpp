#include "../httplib.h"
#include <iostream>

using namespace std;
using namespace httplib;

// This string was slightly modified: slash replaced with minus for better compatibility with paths
const char base64_str[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

// Base64-m decoder (C) Dmitry 'sciloaf' Solovyev, 2024
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

string rlog(const Request &req) {
    std::string s;
    char buf[BUFSIZ];

    s += "================================\n";
    snprintf(buf, sizeof(buf), "%s:%d\t", req.remote_addr.c_str(),req.remote_port);
    s += buf;

    snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
                     req.version.c_str(), req.path.c_str());
    s += buf;

    std::string query;
    for (auto it = req.params.begin(); it != req.params.end(); ++it) {
        const auto &x = *it;
        snprintf(buf, sizeof(buf), "%c%s=%s",
                         (it == req.params.begin()) ? '?' : '&', x.first.c_str(),
                         x.second.c_str());
        query += buf;
    }
    snprintf(buf, sizeof(buf), "%s\n", query.c_str());
    s += buf;

    s += "--------------------------------\n";
    return s;
}

int main()
{
    cout << "Server started." << endl;

    httplib::Server srv;
    srv.Get("/hi/:id", [](const httplib::Request &req, httplib::Response &res) {
        int id = atoi(req.path_params.at("id").c_str());
        char buf[256];
        snprintf(buf,sizeof(buf),"Hello world, user #%d!",id);
        res.set_content(buf,"text/plain");
        cout << rlog(req) << endl;
    });

    srv.Post("/test/:id", [](const httplib::Request& req, httplib::Response& res) {
        int id = atoi(req.path_params.at("id").c_str());
        string s = req.body;
        printf("Post from user #%d:\n'%s'\n",id,s.c_str());
        FILE* f = fopen("/tmp/test.out","wb");
        if (!f) {
            cerr << "Unable to write output file" << endl;
            return false;
        }
        while (!s.empty()) {
            string ss = s.substr(0,300);
            s.erase(0,300);
            cout << ss << endl;
            char buf[4096];
            size_t r = decode(buf,sizeof(buf),ss.c_str());
            fwrite(buf,r,1,f);
        }
        fclose(f);
        cout << "File written" << endl;
        return true;
    });

    cout << "Before listen point" << endl;
    srv.listen("0.0.0.0", 8080);
    cout << "After listen point" << endl;
}
