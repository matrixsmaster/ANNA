/* This code uses parts of the ARIA library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
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
    if (!StartVM()) return;
    state = ARIA_LOADED;
}

Aria::~Aria()
{
    if (luavm) lua_close(luavm);
    if (brain) delete brain;
}

bool Aria::setInput(std::string in)
{
    last_input = in;
    return true;
}

string Aria::getOutput()
{
    string tmp = output;
    output.clear();
    return tmp;
}

AriaState Aria::Processing()
{
    if (!luavm) return ARIA_NOT_INITIALIZED;
    if (!LuaCall("processing",nullptr)) state = ARIA_ERROR;
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
    lua_pushstring(R,last_input.c_str());
    return 1;
}

int Aria::scriptCreateBrain()
{
    return 0;
}

int Aria::scriptDeleteBrain()
{
    return 0;
}
