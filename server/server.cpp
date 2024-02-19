#include <vector>
#include <string>
#include <map>
#include <deque>
#include <iostream>
#include <thread>
#include <utility>
#include <unistd.h>
#include "httplib.h"
#include "base64m.h"
#include "codec.h"
#include "../dtypes.h"
#include "../brain.h"

#define SERVER_VERSION "0.1.1c"
#define SERVER_SAVE_DIR "saves"
#define SERVER_PORT 8080
#define SERVER_SCHED_WAIT 100000
#define SERVER_CLIENT_TIMEOUT 10

#define INFO(...) do { fprintf(stderr,"[INFO] " __VA_ARGS__); fflush(stderr); } while (0)
#define WARN(...) do { fprintf(stderr,"[WARN] " __VA_ARGS__); fflush(stderr); } while (0)
#define ERROR(...) do { fprintf(stderr,"[ERROR] " __VA_ARGS__); fflush(stderr); } while (0)

using namespace std;
using namespace httplib;

enum client_state {
    ANNASERV_CLIENT_CREATED = 0,
    ANNASERV_CLIENT_CONFIGURED,
    ANNASERV_CLIENT_LOADED,
    ANNASERV_CLIENT_UNLOADED,
    ANNASERV_CLIENT_NUMSTATES
};

struct session {
    client_state state;
    string addr;
    AnnaConfig cfg;
    AnnaBrain* brain;
    int reqs;
    chrono::time_point<chrono::steady_clock> last_req;
    mutex lk;
};

map<int,session> usermap;
deque<int> userqueue;
int active_user = -1;
mutex usermap_mtx, q_lock;
bool quit = false;

void add_queue(int id)
{
    q_lock.lock();
    if (userqueue.empty() || find(userqueue.begin(),userqueue.end(),id) == userqueue.end())
        userqueue.push_back(id);
    q_lock.unlock();
}

bool mod_user(int id, AnnaConfig& cfg)
{
    if (!usermap.count(id)) return false;
    INFO("Updating user %d\n",id);
    INFO("Model file: %s\nContext size: %d\nPrompt: %s\nSeed: %u\n",
            cfg.params.model,cfg.params.n_ctx,cfg.params.prompt,cfg.params.seed);

    usermap[id].lk.lock();
    usermap[id].cfg = cfg;
    client_state oldstate = usermap[id].state;
    usermap[id].state = ANNASERV_CLIENT_CONFIGURED;

    AnnaBrain* bp = usermap.at(id).brain;
    if (bp) {
        bp->setConfig(cfg);
        if (oldstate > ANNASERV_CLIENT_CONFIGURED) usermap[id].state = oldstate;
        INFO("Brain config updated\n");

    } else if (active_user < 0) {
        // have capacity to create a new brain
        q_lock.lock();
        INFO("Creating new brain...\n");

        bp = new AnnaBrain(&cfg);
        usermap[id].brain = bp;
        usermap[id].state = ANNASERV_CLIENT_LOADED;

        active_user = id;
        q_lock.unlock();
        INFO("New brain created, config set and active user is now %d\n",id);

    } else {
        // no capacity, add it to the queue
        add_queue(id);
        INFO("Can't create or modify brain config for user %d - postponed\n",id);
    }

    usermap[id].lk.unlock();
    return true;
}

bool del_user(int id)
{
    if (!usermap.count(id)) return false;

    usermap_mtx.lock();
    usermap[id].lk.lock();

    AnnaBrain* ptr = usermap.at(id).brain;
    if (ptr) delete ptr;
    string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);

    usermap.erase(id);
    reclaim_clid(id);
    if (!remove(fn.c_str())) INFO("Save file %s removed\n",fn.c_str());
    usermap_mtx.unlock();

    INFO("User %d session ended\n",id);
    return true;
}

bool hold_user(int id)
{
    if (!usermap.count(id)) return false;

    usermap[id].lk.lock();
    bool res = false;
    AnnaBrain* ptr = usermap.at(id).brain;
    if (ptr) {
        string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);
        INFO("Saving user %d state into %s: ",id,fn.c_str());
        if (ptr->SaveState(fn,nullptr,0)) INFO("success\n");
        else ERROR("failure! %s\n",ptr->getError().c_str());
        delete ptr;
        usermap[id].brain = nullptr;
        usermap[id].state = ANNASERV_CLIENT_UNLOADED;
        res = true;
    } else
        ERROR("Unable to hold inactive user!\n");
    usermap[id].lk.unlock();

    if (res) INFO("User %d session suspended\n",id);
    return res;
}

bool unhold_user(int id)
{
    if (!usermap.count(id)) return false;
    if (usermap.at(id).state != ANNASERV_CLIENT_CONFIGURED && usermap.at(id).state != ANNASERV_CLIENT_UNLOADED) {
        WARN("Unable to resume unconfigured or loaded client. Client %d state is %d\n",id,usermap.at(id).state);
        return false;
    }

    usermap[id].lk.lock();
    bool res = false;
    AnnaBrain* ptr = usermap.at(id).brain;
    if (!ptr) {
        INFO("Re-creating a brain for user %d\n",id);
        ptr = new AnnaBrain(&(usermap[id].cfg));
        usermap[id].brain = ptr;
        res = (ptr->getState() != ANNA_ERROR);

        if (res && usermap[id].state == ANNASERV_CLIENT_UNLOADED) {
            string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);
            INFO("Loading user %d state from %s: ",id,fn.c_str());
            if (ptr->LoadState(fn,nullptr,nullptr)) {
                INFO("success\n");
                res = true;
            } else
                ERROR("failure! %s\n",ptr->getError().c_str());
        }

        if (res) usermap[id].state = ANNASERV_CLIENT_LOADED;

    } else
        ERROR("Unable to unhold active user %d!\n",id);

    usermap[id].last_req = chrono::steady_clock::now();
    usermap[id].lk.unlock();

    if (res) INFO("User %d session resumed\n",id);
    return res;
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

int check_request(const Request& req, Response& res, const string funame)
{
    //cout << rlog(req) << endl;
    int id = atoi(req.path_params.at("id").c_str());
    if (!usermap.count(id)) {
        WARN("%s() requested for non-existing user %d\n",funame.c_str(),id);
        res.status = BadRequest_400;
        return 0;
    }
    if (usermap.at(id).addr != req.remote_addr) {
        ERROR("Request %s() for user %d came from wrong IP (%s expected, got %s)!\n",funame.c_str(),id,usermap.at(id).addr.c_str(),req.remote_addr.c_str());
        res.status = Forbidden_403;
        return 0;
    }

    usermap[id].lk.lock();
    usermap[id].reqs++;
    usermap[id].last_req = chrono::steady_clock::now();
    usermap[id].lk.unlock();

    INFO("%s() for user %d...\n",funame.c_str(),id);
    return id;
}

bool check_brain(int id, const string funame, Response& res)
{
    usermap[id].lk.lock();
    AnnaBrain* ptr = usermap.at(id).brain;
    client_state s = usermap.at(id).state;
    usermap[id].lk.unlock();

    if (ptr) return true;

    switch (s) {
    case ANNASERV_CLIENT_CONFIGURED:
    case ANNASERV_CLIENT_UNLOADED:
        // valid request, just need to wait for client unhold
        INFO("Putting request %s from user %d into queue\n",funame.c_str(),id);
        res.status = ServiceUnavailable_503;
        add_queue(id);
        break;

    default:
        WARN("Invalid request %s from user %d - brain does not exist and state is not on hold\n",funame.c_str(),id);
        res.status = BadRequest_400;
    }

    return false;
}

string to_base64(uint32_t clid, const void* in, size_t inlen)
{
    uint8_t* tmp = (uint8_t*)malloc(inlen);
    if (!tmp) {
        ERROR("Unable to allocate %lu bytes of memory!\n",inlen);
        return 0;
    }

    memcpy(tmp,in,inlen);
    codec_forward(clid,tmp,inlen);

    string s;
    s.resize(inlen*2+1);
    size_t n = encode((char*)s.data(),s.size(),tmp,inlen);
    s.resize(n);

    INFO("Data encoded: %lu bytes from %lu bytes\n",n,inlen);
    free(tmp);
    return s;
}

size_t from_base64(uint32_t clid, void* out, size_t maxlen, const char* in)
{
    size_t n = decode(out,maxlen,in);
    codec_backward(clid,out,n);
    INFO("%lu bytes decoded from %lu bytes string\n",n,strlen(in));
    return n;
}

string from_base64_str(uint32_t clid, string in)
{
    string s;
    s.resize(in.size());
    size_t n = from_base64(clid,s.data(),s.size(),in.c_str());
    s.resize(n);
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

        usermap_mtx.lock();
        usermap[id].state = ANNASERV_CLIENT_CREATED;
        usermap[id].addr = req.remote_addr;
        usermap[id].brain = nullptr;
        usermap[id].reqs = 0;
        usermap[id].last_req = chrono::steady_clock::now();
        usermap_mtx.unlock();

        INFO("User %d session created\n",id);
        return;
    });

    srv->Post("/sessionEnd/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"sessionEnd");
        if (!id) return;
        del_user(id);
    });

    srv->Get("/getState/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getState");
        if (!id) return;
        if (!check_brain(id,"getState",res)) return;

        usermap[id].lk.lock();
        AnnaState s = usermap.at(id).brain->getState();
        usermap[id].lk.unlock();

        string str = AnnaBrain::myformat("%d",(int)s);
        res.set_content(str,"text/plain");
        INFO("getState() for user %d: %s\n",id,str.c_str());
    });

    srv->Post("/setConfig/:id", [](const Request& req, Response& res) {
        //INFO("sizeof AnnaConfig = %lu\n",sizeof(AnnaConfig));
        int id = check_request(req,res,"setConfig");
        if (!id) return;

        AnnaConfig cfg;
        size_t r = from_base64(id,&cfg,sizeof(cfg),req.body.c_str());
        if (r != sizeof(cfg)) {
            ERROR("Unable to decode params: %lu bytes read, %lu bytes needed\n",r,sizeof(cfg));
            res.status = BadRequest_400;
            return;
        }
        INFO("%lu bytes decoded\n",r);
        cfg.user = nullptr; // not needed on the server

        if (!check_brain(id,"setConfig",res)) {
            if (res.status == BadRequest_400) // still a valid request - we just need to initialize the brain
                res.status = OK_200;
            else
                return;
        }

        mod_user(id,cfg);
        INFO("setConfig() complete\n");
        return;
    });

    srv->Get("/getConfig/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"getConfig");
        if (!id) return;
        if (!check_brain(id,"getConfig",res)) return;

        usermap[id].lk.lock();
        AnnaConfig cfg = usermap.at(id).brain->getConfig();
        usermap[id].cfg = cfg;
        usermap[id].lk.unlock();

        codec_infill_str(cfg.params.model,sizeof(cfg.params.model));
        codec_infill_str(cfg.params.prompt,sizeof(cfg.params.prompt));
        res.set_content(to_base64(id,&cfg,sizeof(cfg)),"text/plain");
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
        if (!check_brain(id,"processing",res)) return;

        string arg = req.get_param_value("arg");

        usermap[id].lk.lock();
        AnnaState s = usermap.at(id).brain->Processing(arg == "skip");
        usermap[id].lk.unlock();

        string str = AnnaBrain::myformat("%d",(int)s);
        res.set_content(str,"text/plain");
        INFO("processing(%s) for user %d: %s\n",arg.c_str(),id,str.c_str());
    });

    srv->Get("/getOutput/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getOutput");
        if (!id) return;
        if (!check_brain(id,"processing",res)) return;

        usermap[id].lk.lock();
        AnnaBrain* ptr = usermap.at(id).brain;
        string str = ptr->getOutput();
        usermap[id].lk.unlock();

        res.set_content(to_base64(id,str.data(),str.size()),"text/plain");
        INFO("getOutput() for user %d: %s\n",id,str.c_str());
    });

    srv->Post("/setInput/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setInput");
        if (!id) return;
        if (!check_brain(id,"setInput",res)) return;

        string str = from_base64_str(id,req.body);
        INFO("setInput() for user %d: '%s'\n",id,str.c_str());

        usermap[id].lk.lock();
        usermap.at(id).brain->setInput(str);
        usermap[id].lk.unlock();

        INFO("Brain input set\n");
        return;
    });

    srv->Post("/setPrefix/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setPrefix");
        if (!id) return;
        if (!check_brain(id,"setPrefix",res)) return;

        string str = from_base64_str(id,req.body);
        INFO("setPrefix() for user %d: '%s'\n",id,str.c_str());

        usermap[id].lk.lock();
        usermap.at(id).brain->setPrefix(str);
        usermap[id].lk.unlock();
        INFO("Prefix is set\n");
        return;
    });

    srv->Get("/getError/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getError");
        if (!id) return;
        if (!check_brain(id,"getError",res)) return;

        usermap[id].lk.lock();
        string str = usermap.at(id).brain->getError();
        usermap[id].lk.unlock();

        res.set_content(to_base64(id,str.data(),str.size()),"text/plain");
        INFO("getError() for user %d: %s\n",id,str.c_str());
    });

    srv->Get("/getTokensUsed/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getTokensUsed");
        if (!id) return;
        if (!check_brain(id,"getError",res)) return;

        usermap[id].lk.lock();
        string str = AnnaBrain::myformat("%d",usermap.at(id).brain->getTokensUsed());
        usermap[id].lk.unlock();

        res.set_content(str,"text/plain");
        INFO("getTokensUsed() for user %d: %s\n",id,str.c_str());
    });
}

void server_thread(Server* srv)
{
    INFO("Installing services...\n");
    install_services(srv);
    INFO("Services installed\n");

    INFO("Starting to listen at %d\n",SERVER_PORT);
    srv->listen("0.0.0.0",SERVER_PORT);

    INFO("Stopped listening\n");
}

void sched_thread()
{
    INFO("Scheduler started\n");

    while (!quit) {
        usermap_mtx.lock();
        q_lock.lock();

        // check for active user's validity
        if (active_user > 0) {
            if (usermap.count(active_user) < 1) {
                WARN("Invalid active user %d (not existing), removing",active_user);
                active_user = -1;
            }
        }

        // sanitize the queue
        for (auto qi = userqueue.begin(); qi != userqueue.end();) {
            if (*qi == active_user || usermap.count(*qi) < 1)
                qi = userqueue.erase(qi);
            else
                ++qi;
        }

        // check for active user's timeout
        if (active_user > 0) {
            float age = (float)((chrono::steady_clock::now() - usermap.at(active_user).last_req) / 1ms) / 1000.f;
            if (userqueue.empty()) age = 0; // no need to switch, as there's no more requests atm
            if (usermap[active_user].lk.try_lock()) usermap[active_user].lk.unlock(); // just test the lock
            else age = 0; // if locked - something's happening, so we can't release this client yet

            if (age > SERVER_CLIENT_TIMEOUT) {
                // put the client on hold and reset active user
                hold_user(active_user);
                active_user = -1;
            }
        }

        // check for the queue
        if (active_user < 0 && !userqueue.empty()) {
            active_user = userqueue.front();
            userqueue.pop_front();

            // check if it's still valid
            if (usermap.count(active_user)) {
                if (!unhold_user(active_user)) { // and resume it
                    // can't resume - delete
                    del_user(active_user);
                    ERROR("Unable to resume session for user %d - deleting session now\n",active_user);
                    active_user = -1;
                }
            } else
                active_user = -1;
        }

        q_lock.unlock();
        usermap_mtx.unlock();
        usleep(SERVER_SCHED_WAIT);
    }

    INFO("Scheduler stopped\n");
}

string get_input(string prompt)
{
    string s;
    ssize_t n = -1;
    char cbuf[2];

    printf("%s",prompt.c_str());
    fflush(stdout);

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
    srand(time(NULL));

    Server srv;
    thread srv_thr(server_thread,&srv);
    thread sched_thr(sched_thread);
    INFO("Server started\n");

    // main CLI loop
    sleep(1); // give time to other threads
    while (!quit) {
        string c = get_input("ANNA> ");

        if (c == "quit" || c == "q") {
            quit = true;
            break;

        } else if (c == "list" || c == "ls") {
            puts("=======================================");
            usermap_mtx.lock();
            const auto now = chrono::steady_clock::now();
            for (auto & i : usermap) {
                float age = (float)((now - i.second.last_req) / 1ms) / 1000.f;
                printf("Client %d (0x%08X): state %d, IP %s, %d requests, last one %.2f seconds ago\n",
                        i.first,i.first,i.second.state,i.second.addr.c_str(),i.second.reqs,age);
            }
            usermap_mtx.unlock();
            puts("=======================================");
            printf("Current active user: %d\n",active_user);

        } else if (c == "queue") {
            q_lock.lock();
            puts("=======================================");
            for (auto i : userqueue) printf("%d (0x%08X)\n",i,i);
            puts("=======================================");
            printf("%lu requests in the queue\n",userqueue.size());
            q_lock.unlock();

        } else if (c == "kick" || c == "sus" || c == "res") {
            string a = get_input("Client ID> ");
            if (a.empty()) continue;
            int id = atoi(a.c_str());
            if (id < 1) continue;
            if (c == "kick") del_user(id);
            else if (c == "sus") hold_user(id);
            else if (c == "res") unhold_user(id);
        }
    }

    INFO("Stopping the server...\n");
    srv.stop();
    srv_thr.join();
    sched_thr.join();

    puts("Server exits normally.");
}
