/* This code uses parts of the ARIA library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#include <string.h>
#include "brain.h"
#include "netclient.h"
#include "aria.h"

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

Aria::Aria(string scriptfile, std::string name)
{
    scriptfn = scriptfile;
    mname = name;
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
    bool r = StartVM();
    state = r? ARIA_READY : ARIA_ERROR;
    return r;
}

string Aria::FixPath(string parent, string fn)
{
    char delim = '/';
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

bool Aria::setGlobalInput(std::string in)
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

void Aria::setInPin(int pin, std::string str)
{
    if (pin < 0 || pin >= pins) return;
    LuaCall("inpin","is",pin,str.c_str());
}

string Aria::getOutPin(int pin)
{
    if (pin < 0 || pin >= pouts) return "";
    if (LuaCall("outpin","i",pin)) return LuaGetString();
    return "";
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
        merror = AnnaBrain::myformat("failed to load and compile chunk from file %s (error %d)",scriptfn.c_str(),r);
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

bool Aria::LuaCall(std::string f, const char* args, ...)
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
            DBG("pushing integer %d",i);
            lua_pushinteger(luavm,i);
            num++;
            break;
        }
        case 'f':
        case 'd':
        {
            double vf = va_arg(vl,double);
            DBG("pushing float (number) %f",vf);
            lua_pushnumber(luavm,vf);
            num++;
            break;
        }
        case 's':
        {
            const char* str = va_arg(vl,const char*);
            DBG("pushing string %s",str);
            lua_pushstring(luavm,str);
            num++;
            break;
        }
        default:
            DBG("unknown formatting literal '%c', ignored",*args);
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
    return 0;
}

int Aria::scriptBrainStart()
{
    ARIA_BIND_HEADER("brainstart",2);
    string mod = luaL_checkstring(R,1);
    string srv = luaL_checkstring(R,2);

    mod = FixPath(scriptfn,mod);
    strncpy(bconfig.params.model,mod.c_str(),sizeof(bconfig.params.model));

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
    return 0;
}

int Aria::scriptBrainIn()
{
    ARIA_BIND_HEADER("brainin",1);
    string in = luaL_checkstring(R,1);
    if (brain && !in.empty()) brain->setInput(in);
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
