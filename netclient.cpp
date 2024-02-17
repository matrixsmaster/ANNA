#include "netclient.h"
#include "../server/httplib.h"
#include "../server/base64m.h"
#include "../server/codec.h"

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
    clid = make_clid();
    DBG("clid = %u (0x%08X)\n",clid,clid);

    state = ANNA_NOT_INITIALIZED;
    if (!cfg) return;

    client = new Client(server);
    if (!client->is_valid()) {
        state = ANNA_ERROR;
        internal_error = myformat("Server '%s' is not valid",server.c_str());
        return;
    }
    client->set_read_timeout(ANNA_CLIENT_TIMEOUT,0);
    client->set_write_timeout(ANNA_CLIENT_TIMEOUT,0);
    client->set_connection_timeout(ANNA_CLIENT_TIMEOUT,0);

    config = *cfg;
    state = ANNA_READY;

    if (!command("/sessionStart")) {
        state = ANNA_ERROR;
        internal_error = "Unable to create remote session!";
        return;
    }

    AnnaClient::setConfig(config);
}

AnnaClient::~AnnaClient()
{
    DBG("client d'tor\n");
    command("/sessionEnd",true);
}

AnnaState AnnaClient::getState()
{
    if (state == ANNA_ERROR) return state; // internal error has already occured
    int r = atoi(request("/getState").c_str());
    if (state == ANNA_ERROR) return state; // request failed
    return (r < 0 || r >= ANNA_NUM_STATES)? ANNA_ERROR : (AnnaState)r;
}

const string &AnnaClient::getError()
{
    if (state != ANNA_READY) return internal_error;
    internal_error = fromBase64(request("/getError"));
    return internal_error;
}

int AnnaClient::getTokensUsed()
{
    return atoi(request("/getTokensUsed").c_str());
}

AnnaConfig AnnaClient::getConfig()
{
    string r = request("/getConfig");
    if (r.empty()) return config; // return internal config (server busy?)

    AnnaConfig cfg;
    size_t n = fromBase64(&cfg,sizeof(cfg),r);
    if (n != sizeof(cfg)) {
        state = ANNA_ERROR;
        internal_error = "Failed to read encoded config";
    } else
        config = cfg;

    return config;
}

void AnnaClient::setConfig(const AnnaConfig &cfg)
{
    DBG("sizeof AnnaConfig = %lu\n",sizeof(AnnaConfig));
    config = cfg;

    // replace full file path with just file name
    string fn = config.params.model;
    memset(config.params.model,0,sizeof(config.params.model));
    auto ps = fn.rfind('/');
    if (ps != string::npos) fn.erase(0,ps+1);
    strncpy(config.params.model,fn.c_str(),sizeof(config.params.model));

    // infill large strings
    codec_infill_str(config.params.model,sizeof(config.params.model));
    codec_infill_str(config.params.prompt,sizeof(config.params.prompt));

    // encode and send
    string enc = asBase64((void*)&(config),sizeof(config));
    DBG("encoded state len = %lu\n",enc.size());
    command("/setConfig",enc);
}

string AnnaClient::getOutput()
{
    return fromBase64(request("/getOutput"));
}

void AnnaClient::setInput(string inp)
{
    command("/setInput",asBase64(inp));
}

void AnnaClient::setPrefix(string str)
{
    command("/setPrefix",asBase64(str));
}

//const char *AnnaClient::TokenToStr(llama_token token)
//{
//}

string AnnaClient::PrintContext()
{
    return request("/printContext");
}

bool AnnaClient::SaveState(string fname, const void* user_data, size_t user_size)
{
    string r = request("/saveState",fname);
    return (r == "success");
}

bool AnnaClient::LoadState(string fname, void* user_data, size_t* user_size)
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

string AnnaClient::asBase64(const void *data, size_t len)
{
    uint8_t* tmp = (uint8_t*)malloc(len);
    if (!tmp) {
        state = ANNA_ERROR;
        internal_error = myformat("Unable to allocate %lu bytes of memory!\n",len);
        return "";
    }

    memcpy(tmp,data,len);
    codec_forward(clid,tmp,len);

    string s;
    s.resize(len*2+1);
    size_t n = encode((char*)s.data(),s.size(),tmp,len);
    s.resize(n);
    free(tmp);
    return s;
}

string AnnaClient::asBase64(const string& in)
{
    return asBase64(in.data(),in.size());
}

string AnnaClient::fromBase64(const string& in)
{
    string s;
    s.resize(in.size());
    size_t n = fromBase64((char*)s.data(),s.size(),in);
    s.resize(n);
    return s;
}

size_t AnnaClient::fromBase64(void *data, size_t len, string in)
{
    size_t n = decode(data,len,in.c_str());
    codec_backward(clid,data,n);
    DBG("%lu bytes decoded from %lu bytes string\n",n,strlen(in.c_str()));
    return n;
}

string AnnaClient::request(string cmd)
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
        //DBG("body = '%s'\n",r->body.c_str());
        return r->body;
    }

    state = ANNA_ERROR;
    internal_error = myformat("Remote request failed: %s",cmd.c_str());
    return "";
}

string AnnaClient::request(string cmd, string arg)
{
    if (state != ANNA_READY) return "";
    cmd += myformat("/%d",clid);
    DBG("request/2: '%s' '%s'\n",cmd.c_str(),arg.c_str());
    Params params { { "arg", arg } };
    auto r = client->Get(cmd,params,Headers(),Progress());
    if (r) {
        DBG("status = %d\n",r->status);
        if (r->status != OK_200) {
            state = ANNA_ERROR;
            internal_error = myformat("Remote rejected request %s: %d",cmd.c_str(),r->status);
            return "";
        }
        //DBG("body = '%s'\n",r->body.c_str());
        return r->body;
    }

    state = ANNA_ERROR;
    internal_error = myformat("Remote request failed: %s",cmd.c_str());
    return "";
}

bool AnnaClient::command(string cmd, bool force)
{
    return command(cmd,"<empty>",force);
}

bool AnnaClient::command(string cmd, string arg, bool force)
{
    if (!force && state != ANNA_READY) return false;
    cmd += myformat("/%d",clid);
    DBG("command: '%s'\n",cmd.c_str());
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
