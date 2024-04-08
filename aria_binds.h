/* This code uses parts of the SGUI library (C) Dmitry 'MatrixS_Master' Solovyev, 2016-2024 */
#ifndef ARIA_BINDS_H
#define ARIA_BINDS_H

#define LBIND(X,N) lua_pushcfunction(luavm,X); lua_setglobal(luavm,N);
#define LFUNC(N,X) static int N(lua_State* L) { \
        SGUI* pb; \
        lua_getglobal(L,"thisptr"); \
        pb = (SGUI*)lua_touserdata(L,-1); \
        if (pb) return (pb->X()); \
        else return 0; /* we did nothing, so no values pushed */ \
        }

#endif // ARIA_BINDS_H

#ifdef ARIA_BINDS_FUNCTIONS

LFUNC(bind_GetVersion,scriptGetVersion)

#endif // ARIA_BINDS_FUNCTIONS

#ifdef ARIA_BINDS_NAMES

LBIND(bind_GetVersion,"getversion");

#endif // ARIA_BINDS_NAMES
