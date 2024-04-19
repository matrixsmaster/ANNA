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
LFUNC(bind_GetUserImage,scriptGetUserImage)
LFUNC(bind_GetName,scriptGetName)
LFUNC(bind_SetIOCount,scriptSetIOCount)
LFUNC(bind_BrainStart,scriptBrainStart)
LFUNC(bind_BrainStop,scriptBrainStop)
LFUNC(bind_BrainState,scriptBrainState)
LFUNC(bind_BrainIn,scriptBrainIn)
LFUNC(bind_BrainOut,scriptBrainOut)
LFUNC(bind_BrainPrefix,scriptBrainPrefix)
LFUNC(bind_BrainProcess,scriptBrainProcess)
LFUNC(bind_BrainSetVEnc,scriptBrainSetVEnc)
LFUNC(bind_BrainLoadImage,scriptBrainLoadImage)
LFUNC(bind_BrainError,scriptBrainError)

#endif // ARIA_BINDS_FUNCTIONS

#ifdef ARIA_BINDS_NAMES

LBIND(bind_GetVersion,"getversion");
LBIND(bind_PrintOut,"printout");
LBIND(bind_GetInput,"getinput");
LBIND(bind_GetUserImage,"getuserimage");
LBIND(bind_GetName,"getname");
LBIND(bind_SetIOCount,"setiocount");
LBIND(bind_BrainStart,"brainstart");
LBIND(bind_BrainStop,"brainstop");
LBIND(bind_BrainState,"brainstate");
LBIND(bind_BrainIn,"brainin");
LBIND(bind_BrainOut,"brainout");
LBIND(bind_BrainPrefix,"brainprefix");
LBIND(bind_BrainProcess,"brainprocess");
LBIND(bind_BrainSetVEnc,"brainsetvenc");
LBIND(bind_BrainLoadImage,"brainloadimage");
LBIND(bind_BrainError,"brainerror");

#endif // ARIA_BINDS_NAMES
