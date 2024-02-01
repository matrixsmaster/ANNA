#include "netclient.h"
#include "httplib.h"

//#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
//#else
//#define DBG(...)
//#endif

AnnaClient::AnnaClient(AnnaConfig* cfg) : AnnaBrain(nullptr)
{
    DBG("client c'tor\n");
}

AnnaClient::~AnnaClient()
{
    DBG("client d'tor\n");
    //AnnaBrain::~AnnaBrain();
}

AnnaState AnnaClient::getState()
{
    DBG("getState()\n");
    return ANNA_ERROR;
}
