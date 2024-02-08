#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <thread>
#include <utility>
#include "httplib.h"
#include "base64m.h"
#include "../dtypes.h"
#include "../brain.h"

#define PORT 8080
#define INFO(...) do { fprintf(stderr,"[INFO] " __VA_ARGS__); fflush(stderr); } while (0)
#define WARN(...) do { fprintf(stderr,"[WARN] " __VA_ARGS__); fflush(stderr); } while (0)
#define ERROR(...) do { fprintf(stderr,"[ERROR] " __VA_ARGS__); fflush(stderr); } while (0)

using namespace std;
using namespace httplib;

struct session {
    int state; // FIXME: placeholder
    gpt_params params;
    AnnaBrain* brain;
};

map<int,session> usermap;

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
    srv->Post("/sessionStart/:id", [](const Request &req, Response &res) {
        cout << rlog(req) << endl;
        int id = atoi(req.path_params.at("id").c_str());
        INFO("Starting session for user %d\n",id);
        if (usermap.count(id) > 0) {
            ERROR("User %d already exists, rejecting.\n",id);
            res.status = Forbidden_403;
            return;
        }
        session s;
        s.state = 0;
        s.brain = nullptr;
        usermap[id] = s;
        INFO("User %d session created\n",id);
        return;
    });

    srv->Post("/sessionEnd/:id", [](const Request &req, Response &res) {
        cout << rlog(req) << endl;
        int id = atoi(req.path_params.at("id").c_str());
        usermap.erase(id);
        INFO("User %d session ended\n",id);
    });

    srv->Get("/getState/:id", [](const Request &req, Response &res) {
        cout << rlog(req) << endl;
        int id = atoi(req.path_params.at("id").c_str());
        if (!usermap.count(id)) {
            WARN("getState() requested for non-existing user %d\n",id);
            res.status = BadRequest_400;
            return;
        }
        AnnaBrain* ptr = usermap.at(id).brain;
        AnnaState s = ANNA_NOT_INITIALIZED;
        if (ptr) s = ptr->getState();
        string str = AnnaBrain::myformat("%d",(int)s);
        res.set_content(str,"text/plain");
        INFO("getState() for user %d: %s\n",id,str.c_str());
    });

    srv->Post("/setConfig/:id", [](const Request& req, Response& res) {
        cout << rlog(req) << endl;
        int id = atoi(req.path_params.at("id").c_str());
        if (!usermap.count(id)) {
            WARN("getState() requested for non-existing user %d\n",id);
            res.status = BadRequest_400;
            return;
        }
        INFO("setConfig() for user %d...\n",id);
        gpt_params* ptr = &(usermap.at(id).params);
        size_t r = decode(ptr,sizeof(gpt_params),req.body.c_str());
        if (r != sizeof(gpt_params)) {
            ERROR("Unable to decode params: %lu bytes read, %lu bytes needed\n",r,sizeof(gpt_params));
            res.status = BadRequest_400;
            return;
        }
        INFO("%lu bytes decoded\n",r);
        INFO("Model file: %s\nContext size: %d\nPrompt: %s\nSeed: %u\n",ptr->model,ptr->n_ctx,ptr->prompt,ptr->seed);
        AnnaBrain* bp = usermap.at(id).brain;
        if (bp) {
            AnnaConfig cfg;
            cfg.params = usermap.at(id).params;
            cfg.verbose_level = 0;
            cfg.user = nullptr;
            bp->setConfig(cfg);
            INFO("Brain config set\n");
        }
        INFO("setConfig() complete\n");
        return;
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
    INFO("sizeof gpt_params = %lu\n",sizeof(gpt_params));

    thread srv_thr(server_thread);
    cout << "Server started." << endl;

    while (1) {
        // main loop
    }
}
