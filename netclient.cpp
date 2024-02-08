#include "netclient.h"
#include "../server/httplib.h"
#include "../server/base64m.h"

//#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
//#else
//#define DBG(...)
//#endif

using namespace std;
using namespace httplib;

AnnaClient::AnnaClient(AnnaConfig* cfg, string server) : AnnaBrain(nullptr)
{
    DBG("client c'tor\n");
    auto rng = std::mt19937(time(NULL));
    clid = (uint32_t)rng() & ANNA_CLIENT_MASK;
    DBG("clid = %u (0x%08X)\n",clid,clid);

    state = ANNA_NOT_INITIALIZED;
    if (!cfg) return;

    client = new Client(server);
    if (!client->is_valid()) {
        state = ANNA_ERROR;
        internal_error = myformat("Server '%s' is not valid",server.c_str());
        return;
    }
    config = *cfg;

    state = ANNA_READY;
    if (!command("/sessionStart")) {
        //state = ANNA_ERROR;
        internal_error = "Unable to create remote session!";
        //return;
    }

    AnnaClient::setConfig(config);
}

AnnaClient::~AnnaClient()
{
    DBG("client d'tor\n");
    command("/sessionEnd");
}

AnnaState AnnaClient::getState()
{
    if (state != ANNA_READY) return state; // internal state
    int r = atoi(request("/getState").c_str());
    if (state == ANNA_ERROR) return state; // request failed
    return (r < 0 || r >= ANNA_NUM_STATES)? ANNA_ERROR : (AnnaState)r;
}

const string &AnnaClient::getError()
{
    if (state != ANNA_READY) return internal_error;
    internal_error = request("/getError");
    return internal_error;
}

int AnnaClient::getTokensUsed()
{
    return atoi(request("/getTokensUsed").c_str());
}

AnnaConfig AnnaClient::getConfig()
{
    // TODO: request
    return config;
}

void AnnaClient::setConfig(const AnnaConfig &cfg)
{
    DBG("sizeof gpt_params = %lu\n",sizeof(gpt_params));
    string enc = asBase64((void*)&(cfg.params),sizeof(cfg));
    DBG("encoded state len = %lu\n",enc.size());
    command("/setConfig",enc);
}

string AnnaClient::getOutput()
{
    return request("/getOutput");
}

void AnnaClient::setInput(string inp)
{
    command("/setInput",inp);
}

void AnnaClient::setPrefix(string str)
{
    command("/setPrefix",str);
}

//const char *AnnaClient::TokenToStr(llama_token token)
//{
//}

string AnnaClient::PrintContext()
{
    return request("/printContext");
}

bool AnnaClient::SaveState(string fname)
{
    string r = request("/saveState",fname);
    return (r == "success");
}

bool AnnaClient::LoadState(string fname)
{
    string r = request("/loadState",fname);
    return (r == "success");
}

void AnnaClient::setClipModelFile(string fn)
{
    command("/setClipModelFile",fn);
}

bool AnnaClient::EmbedImage(string imgfile)
{
    // TODO: not supported atm
    return false;
}

AnnaState AnnaClient::Processing(bool skip_sampling)
{
    int r = atoi(request("/processing",skip_sampling? "skip":"noskip").c_str());
    return (r < 0 || r >= ANNA_NUM_STATES)? ANNA_ERROR : (AnnaState)r;
}

void AnnaClient::Reset()
{
    command("/reset");
}

void AnnaClient::Undo()
{
    command("/undo");
}

string AnnaClient::asBase64(void *data, int len)
{
    string s;
    s.resize(len*2);
    size_t n = encode((char*)s.data(),s.size(),data,len);
    s.resize(n);
    return s;
}

string AnnaClient::request(std::string cmd)
{
    if (state != ANNA_READY) return "";
    cmd += myformat("/%d",clid);
    DBG("request/1: '%s'\n",cmd.c_str());
    auto r = client->Get(cmd);
    if (r) {
        DBG("status = %d\n",r->status);
        if (r->status != OK_200) {
            state = ANNA_ERROR;
            internal_error = myformat("Remote rejected request %s: %d",cmd.c_str(),r->status);
            return "";
        }
        DBG("body = '%s'\n",r->body.c_str());
        return r->body;
    }

    state = ANNA_ERROR;
    internal_error = myformat("Remote request failed: %s",cmd.c_str());
    return "";
}

string AnnaClient::request(std::string cmd, std::string arg)
{
    // TODO
    return "";
}

bool AnnaClient::command(std::string cmd)
{
    return command(cmd,"<empty>");
}

bool AnnaClient::command(std::string cmd, std::string arg)
{
    if (state != ANNA_READY) return false;
    cmd += myformat("/%d",clid);
    DBG("command/1: '%s'\n",cmd.c_str());
    auto r = client->Post(cmd,arg,"text/plain");
    if (r) {
        DBG("status = %d\n",r->status);
        if (r->status != OK_200) {
            state = ANNA_ERROR;
            internal_error = myformat("Remote rejected command %s: %d",cmd.c_str(),r->status);
        }
        //DBG("body = '%s'\n",r->body.c_str());
    } else {
        state = ANNA_ERROR;
        internal_error = myformat("Remote command failed: %s",cmd.c_str());
    }
    return true;
}
