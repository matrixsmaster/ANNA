#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <thread>
#include <utility>
#include "httplib.h"
#include "base64m.h"

#define PORT 8080
#define INFO(...) fprintf(stderr,"[INFO] " __VA_ARGS__);

using namespace std;
using namespace httplib;

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

void install_services(Server* srv)
{
    srv->Get("/hi/:id", [](const Request &req, Response &res) {
        int id = atoi(req.path_params.at("id").c_str());
        char buf[256];
        snprintf(buf,sizeof(buf),"Hello world, user #%d!",id);
        res.set_content(buf,"text/plain");
        cout << rlog(req) << endl;
    });

    srv->Post("/test/:id", [](const Request& req, Response& res) {
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
}

void server_thread()
{
    Server srv;
    install_services(&srv);

    INFO("Starting to listen at %d\n",PORT);
    srv.listen("0.0.0.0",PORT);
    INFO("Stopped listening");
}

int main(int argc, char* argv[])
{
    thread srv_thr(server_thread);
    cout << "Server started." << endl;

    while (1) {
        // main loop
    }
}
