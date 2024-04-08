#pragma once

#include "brain.h"
#include "lua.hpp"

#define ARIA_VERSION "0.0.1"

class Aria
{
public:
    Aria();
    virtual ~Aria();

private:
    lua_State* vm = nullptr;
};
