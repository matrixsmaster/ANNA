#include <vector>
#include <string>
#include <map>
#include <deque>
#include <iostream>
#include <thread>
#include <utility>
#include <stdio.h>
#include <unistd.h>
#include "httplib.h"
#include "base64m.h"
#include "codec.h"
#include "../dtypes.h"
#include "../brain.h"
#include "../common.h"
#include "../vecstore.h"

#define SERVER_VERSION "0.1.6"
#define SERVER_DEBUG 1

#define SERVER_SAVE_DIR "saves"
#define SERVER_DBGLOG_DIR "debug_log"
#define SERVER_MODEL_DIR "models"

#define SERVER_PORT 8080

#define SERVER_SCHED_WAIT 100000
#define SERVER_CLIENT_TIMEOUT 5
#define SERVER_CLIENT_MAXTIME 30
//#define SERVER_CLIENT_DEAD_TO 24

#define SERVER_CLIENT_CHUNK (16ULL * 1024ULL * 1024ULL)

#define SERVER_DEF_CPU_THREADS 12
#define SERVER_DEF_GPU_VRAM (14ULL * 1024ULL * 1024ULL * 1024ULL)
#define SERVER_DEF_GPU_MARGIN 0.86

#define INFO(...) do { fprintf(stderr,"[INFO] " __VA_ARGS__); fflush(stderr); } while (0)
#define WARN(...) do { fprintf(stderr,"[WARN] " __VA_ARGS__); fflush(stderr); } while (0)
#define ERROR(...) do { fprintf(stderr,"[ERROR] " __VA_ARGS__); fflush(stderr); } while (0)

#ifdef SERVER_DEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;
using namespace httplib;

enum client_state {
    ANNASERV_CLIENT_CREATED = 0,
    ANNASERV_CLIENT_ERROR,
    ANNASERV_CLIENT_CONFIGURED,
    ANNASERV_CLIENT_LOADED,
    ANNASERV_CLIENT_UNLOADED,
    ANNASERV_CLIENT_TRANSFER,
    ANNASERV_CLIENT_NUMSTATES
};

struct session {
    client_state state, old_state;
    string addr;
    AnnaConfig cfg;
    AnnaBrain* brain;
    int gpu_layers;
    int reqs;
    time_t started;
    chrono::time_point<chrono::steady_clock> last_req;
    mutex lk;
    string last_error;
    list<string> iolog;
    FILE* fhandle;
    size_t transize;
};

map<int,session> usermap;
deque<int> userqueue;
int active_user = -1;
mutex usermap_mtx, q_lock;
bool quit = false;

#ifdef SERVER_DEBUG

string rlog(const Request &req)
{
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

void save_log(int id)
{
    usermap[id].lk.lock();

    string fn = AnnaBrain::myformat("%s/%s-%d.log",SERVER_DBGLOG_DIR,usermap[id].addr.c_str(),id);
    time_t end = time(NULL);
    string period = AnnaBrain::myformat("Started: %s; ",ctime(&(usermap[id].started)));
    period += AnnaBrain::myformat("Now: %s",ctime(&end));

    FILE* f = fopen(fn.c_str(),"wb");
    if (!f) {
        ERROR("Unable to write log file '%s' for client %d\n",fn.c_str(),id);
        goto log_end;
    }

    if (!fwrite(&(usermap[id].cfg),sizeof(AnnaConfig),1,f)) goto log_end;
    if (!vector_storage<char>::store(vector_storage<char>::from_string(period),f)) goto log_end;
    for (auto & i: usermap[id].iolog)
        if (!vector_storage<char>::store(vector_storage<char>::from_string(i),f)) goto log_end;

log_end:
    usermap[id].lk.unlock();
    if (f) fclose(f);
}

#else

// nothing to do in release config
string rlog(const Request &) { return ""; }
void save_log(int) {}

#endif /* SERVER_DEBUG */

void add_queue(int id)
{
    q_lock.lock();
    if (userqueue.empty() || find(userqueue.begin(),userqueue.end(),id) == userqueue.end())
        userqueue.push_back(id);
    q_lock.unlock();
}

int get_max_gpu_layers(AnnaConfig* cfg)
{
    // first of all, let's determine the overall size of the model file
    FILE* f = fopen(cfg->params.model,"rb");
    if (!f) {
        WARN("Unable to open file '%s'!\n",cfg->params.model);
        return 0;
    }
    fseek(f,0,SEEK_END);
    size_t fsz = ftell(f);
    fclose(f);

    // we need to know the number of layers and the state size, so (pre-)load the model
    llama_model* mdl;
    llama_context* ctx;
    tie(mdl,ctx) = llama_init_from_gpt_params(cfg->params);
    if (!mdl) {
        WARN("Unable to test-load model '%s'. Using no GPU offload for it!\n",cfg->params.model);
        return 0;
    }

    DBG("Model size: %lu\nLayers: %d\nState size: %lu\n",fsz,llama_model_n_layers(mdl),llama_get_state_size(ctx));
    double totsz = llama_get_state_size(ctx) + fsz;
    double fulloff = (double)SERVER_DEF_GPU_VRAM * (double)llama_model_n_layers(mdl) / totsz;
    int off = floor(fulloff * SERVER_DEF_GPU_MARGIN);
    if (off < 0) off = 0;
    INFO("GPU offload for '%s' calculated as %.2f (%d) layers\n",cfg->params.model,fulloff,off);

    llama_free(ctx);
    llama_free_model(mdl);

    return off;
}

bool mod_user(int id, const AnnaConfig& cfg)
{
    if (!usermap.count(id)) return false;
    save_log(id);

    INFO("Updating user %d\n",id);
    DBG("Model file: %s\nContext size: %d\nPrompt: %s\nSeed: %u\n",
            cfg.params.model,cfg.params.n_ctx,cfg.params.prompt,cfg.params.seed);

    usermap[id].lk.lock();
    usermap[id].cfg = cfg;

    if (usermap[id].state < ANNASERV_CLIENT_CONFIGURED)
        usermap[id].state = ANNASERV_CLIENT_CONFIGURED;

    AnnaBrain* bp = usermap.at(id).brain;
    if (bp) {
        // change immediately if we have such opportunity
        bp->setConfig(cfg);
        INFO("Brain config updated\n");
    }

    usermap[id].lk.unlock();
    return true;
}

bool del_user(int id)
{
    if (!usermap.count(id)) return false;
    save_log(id);

    usermap_mtx.lock();
    usermap[id].lk.lock();

    AnnaBrain* ptr = usermap.at(id).brain;
    if (ptr) delete ptr;

    string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);
    if (!remove(fn.c_str())) INFO("Save file %s removed\n",fn.c_str());

    usermap.erase(id);
    reclaim_clid(id);

    usermap_mtx.unlock();

    INFO("User %d session ended\n",id);
    return true;
}

bool hold_user(int id)
{
    if (!usermap.count(id)) return false;
    save_log(id);

    usermap[id].lk.lock();
    bool res = false;
    AnnaBrain* ptr = usermap.at(id).brain;
    if (ptr) {
        string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);
        INFO("Saving user %d state into %s: ",id,fn.c_str());
        if (ptr->SaveState(fn,nullptr,0))
            INFO("success\n");
        else {
            ERROR("failure! (%s)\n",ptr->getError().c_str());
            usermap[id].state = ANNASERV_CLIENT_ERROR;
            usermap[id].last_error = ptr->getError();
        }
        delete ptr;
        usermap[id].brain = nullptr;
        usermap[id].state = ANNASERV_CLIENT_UNLOADED;
        res = true;
    } else
        WARN("Unable to hold inactive user!\n");
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
    AnnaBrain* ptr = usermap[id].brain;
    if (!ptr) {
        DBG("Re-creating a brain for user %d\n",id);

        if (usermap[id].gpu_layers < 0)
            usermap[id].gpu_layers = get_max_gpu_layers(&(usermap[id].cfg));
        usermap[id].cfg.params.n_gpu_layers = usermap[id].gpu_layers;

        ptr = new AnnaBrain(&(usermap[id].cfg));
        usermap[id].brain = ptr;
        res = (ptr->getState() != ANNA_ERROR);
        if (!res) {
            ERROR("Unable to load model file %s: %s\n",usermap[id].cfg.params.model,ptr->getError().c_str());
            usermap[id].state = ANNASERV_CLIENT_ERROR;
            usermap[id].last_error = ptr->getError();
        }

        if (res && usermap[id].state == ANNASERV_CLIENT_UNLOADED) {
            string fn = AnnaBrain::myformat("%s/%d.anna",SERVER_SAVE_DIR,id);
            INFO("Loading user %d state from %s: ",id,fn.c_str());
            if (ptr->LoadState(fn,nullptr,nullptr))
                INFO("success\n");
            else {
                ERROR("failure! (%s)\n",ptr->getError().c_str());
                usermap[id].state = ANNASERV_CLIENT_ERROR;
                usermap[id].last_error = ptr->getError();
                delete ptr;
                usermap[id].brain = nullptr;
                WARN("Brain deleted\n");
                res = false;
            }
        }

        if (res) usermap[id].state = ANNASERV_CLIENT_LOADED;

    } else
        ERROR("Unable to unhold active user %d!\n",id);

    usermap[id].last_req = chrono::steady_clock::now();
    usermap[id].lk.unlock();

    if (res) INFO("User %d session resumed\n",id);
    else WARN("User %d session is NOT resumed\n",id);

    return res;
}

int check_request(const Request& req, Response& res, const string funame)
{
    //cout << rlog(req) << endl;
    int id = atoi(req.path_params.at("id").c_str());

    usermap_mtx.lock();
    int cnt = usermap.count(id);
    string id_addr = (cnt > 0)? usermap.at(id).addr : "";
    usermap_mtx.unlock();

    if (!cnt) {
        WARN("%s() requested for non-existing user %d\n",funame.c_str(),id);
        res.status = BadRequest_400;
        return 0;
    }
    if (id_addr != req.remote_addr) {
        ERROR("Request %s() for user %d came from wrong IP (%s expected, got %s)!\n",funame.c_str(),id,id_addr.c_str(),req.remote_addr.c_str());
        res.status = Forbidden_403;
        return 0;
    }

    DBG("%s() for user %d...\n",funame.c_str(),id);
    return id;
}

void fin_request(int id)
{
    usermap[id].lk.lock();
    usermap[id].reqs++;
    usermap[id].last_req = chrono::steady_clock::now();
    usermap[id].lk.unlock();
}

bool check_brain(int id, const string funame, Response& res)
{
    usermap[id].lk.lock();
    AnnaBrain* ptr = usermap.at(id).brain;
    client_state s = usermap.at(id).state;
    string err = usermap.at(id).last_error;
    usermap[id].lk.unlock();

    if (ptr) return true;

    switch (s) {
    case ANNASERV_CLIENT_CONFIGURED:
    case ANNASERV_CLIENT_UNLOADED:
        // valid request, just need to wait for client unhold
        DBG("Putting request %s from user %d into queue\n",funame.c_str(),id);
        res.status = ServiceUnavailable_503;
        add_queue(id);
        break;

    case ANNASERV_CLIENT_ERROR:
        WARN("Rejecting request from errored-out user %d (reason: %s)\n",id,err.c_str());
        res.status = MethodNotAllowed_405;
        res.body = err;
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

void fix_config(AnnaConfig& cfg)
{
#ifndef SERVER_DEBUG
    cfg.verbose_level = 0;
#endif

    // reset server-controlled fields
    cfg.user = nullptr; // not needed on the server
    cfg.params.n_threads = SERVER_DEF_CPU_THREADS; // hardcoded value for all clients
    cfg.params.n_gpu_layers = 0; // reset GPU offload for now

    // fix model file path
    string fn = cfg.params.model;
    memset(cfg.params.model,0,sizeof(cfg.params.model));
    auto ps = fn.rfind('/');
    if (ps != string::npos) fn.erase(0,ps+1);
    fn = SERVER_MODEL_DIR + string("/") + fn;
    strncpy(cfg.params.model,fn.c_str(),sizeof(cfg.params.model)-1);
}

bool is_workable(int id)
{
    switch (usermap[id].state) {
    case ANNASERV_CLIENT_CONFIGURED:
    case ANNASERV_CLIENT_LOADED:
    case ANNASERV_CLIENT_UNLOADED:
        return true;
    default:
        return false;
    }
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
        usermap[id].gpu_layers = -1;
        usermap[id].reqs = 0;
        usermap[id].started = time(NULL);
        usermap[id].last_req = chrono::steady_clock::now();
        usermap_mtx.unlock();

        INFO("User %d session created\n",id);
    });

    srv->Post("/sessionEnd/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"sessionEnd");
        if (id) del_user(id);
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
        DBG("getState() for user %d: %s\n",id,str.c_str());
        fin_request(id);
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
        DBG("%lu bytes decoded\n",r);
        fix_config(cfg);

        if (!check_brain(id,"setConfig",res)) {
            if (res.status == BadRequest_400) // still a valid request - we just need to initialize the brain
                res.status = OK_200;
            else
                return;
        }

        mod_user(id,cfg);
        DBG("setConfig() complete\n");
        fin_request(id);
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
        DBG("getConfig() complete\n");
        fin_request(id);
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
        DBG("processing(%s) for user %d: %s\n",arg.c_str(),id,str.c_str());
        fin_request(id);
    });

    srv->Get("/getOutput/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getOutput");
        if (!id) return;
        if (!check_brain(id,"processing",res)) return;

        usermap[id].lk.lock();
        AnnaBrain* ptr = usermap.at(id).brain;
        string str = ptr->getOutput();
        usermap[id].iolog.push_back("A"+str);
        usermap[id].lk.unlock();

        res.set_content(to_base64(id,str.data(),str.size()),"text/plain");
        DBG("getOutput() for user %d: %s\n",id,str.c_str());
        fin_request(id);
    });

    srv->Post("/setInput/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setInput");
        if (!id) return;
        if (!check_brain(id,"setInput",res)) return;

        string str = from_base64_str(id,req.body);
        DBG("setInput() for user %d: '%s'\n",id,str.c_str());

        usermap[id].lk.lock();
        usermap[id].iolog.push_back("U"+str);
        usermap.at(id).brain->setInput(str);
        usermap[id].lk.unlock();

        DBG("Brain input set\n");
        fin_request(id);
    });

    srv->Post("/setPrefix/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setPrefix");
        if (!id) return;
        if (!check_brain(id,"setPrefix",res)) return;

        string str = from_base64_str(id,req.body);
        DBG("setPrefix() for user %d: '%s'\n",id,str.c_str());

        usermap[id].lk.lock();
        usermap.at(id).brain->setPrefix(str);
        usermap[id].lk.unlock();
        DBG("Prefix is set\n");
        fin_request(id);
    });

    srv->Get("/getError/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getError");
        if (!id) return;
        if (!check_brain(id,"getError",res)) return;

        usermap[id].lk.lock();
        string str = usermap.at(id).brain->getError();
        usermap[id].lk.unlock();

        res.set_content(to_base64(id,str.data(),str.size()),"text/plain");
        DBG("getError() for user %d: %s\n",id,str.c_str());
        fin_request(id);
    });

    srv->Get("/getTokensUsed/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"getTokensUsed");
        if (!id) return;
        if (!check_brain(id,"getTokensUsed",res)) return;

        usermap[id].lk.lock();
        string str = AnnaBrain::myformat("%d",usermap.at(id).brain->getTokensUsed());
        usermap[id].lk.unlock();

        res.set_content(str,"text/plain");
        DBG("getTokensUsed() for user %d: %s\n",id,str.c_str());
        fin_request(id);
    });

    srv->Post("/reset/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"reset");
        if (!id) return;
        if (!check_brain(id,"reset",res)) return;

        usermap[id].lk.lock();
        usermap.at(id).brain->Reset();
        usermap[id].lk.unlock();
        DBG("Brain is reset\n");
        fin_request(id);
    });

    srv->Post("/undo/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"undo");
        if (!id) return;
        if (!check_brain(id,"undo",res)) return;

        usermap[id].lk.lock();
        usermap.at(id).brain->Undo();
        usermap[id].lk.unlock();
        DBG("Undo done\n");
        fin_request(id);
    });

    srv->Get("/printContext/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"printContext");
        if (!id) return;
        if (!check_brain(id,"printContext",res)) return;

        usermap[id].lk.lock();
        string str = usermap.at(id).brain->PrintContext();
        usermap[id].lk.unlock();

        res.set_content(str,"text/plain");
        DBG("printContext() for user %d: %s\n",id,str.c_str());
        fin_request(id);
    });

    srv->Post("/addEmbeddings/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"addEmbeddings");
        if (!id) return;
        if (!check_brain(id,"addEmbeddings",res)) return;

        string buf(req.body.length(),0);
        size_t r = from_base64(id,(void*)buf.data(),buf.size(),req.body.c_str());
        if (!r || r % sizeof(float)) {
            ERROR("Unable to extract embeddings: %lu bytes decoded\n",r);
            res.status = BadRequest_400;
            return;
        }
        buf.resize(r);
        DBG("%lu bytes decoded for embeddings\n",r);

        vector<float> emb(r / sizeof(float));
        memcpy((void*)emb.data(),buf.data(),r);
        DBG("%lu float records constructed\n",emb.size());

        usermap[id].lk.lock();
        usermap.at(id).brain->addEmbeddings(emb);
        usermap[id].lk.unlock();

        DBG("addEmbeddings() complete\n");
        fin_request(id);
    });

    srv->Get("/checkModel/:id", [](const Request &req, Response &res) {
        int id = check_request(req,res,"checkModel");
        if (!id) return;

        string fn = req.get_param_value("arg");
        if (fn.empty()) {
            ERROR("Empty filename received for checkModel\n");
            res.status = BadRequest_400;
            return;
        }

        string path = AnnaBrain::myformat("%s/%s",SERVER_MODEL_DIR,fn.c_str());
        FILE* f = fopen(path.c_str(),"rb");
        if (f) {
            fclose(f);
            res.set_content("OK","text/plain");
        } else
            res.set_content("File not found","text/plain");

        DBG("checkModel() for user %d: %s\n",id,res.body.c_str());
        fin_request(id);
    });

    srv->Get("/uploadModel/:id/:fn", [](const Request &req, Response &res) {
        int id = check_request(req,res,"uploadModel");
        if (!id) return;

        string fn = req.path_params.at("fn");
        string ssz = req.get_param_value("arg");
        DBG("fn = %s; ssz = %s\n",fn.c_str(),ssz.c_str());
        size_t sz = atoll(ssz.c_str());
        if (fn.empty() || ssz.empty() || !sz) {
            ERROR("Malformed request to uploadModel: '%s','%s'\n",fn.c_str(),ssz.c_str());
            res.status = BadRequest_400;
            return;
        }

        string path = AnnaBrain::myformat("%s/%s",SERVER_MODEL_DIR,fn.c_str());
        FILE* f = fopen(path.c_str(),"wb");
        if (!f) {
            ERROR("Unable to create file %s\n",path.c_str());
            res.set_content("Unable to create file " + path,"text/plain");
            return;
        }

        q_lock.lock();
        hold_user(id);
        usermap[id].lk.lock();
        usermap[id].old_state = usermap[id].state;
        usermap[id].state = ANNASERV_CLIENT_TRANSFER;
        usermap[id].fhandle = f;
        usermap[id].transize = sz;
        usermap[id].lk.unlock();
        q_lock.unlock();

        res.set_content("OK","text/plain");
        DBG("uploadModel() started for user %d\n",id);
        fin_request(id);
    });

    srv->Post("/setChunk/:id/:ci", [](const Request& req, Response& res) {
        int id = check_request(req,res,"setChunk");
        if (!id) return;

        usermap[id].lk.lock();
        client_state s = usermap[id].state;
        FILE* f = usermap[id].fhandle;
        size_t sz = usermap[id].transize;
        usermap[id].lk.unlock();

        if (s != ANNASERV_CLIENT_TRANSFER || !f) {
            ERROR("setChunk() for client %d: Wrong state or file is not open\n",id);
            res.status = BadRequest_400;
            return;
        }

        string sidx = req.path_params.at("ci");
        size_t idx = atoll(sidx.c_str());
        if (idx > sz / SERVER_CLIENT_CHUNK) {
            ERROR("setChunk() for client %d: Chunk index beyond limit (#%lu in %lu bytes file)\n",id,idx,sz);
            res.status = BadRequest_400;
            return;
        }
        if (idx != ftell(f) / SERVER_CLIENT_CHUNK) {
            ERROR("setChunk() for client %d: Chunk is out of order (expected #%lu, got #%lu)\n",id,size_t(ftell(f) / SERVER_CLIENT_CHUNK),idx);
            res.status = BadRequest_400;
            return;
        }

        string buf(SERVER_CLIENT_CHUNK,0);
        size_t r = from_base64(id,(void*)buf.data(),SERVER_CLIENT_CHUNK,req.body.c_str());
        DBG("%lu bytes decoded as chunk data\n",r);

        if (!fwrite(buf.data(),r,1,f)) {
            ERROR("setChunk() for client %d: unable to write chunk #%lu\n",id,idx);
            res.status = InternalServerError_500;
            return;
        }

        DBG("setChunk() complete\n");
        fin_request(id);
    });

    srv->Post("/endTransfer/:id", [](const Request& req, Response& res) {
        int id = check_request(req,res,"endTransfer");
        if (!id) return;

        usermap[id].lk.lock();
        if (usermap[id].fhandle) fclose(usermap[id].fhandle);
        usermap[id].fhandle = NULL;
        usermap[id].lk.unlock();

        q_lock.lock();
        usermap[id].lk.lock();
        client_state s = usermap[id].state;
        if (s == ANNASERV_CLIENT_TRANSFER) usermap[id].state = usermap[id].old_state;
        usermap[id].lk.unlock();
        q_lock.unlock();

        DBG("File transfer complete for client %d\n",id);
        fin_request(id);
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
        int hold = -1, unhold = -1;

        usermap_mtx.lock();
        q_lock.lock();

        // check for active user's validity
        if (active_user > 0) {
            if (usermap.count(active_user) < 1) {
                WARN("Invalid active user %d (not existing), removing...\n",active_user);
                active_user = -1;

            } else if (!is_workable(active_user)) {
                WARN("Current active user %d is in inactive state, switching over...\n",active_user);
                active_user = -1;
            }
        }

        // sanitize the queue
        for (auto qi = userqueue.begin(); qi != userqueue.end();) {
            if (*qi == active_user || usermap.count(*qi) < 1 || !is_workable(*qi))
                qi = userqueue.erase(qi);
            else
                ++qi;
        }

        // check for active user's inactivity timeout
        if (active_user > 0) {
            float age = (float)((chrono::steady_clock::now() - usermap.at(active_user).last_req) / 1ms) / 1000.f;
            if (userqueue.empty()) age = 0; // no need to switch, as there's no more requests atm
            if (usermap[active_user].lk.try_lock()) usermap[active_user].lk.unlock(); // just test the lock
            else age = 0; // if locked - something's happening, so we can't release this client yet

            if (age > SERVER_CLIENT_TIMEOUT) {
                INFO("User %d inactivity timeout\n",active_user);
                hold = active_user;
            }
        }

        // check for queue waiting time
        if (active_user > 0 && !userqueue.empty()) {
            int next = userqueue.front(); // oldest request will be the first
            if (usermap.count(next)) {
                int age = (chrono::steady_clock::now() - usermap.at(next).last_req) / 1s;
                // if another client waited too long, then remove the current client
                if (age > SERVER_CLIENT_MAXTIME) {
                    INFO("User %d waited too long, forcing suspend of user %d\n",next,active_user);
                    hold = active_user;
                }
            }
        }

        // check for the queue
        if (active_user < 0 && !userqueue.empty()) {
            unhold = userqueue.front();
            userqueue.pop_front();
            // check if it's still valid
            if (usermap.count(unhold) < 1)
                unhold = -1;
        }

        q_lock.unlock();
        usermap_mtx.unlock();

        // hold a user
        if (hold > 0) {
            // put the client on hold and reset active user
            hold_user(hold);
            q_lock.lock();
            active_user = -1;
            q_lock.unlock();
        }

        // unhold a user
        if (unhold > 0) {
            if (!unhold_user(unhold)) { // and resume it
                ERROR("Unable to resume session for user %d\n",unhold);
                unhold = -1;
            }
            q_lock.lock();
            active_user = unhold;
            q_lock.unlock();
        }

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
    printf("\nANNA Server version " SERVER_VERSION " starting up\n");
    printf("\nANNA Brain version " ANNA_VERSION "\n\n");
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
