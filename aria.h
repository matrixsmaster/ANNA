/* This code uses parts of the SGUI library
 * (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2025 */
#pragma once

#include <thread>
#include <atomic>
#include "brain.h"
#include "lua.hpp"

#define ARIA_VERSION "0.0.11"

#define ARIA_PATH_DELIM '/'

enum AriaState {
    ARIA_NOT_INITIALIZED,
    ARIA_ERROR,
    //ARIA_LOADED,
    ARIA_READY,
    ARIA_RUNNING,
    ARIA_NUMSTATES
};

enum AriaThreadSem {
    ARIA_THR_NOT_RUNNING = 0,
    ARIA_THR_RUNNING,
    ARIA_THR_STOPPED,
    ARIA_THR_FORCE_STOP
};

// Artificially Restricted Intelligent Agent
class Aria
{
public:
    Aria(std::string scriptfile, std::string name);
    virtual ~Aria();

    void Close();
    bool Reload();

    AriaState getState()            const   { return state; }
    std::string getError()          const   { return merror; }
    std::string getFName()          const   { return scriptfn; }
    std::string getName()           const   { return mname; }
    int getNumInPins()              const   { return pins; }
    int getNumOutPins()             const   { return pouts; }

    static std::string FixPath(std::string parent, std::string fn);
    static std::string MakeRelativePath(std::string parent, std::string fn);

    bool setGlobalInput(std::string in);
    std::string getGlobalOutput();

    bool setUserImage(std::string fn);

    void setInPin(int pin, std::string str);
    std::string getOutPin(int pin);

    AriaState Processing();

    int scriptGetVersion();
    int scriptPrintOut();
    int scriptGetInput();
    int scriptGetUserImage();
    int scriptGetName();
    int scriptSetIOCount();
    int scriptBrainStart();
    int scriptBrainStop();
    int scriptBrainState();
    int scriptBrainIn();
    int scriptBrainOut();
    int scriptBrainPrefix();
    int scriptBrainProcess();
    int scriptBrainSetVEnc();
    int scriptBrainLoadImage();
    int scriptBrainError();

private:
    lua_State* luavm = nullptr;
    AnnaBrain* brain = nullptr;
    AnnaConfig bconfig;
    AriaState state = ARIA_NOT_INITIALIZED;
    std::thread* process_thr = nullptr;
    std::atomic<AriaThreadSem> thr_state;
    std::string scriptfn, mname;
    std::string merror;
    std::string output;
    std::string input, last_input, usrimage;
    int pins = 0;
    int pouts = 0;

    bool StartVM();
    void ErrorVM();
    bool LuaCall(std::string f, const char* args, ...);
    std::string LuaGetString();
    void StopProcessing();
};
