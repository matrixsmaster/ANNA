/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#pragma once

#include "brain.h"
#include "lua.hpp"

#define ARIA_VERSION "0.0.1"

enum AriaState {
    ARIA_NOT_INITIALIZED,
    ARIA_ERROR,
    ARIA_LOADED,
    ARIA_RUNNING,
    ARIA_NUMSTATES
};

class Aria
{
public:
    Aria(std::string scriptfile);
    virtual ~Aria();

    AriaState getState();
    std::string getError();

    int scriptGetVersion();
    int scriptPrintOut();
    int scriptCreateBrain();
    int scriptDeleteBrain();

private:
    lua_State* luavm = nullptr;
    AnnaBrain* brain = nullptr;
    AriaState state = ARIA_NOT_INITIALIZED;
    std::string scriptfn;
    std::string merror;

    bool StartVM();
};