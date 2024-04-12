/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#ifndef ARIA_BINDS_H
#define ARIA_BINDS_H

#define LBIND(X,N) lua_pushcfunction(luavm,X); lua_setglobal(luavm,N);
#define LFUNC(N,X) static int N(lua_State* L) { \
        lua_getglobal(L,"thisptr"); \
        Aria* pb = (Aria*)lua_touserdata(L,-1); \
        if (pb) return (pb->X()); \
        else return 0; /* we did nothing, so no values pushed */ \
        }

#endif // ARIA_BINDS_H

#ifdef ARIA_BINDS_FUNCTIONS

LFUNC(bind_GetVersion,scriptGetVersion)
LFUNC(bind_PrintOut,scriptPrintOut)
LFUNC(bind_GetInput,scriptGetInput)
LFUNC(bind_GetName,scriptGetName)
LFUNC(bind_SetIOCount,scriptSetIOCount)

#endif // ARIA_BINDS_FUNCTIONS

#ifdef ARIA_BINDS_NAMES

LBIND(bind_GetVersion,"getversion");
LBIND(bind_PrintOut,"printout");
LBIND(bind_GetInput,"getinput");
LBIND(bind_GetName,"getname");
LBIND(bind_SetIOCount,"setiocount");

#endif // ARIA_BINDS_NAMES
