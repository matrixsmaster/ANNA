/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#pragma once

#include <thread>
#include <atomic>
#include "brain.h"
#include "lua.hpp"

#define ARIA_VERSION "0.0.3"

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

class Aria
{
public:
    Aria(std::string scriptfile, std::string name);
    virtual ~Aria();

    AriaState getState()            const   { return state; }
    std::string getError()          const   { return merror; }
    int getNumInPins()              const   { return pins; }
    int getNumOutPins()             const   { return pouts; }

    bool setGlobalInput(std::string in);
    std::string getGlobalOutput();

    void setInPin(int pin, std::string str);
    std::string getOutPin(int pin);

    AriaState Processing();

    int scriptGetVersion();
    int scriptPrintOut();
    int scriptGetInput();
    int scriptCreateBrain();
    int scriptDeleteBrain();

private:
    lua_State* luavm = nullptr;
    AnnaBrain* brain = nullptr;
    AriaState state = ARIA_NOT_INITIALIZED;
    std::thread* process_thr = nullptr;
    std::atomic<AriaThreadSem> thr_state;
    std::string scriptfn, mname;
    std::string merror;
    std::string output;
    std::string input, last_input;
    int pins = 0;
    int pouts = 0;

    bool StartVM();
    void ErrorVM();
    bool LuaCall(std::string f, const char* args, ...);
    void StopProcessing();
};
