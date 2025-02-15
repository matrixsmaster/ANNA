/* This code uses parts of the SGUI library
 * (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2025 */
#include <string.h>
#include "brain.h"
#include "netclient.h"
#include "aria.h"
#include "libfme.h"

#define ERR(X,...) fprintf(stderr, "[ARIA] ERROR: " X "\n", __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[ARIA DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

#define ARIA_BIND_GETVM (lua_isthread(luavm,1))? lua_tothread(luavm,1) : luavm
#define ARIA_BIND_STACK_EXTRAS 1
#define ARIA_BIND_CHECK_NUM_ARGS(N,V,X) if (lua_gettop(V) - ARIA_BIND_STACK_EXTRAS < X) { \
    return luaL_error(luavm, #N ": %d arguments expected, got %d", X, (lua_gettop(V) - ARIA_BIND_STACK_EXTRAS)); \
    }
#define ARIA_BIND_HEADER(N,X) lua_State* R = ARIA_BIND_GETVM; \
    ARIA_BIND_CHECK_NUM_ARGS(N,R,X);

using namespace std;

#define ARIA_BINDS_FUNCTIONS
#include "aria_binds.h"
#undef ARIA_BINDS_FUNCTIONS

Aria::Aria(string scriptfile, string name)
{
    scriptfn = scriptfile;
    mname = name;
    thr_state = ARIA_THR_NOT_RUNNING;
    Reload();
}

Aria::~Aria()
{
    Close();
}

void Aria::Close()
{
    StopProcessing();
    if (luavm) lua_close(luavm);
    if (brain) delete brain;
}

bool Aria::Reload()
{
    Close();
    pins = pouts = 0;
    name_ins.clear();
    name_outs.clear();

    clock_gettime(CLOCK_MONOTONIC,&start_time);
    bool r = StartVM();
    state = r? ARIA_READY : ARIA_ERROR;
    return r;
}

string Aria::getPinName(bool input, int num)
{
    if (num < 0 || (input && num >= (int)name_ins.size()) || (!input && num >= (int)name_outs.size())) return "";
    return input? name_ins.at(num) : name_outs.at(num);
}

string Aria::FixPath(string parent, string fn)
{
    const char delim = ARIA_PATH_DELIM;
#ifdef _WIN32
    // in mingw the paths will be POSIX-style, but we might still need to fix user-supplied paths
    for (auto &i : parent) i = (i == '\\')? delim : i;
    for (auto &i : fn) i = (i == '\\')? delim : i;
    if (!(fn.length() > 3 && fn.at(1) == ':' && fn.at(2) == delim)) {
#else
    if (fn.at(0) != delim) { // don't do anything to an absolute path
#endif
        auto pos = parent.rfind(delim);
        if (pos != string::npos) {
            fn = parent.substr(0,pos) + delim + fn;
        }
    }
    return fn;
}

string Aria::MakeRelativePath(string parent, string fn)
{
    const char delim = ARIA_PATH_DELIM;
    string norm = FixPath(parent,fn); // make sure it's absolute first
#ifdef _WIN32
    for (auto &i : parent) i = (i == '\\')? delim : i;
#endif

    unsigned stop = 0;
    for (unsigned i = 0; i < std::min(norm.length(),parent.length()); i++) {
        if (norm[i] != parent[i]) {
            if (!i || !stop) break;
            norm.erase(0,stop+1);
            return norm;

        } else if (norm[i] == delim)
            stop = i;
    }
    return fn; // unchanged
}

bool Aria::setGlobalInput(string in)
{
    input = in;
    return true;
}

string Aria::getGlobalOutput()
{
    string tmp = output;
    output.clear();
    return tmp;
}

bool Aria::setUserImage(string fn)
{
    usrimage = fn;
    return true;
}

void Aria::setName(string name)
{
    if (name.empty()) return;
    mname = name;
    Reload();
}

void Aria::setInPin(int pin, string str)
{
    if (pin < 0 || pin >= pins) return;
    // call by name (if exists), otherwise call by number
    if (pin < (int)name_ins.size())
        LuaCall("inpin","ss",name_ins.at(pin).c_str(),str.c_str());
    else
        LuaCall("inpin","is",pin,str.c_str());
}

string Aria::getOutPin(int pin)
{
    if (pin < 0 || pin >= pouts) return "";

    if (pin < (int)name_outs.size()) {
        if (!LuaCall("outpin","s",name_outs.at(pin).c_str())) return "";
    } else {
        if (!LuaCall("outpin","i",pin)) return "";
    }

    string out = LuaGetString();
    if (!out.empty()) last_outputs[pin] = out;
    return out;
}

string Aria::getLastOutPin(int pin)
{
    if (pin < 0 || pin >= pouts) return "";
    return last_outputs[pin];
}

AriaState Aria::Processing()
{
    if (!luavm) return ARIA_NOT_INITIALIZED;

    switch (state) {
    case ARIA_READY:
        if (process_thr) StopProcessing();
        state = ARIA_RUNNING;
        thr_state = ARIA_THR_NOT_RUNNING;
        process_thr = new std::thread([&] {
            thr_state = ARIA_THR_RUNNING;
            if (!LuaCall("processing",nullptr)) state = ARIA_ERROR;
            thr_state = ARIA_THR_STOPPED;
        });
        break;

    case ARIA_RUNNING:
        if (thr_state == ARIA_THR_STOPPED) {
            state = ARIA_READY;
            StopProcessing();
        }
        break;

    default:
        break;
    }

    return state;
}

bool Aria::StartVM()
{
    //create a VM
    luavm = luaL_newstate();
    luaL_openlibs(luavm);

    //assign global variables
    lua_pushlightuserdata(luavm,this);
    lua_setglobal(luavm,"thisptr");

    //assign functions
    #define ARIA_BINDS_NAMES
    #include "aria_binds.h"
    #undef ARIA_BINDS_NAMES

    //load the script file
    int r = luaL_loadfilex(luavm,scriptfn.c_str(),NULL);
    if (r != LUA_OK) {
        ErrorVM();
        return false;
    }

    //run init chunk
    if (lua_pcall(luavm,0,0,0) != LUA_OK) {
        ErrorVM();
        return false;
    }
    return true;
}

void Aria::ErrorVM()
{
    string err = lua_tostring(luavm,-1);

    if (!err.empty()) {
        merror = "Lua Error: " + err;
        state = ARIA_ERROR;
    }
    lua_pop(luavm,1);
}

bool Aria::LuaCall(string f, const char* args, ...)
{
    if (!luavm) return false;

    va_list vl;
    int num,r;
    bool res;

    //push function to be called
    lua_getglobal(luavm,f.c_str());
    num = 0;

    if (lua_isnil(luavm,-(num+1))) {
        lua_remove(luavm,-(num+1)); //remove nil
        merror = AnnaBrain::myformat("function %s doesn't exist in Aria script %s",f.c_str(),scriptfn.c_str());
        return false;
    }

    //process and push all arguments (if any)
    va_start(vl,args);
    while (args && *args) {
        switch (*args) {
        case 'i':
        {
            int i = va_arg(vl,int);
            DBG("pushing integer %d\n",i);
            lua_pushinteger(luavm,i);
            num++;
            break;
        }
        case 'f':
        case 'd':
        {
            double vf = va_arg(vl,double);
            DBG("pushing float (number) %f\n",vf);
            lua_pushnumber(luavm,vf);
            num++;
            break;
        }
        case 's':
        {
            const char* str = va_arg(vl,const char*);
            DBG("pushing string %s\n",str);
            lua_pushstring(luavm,str);
            num++;
            break;
        }
        default:
            DBG("unknown formatting literal '%c', ignored\n",*args);
        }
        args++;
    }
    va_end(vl);

    //now call the target
    r = lua_pcall(luavm,num,1,0);
    DBG("pod %s called %s, result = %d\n",scriptfn.c_str(),f.c_str(),r);
    res = true;

    //check the results
    if (r != LUA_OK) { //in case of errors, remove values from stack
        res = false;
        ErrorVM();
    }

    return res;
}

string Aria::LuaGetString()
{
    if (!luavm) return "";
    if (!lua_isstring(luavm,-1)) return "";
    string r = lua_tostring(luavm,-1);
    DBG("string returned: '%s'\n",r.c_str());
    return r;
}

std::vector<string> Aria::LuaGetStringList(int idx)
{
    std::vector<string> res;
    if (lua_isnoneornil(luavm,idx)) return res;

    luaL_checktype(luavm,idx,LUA_TTABLE);

    int n = luaL_len(luavm,idx);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(luavm,idx,i);
        const char* str = lua_tostring(luavm,-1);
        if (str) res.push_back(str);
        lua_pop(luavm,1);
    }
    return res;
}

void Aria::StopProcessing()
{
    if (process_thr) {
        switch (thr_state) {
        case ARIA_THR_RUNNING:
            thr_state = ARIA_THR_FORCE_STOP;
            // fall-through
        case ARIA_THR_STOPPED:
        case ARIA_THR_FORCE_STOP:
            process_thr->join();
            break;
        case ARIA_THR_NOT_RUNNING:
            break;
        }
        if (process_thr) delete process_thr;
        process_thr = nullptr;
    }
    thr_state = ARIA_THR_NOT_RUNNING;
}

int Aria::scriptGetVersion()
{
    ARIA_BIND_HEADER("getversion",0);
    lua_pushstring(R,ARIA_VERSION);
    return 1;
}

int Aria::scriptPrintOut()
{
    ARIA_BIND_HEADER("printout",1);
    const char* str = luaL_checkstring(R,1);
    output += str;
    output += "\n";
    return 0;
}

int Aria::scriptGetInput()
{
    ARIA_BIND_HEADER("getinput",0);
    last_input = input;
    input.clear();
    lua_pushstring(R,last_input.c_str());
    return 1;
}

int Aria::scriptGetUserImage()
{
    ARIA_BIND_HEADER("getuserimage",0);
    lua_pushstring(R,usrimage.c_str());
    usrimage.clear();
    return 1;
}

int Aria::scriptGetName()
{
    ARIA_BIND_HEADER("getinput",0);
    lua_pushstring(R,mname.c_str());
    return 1;
}

int Aria::scriptSetIOCount()
{
    ARIA_BIND_HEADER("setiocount",2);
    pins = luaL_checknumber(R,1);
    pouts = luaL_checknumber(R,2);
    last_outputs.resize(pouts);
    return 0;
}

int Aria::scriptSetIONames()
{
    ARIA_BIND_HEADER("setionames",2);
    name_ins = LuaGetStringList(1);
    name_outs = LuaGetStringList(2);
    return 0;
}

int Aria::scriptBrainStart()
{
    ARIA_BIND_HEADER("brainstart",2);
    string mod = luaL_checkstring(R,1);
    string srv = luaL_checkstring(R,2);

    mod = FixPath(scriptfn,mod);
    strncpy(bconfig.params.model,mod.c_str(),sizeof(bconfig.params.model)-1);

    if (brain) {
        ERR("Brain has already been created, refusing to create another one for model %s\n",mod.c_str());
        lua_pushboolean(R,false);

    } else {
        if (srv.empty()) {
            // normal offline instance
            brain = new AnnaBrain(&bconfig);
            DBG("Normal brain created\n");

        } else {
            // offloaded instance
            brain = new AnnaClient(&bconfig,srv,false,nullptr);
            DBG("Net client created\n");
        }

        lua_pushboolean(R,(brain->getState() == ANNA_READY));
    }
    return 1;
}

int Aria::scriptBrainStop()
{
    ARIA_BIND_HEADER("brainstop",0);
    if (brain) delete brain;
    brain = nullptr;
    DBG("Brain destroyed\n");
    return 0;
}

int Aria::scriptBrainState()
{
    ARIA_BIND_HEADER("brainstate",0);
    string s = brain? AnnaBrain::StateToStr(brain->getState()) : "not loaded";
    lua_pushstring(R,s.c_str());
    return 1;
}

int Aria::scriptBrainCThreads()
{
    ARIA_BIND_HEADER("braincthreads",1);
    int thr = luaL_checkinteger(R,1);
    if (thr > 0) {
        bconfig.params.n_threads = thr;
        bconfig.params.n_threads_batch = thr;
        DBG("Threads limit set to %d\n",thr);
    }
    return 0;
}

int Aria::scriptBrainCContext()
{
    ARIA_BIND_HEADER("brainccontext",1);
    int nctx = luaL_checkinteger(R,1);
    if (nctx > 0) {
        bconfig.params.n_ctx = nctx;
        DBG("Context length set to %d\n",nctx);
    }
    return 0;
}

int Aria::scriptBrainCGroupAtt()
{
    ARIA_BIND_HEADER("braincgroupatt",2);
    int fac = luaL_checkinteger(R,1);
    int width = luaL_checkinteger(R,2);
    if (fac > 0) {
        DBG("Setting group attention to factor %d, width %d\n",fac,width);
        bconfig.params.grp_attn_n = fac;
        bconfig.params.grp_attn_w = width;
    } else {
        DBG("Setting widening to RoPE scaling\n");
        bconfig.params.grp_attn_n = 0;
    }
    return 0;
}

int Aria::scriptBrainCSampling()
{
    ARIA_BIND_HEADER("braincsampling",1);
    string dsc = luaL_checkstring(R,1);
    if (dsc.empty()) return 0;
    const char* loc = setlocale(LC_NUMERIC,"C"); // stupid locale conversions of decimal separators!!! IT'S DOT. PERIOD. F OFF!

    if (dsc.starts_with("greedy;")) {
        DBG("Setting greedy sampling\n");
        bconfig.params.sparams.temp = -1;

    } else if (dsc.starts_with("topp;")) {
        dsc.erase(0,5);
        float temp,topp,topk,tfz,typp;
        int arg = sscanf(dsc.c_str(),"%f;%f;%f;%f;%f;",&temp,&topp,&topk,&tfz,&typp);
        if (arg < 5) {
            ERR("Not enough arguments: %d received\n",arg);
            goto csamp_end;
        }
        DBG("Setting Top P sampling with temp %.2f, topp %.2f, topk %.2f, tfz %.2f, typp %.2f\n",temp,topp,topk,tfz,typp);
        bconfig.params.sparams.temp = temp;
        bconfig.params.sparams.top_p = topp;
        bconfig.params.sparams.top_k = topk;
        bconfig.params.sparams.tfs_z = tfz;
        bconfig.params.sparams.typical_p = typp;

    } else if (dsc.starts_with("miro2;")) {
        dsc.erase(0,6);
        float temp,rate,ent;
        int arg = sscanf(dsc.c_str(),"%f;%f;%f;",&temp,&rate,&ent);
        if (arg < 3) {
            ERR("Not enough arguments: %d received\n",arg);
            goto csamp_end;
        }
        DBG("Setting Mirostat V2 sampling with temp %.2f, rate %.3f, entropy %.2f\n",temp,rate,ent);
        bconfig.params.sparams.temp = temp;
        bconfig.params.sparams.mirostat = 2;
        bconfig.params.sparams.mirostat_eta = rate;
        bconfig.params.sparams.mirostat_tau = ent;
    }

csamp_end:
    if (loc) setlocale(LC_NUMERIC,loc);
    return 0;
}

int Aria::scriptBrainLoad()
{
    ARIA_BIND_HEADER("brainload",1);
    string fn = luaL_checkstring(R,1);
    if (!brain) lua_pushboolean(R,0);
    else {
        DBG("Attempting to load brain state from %s\n",fn.c_str());
        lua_pushboolean(R,brain->LoadState(fn,nullptr,nullptr));
    }
    return 1;
}

int Aria::scriptBrainSave()
{
    ARIA_BIND_HEADER("brainsave",1);
    string fn = luaL_checkstring(R,1);
    if (!brain) lua_pushboolean(R,0);
    else {
        DBG("Attempting to save brain state to %s\n",fn.c_str());
        lua_pushboolean(R,brain->SaveState(fn,nullptr,0));
    }
    return 1;
}

int Aria::scriptBrainIn()
{
    ARIA_BIND_HEADER("brainin",1);
    string in = luaL_checkstring(R,1);
    if (brain) brain->setInput(in);
    return 0;
}

int Aria::scriptBrainOut()
{
    ARIA_BIND_HEADER("brainout",0);
    string r;
    if (brain) r = brain->getOutput();
    lua_pushstring(R,r.c_str());
    return 1;
}

int Aria::scriptBrainPrefix()
{
    ARIA_BIND_HEADER("brainprefix",1);
    string in = luaL_checkstring(R,1);
    if (brain) brain->setPrefix(in);
    return 0;
}

int Aria::scriptBrainProcess()
{
    ARIA_BIND_HEADER("brainprocess",1);
    bool skip = lua_toboolean(R,1);
    // we'll return true in case of errors to prevent potential infinite loops in scripts
    if (brain) {
        AnnaState s = brain->Processing(skip);
        lua_pushboolean(R,(s != ANNA_PROCESSING));
    } else
        lua_pushboolean(R,true);
    return 1;
}

int Aria::scriptBrainSetVEnc()
{
    ARIA_BIND_HEADER("brainsetvenc",1);
    string fn = luaL_checkstring(R,1);
    if (!fn.empty() && brain) {
        fn = FixPath(scriptfn,fn);
        brain->setClipModelFile(fn);
    }
    return 0;
}

int Aria::scriptBrainLoadImage()
{
    ARIA_BIND_HEADER("brainloadimage",1);
    string fn = luaL_checkstring(R,1);
    bool r = false;
    if (!fn.empty() && brain) {
        fn = FixPath(scriptfn,fn);
        r = brain->EmbedImage(fn);
    }
    lua_pushboolean(R,r);
    return 1;
}

int Aria::scriptBrainError()
{
    ARIA_BIND_HEADER("brainerror",0);
    string err = brain? brain->getError() : "brain doesn't exist";
    lua_pushstring(R,err.c_str());
    return 1;
}

int Aria::scriptMillis()
{
    ARIA_BIND_HEADER("millis",0);
    timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    int ms = floor((double)(now.tv_sec - start_time.tv_sec) * 1e3 + ((double)(now.tv_nsec - start_time.tv_nsec) / 1e6));
    lua_pushinteger(R,ms);
    return 1;
}

int Aria::scriptFmeCheck()
{
    ARIA_BIND_HEADER("fmecheck",1);
    lua_pushboolean(R,fme_check_msg(luaL_checkstring(R,1)));
    return 1;
}

int Aria::scriptFmeReceive()
{
    ARIA_BIND_HEADER("fmereceive",2);
    string msg;
    int len = luaL_checkinteger(R,2);
    msg.resize(len,0);
    len = fme_receive_msg(luaL_checkstring(R,1),msg.data(),len);
    msg.resize(len);
    lua_pushstring(R,msg.c_str());
    return 1;
}

int Aria::scriptFmeSend()
{
    ARIA_BIND_HEADER("fmesend",2);
    string msg = luaL_checkstring(R,2);
    lua_pushboolean(R,fme_send_msg(luaL_checkstring(R,1),msg.c_str(),msg.length(),luaL_checkinteger(R,3)));
    return 1;
}

int Aria::scriptScriptDir()
{
    ARIA_BIND_HEADER("scriptdir",0);
    string dir = scriptfn;
    auto pos = dir.find_last_of(ARIA_PATH_DELIM);
    if (pos == string::npos) dir = ".";
    else dir.erase(pos);
    lua_pushstring(R,dir.c_str());
    return 1;
}
