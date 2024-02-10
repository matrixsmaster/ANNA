#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <thread>
#include <utility>
#include <unistd.h>
#include "httplib.h"
#include "base64m.h"
#include "../dtypes.h"
#include "../brain.h"

#define SERVER_VERSION "0.0.3b"

#define PORT 8080
#define INFO(...) do { fprintf(stderr,"[INFO] " __VA_ARGS__); fflush(stderr); } while (0)
#define WARN(...) do { fprintf(stderr,"[WARN] " __VA_ARGS__); fflush(stderr); } while (0)
#define ERROR(...) do { fprintf(stderr,"[ERROR] " __VA_ARGS__); fflush(stderr); } while (0)

using namespace std;
using namespace httplib;

struct session {
    int state; // FIXME: placeholder
    string addr;
    AnnaBrain* brain;
    int reqs;
};

map<int,session> usermap;
bool quit = false;

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

int check_request(const Request& req, Response& res, const string fname)
{
    //cout << rlog(req) << endl;
    int id = atoi(req.path_params.at("id").c_str());
    if (!usermap.count(id)) {
        WARN("%s() requested for non-existing user %d\n",fname.c_str(),id);
        res.status = BadRequest_400;
        return 0;
    }
    if (usermap.at(id).addr != req.remote_addr) {
        ERROR("Request %s() for user %d came from wrong IP (%s expected, got %s)!\n",fname.c_str(),id,usermap.at(id).addr.c_str(),req.remote_addr.c_str());
        res.status = Forbidden_403;
        return 0;
    }
    usermap[id].reqs++;
    return id;
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
        s.addr = req.remote_addr;
        s.brain = nullptr;
        s.reqs = 0;
        usermap[id] = s;
        INFO("User %d session created\n",id);
        return;
    });

    srv->Post("/sessionEnd/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"sessionEnd");
        if (!id) return;
        AnnaBrain* ptr = usermap.at(id).brain;
        if (ptr) delete ptr;
        usermap.erase(id);
        INFO("User %d session ended\n",id);
    });

    srv->Get("/getState/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getState");
        if (!id) return;
        AnnaBrain* ptr = usermap.at(id).brain;
        AnnaState s = ANNA_NOT_INITIALIZED;
        if (ptr) s = ptr->getState();
        string str = AnnaBrain::myformat("%d",(int)s);
        res.set_content(str,"text/plain");
        INFO("getState() for user %d: %s\n",id,str.c_str());
    });

    srv->Post("/setConfig/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setConfig");
        if (!id) return;
        INFO("sizeof AnnaConfig = %lu\n",sizeof(AnnaConfig));
        INFO("setConfig() for user %d...\n",id);
        AnnaConfig cfg;
        size_t r = decode(&cfg,sizeof(cfg),req.body.c_str());
        if (r != sizeof(cfg)) {
            ERROR("Unable to decode params: %lu bytes read, %lu bytes needed\n",r,sizeof(cfg));
            res.status = BadRequest_400;
            return;
        }
        INFO("%lu bytes decoded\n",r);
        INFO("Model file: %s\nContext size: %d\nPrompt: %s\nSeed: %u\n",
                cfg.params.model,cfg.params.n_ctx,cfg.params.prompt,cfg.params.seed);
        AnnaBrain* bp = usermap.at(id).brain;
        cfg.user = nullptr; // not needed on the server
        if (bp) {
            bp->setConfig(cfg);
            INFO("Brain config set\n");
        } else {
            INFO("Creating new brain...\n");
            bp = new AnnaBrain(&cfg);
            usermap[id].brain = bp;
            INFO("New brain created and config is set\n");
        }
        INFO("setConfig() complete\n");
        return;
    });

    srv->Get("/getConfig/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"getConfig");
        if (!id) return;
        INFO("getConfig() for user %d...\n",id);
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("getConfig() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        AnnaConfig cfg = ptr->getConfig();
        string s;
        s.resize(sizeof(cfg)*2);
        size_t n = encode((char*)s.data(),s.size(),&cfg,sizeof(cfg));
        s.resize(n);
        INFO("Config encoded: %lu bytes\n",n);
        res.set_content(s,"text/plain");
        INFO("getConfig() complete\n");
        return;
    });

    srv->Get("/processing/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"processing");
        if (!id) return;
        if (!req.has_param("arg")) {
            WARN("processing() requested for user %d without argument\n",id);
            res.status = BadRequest_400;
            return;
        }
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("processing() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        string arg = req.get_param_value("arg");
        AnnaState s = ptr->Processing(arg == "skip");
        string str = AnnaBrain::myformat("%d",(int)s);
        res.set_content(str,"text/plain");
        INFO("processing(%s) for user %d: %s\n",arg.c_str(),id,str.c_str());
    });

    srv->Get("/getOutput/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getOutput");
        if (!id) return;
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("getOutput() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        string str = ptr->getOutput();
        res.set_content(str,"text/plain");
        INFO("getOutput() for user %d: %s\n",id,str.c_str());
    });

    srv->Post("/setInput/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setInput");
        if (!id) return;
        INFO("setInput() for user %d: '%s'\n",id,req.body.c_str());
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("setInput() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        ptr->setInput(req.body);
        INFO("Brain input set\n");
        return;
    });

    srv->Post("/setPrefix/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setPrefix");
        if (!id) return;
        INFO("setPrefix() for user %d: '%s'\n",id,req.body.c_str());
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("setPrefix() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        ptr->setPrefix(req.body);
        INFO("Brain input set\n");
        return;
    });

    srv->Get("/getError/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getError");
        if (!id) return;
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("getError() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        string str = ptr->getError();
        res.set_content(str,"text/plain");
        INFO("getError() for user %d: %s\n",id,str.c_str());
    });

    srv->Get("/getTokensUsed/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getTokensUsed");
        if (!id) return;
        AnnaBrain* ptr = usermap.at(id).brain;
        if (!ptr) {
            ERROR("getTokensUsed() for user %d requested before brain is created\n",id);
            res.status = BadRequest_400;
            return;
        }
        string str = AnnaBrain::myformat("%d",ptr->getTokensUsed());
        res.set_content(str,"text/plain");
        INFO("getTokensUsed() for user %d: %s\n",id,str.c_str());
    });
}

void server_thread(Server* srv)
{
    install_services(srv);

    INFO("Starting to listen at %d\n",PORT);
    srv->listen("0.0.0.0",PORT);
    INFO("Stopped listening\n");
}

string get_input()
{
    string s;
    ssize_t n = -1;
    char cbuf[2];

    while (!quit) {
        n = read(0,cbuf,1);
        if (n <= 0) break;
        if (*cbuf == '\n') break;
        else s += *cbuf;
    }
    return s;
}

int main()
{
    printf("\nANNA Server version " SERVER_VERSION " starting up\n\n");

    Server srv;
    thread srv_thr(server_thread,&srv);
    puts("Server started.");

    // main CLI loop
    while (!quit) {
        string c = get_input();

        if (c == "quit") {
            quit = true;
            break;

        } else if (c == "list") {
            puts("=======================================");
            for (auto & i : usermap)
                printf("Client %d (0x%08X): IP %s, %d requests\n",i.first,i.first,i.second.addr.c_str(),i.second.reqs);
            puts("=======================================");
        }
    }

    puts("Stopping the server...");
    srv.stop();
    srv_thr.join();

    puts("Server exits normally.");
}
