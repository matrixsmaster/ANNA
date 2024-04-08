#include "aria.h"

Aria::Aria()
{
    //...
}

Aria::~Aria()
{
    if (vm) lua_close(vm);
    vm = nullptr;
}
