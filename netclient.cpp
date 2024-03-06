#include <unistd.h>
#include <chrono>
#include <future>
#include "netclient.h"
#include "../server/httplib.h"
#include "../server/base64m.h"
#include "../server/codec.h"
#include "lfs.h"
#include "md5calc.h"

//#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
//#else
//#define DBG(...)
//#endif

using namespace std;
using namespace httplib;

AnnaClient::AnnaClient(AnnaConfig* cfg, string server, bool mk_dummy, waitfunction wait_cb) :
    AnnaBrain(nullptr),
    create_dummy(mk_dummy),
    wait_callback(wait_cb)
{
    DBG("client c'tor\n");
    state = ANNA_NOT_INITIALIZED;
    if (!cfg) return;

    // create a client ID for ourselves
    clid = make_clid();
    DBG("clid = %u (0x%08X)\n",clid,clid);

    // create HTTP client
    client = new Client(server);
    if (!client->is_valid()) {
        state = ANNA_ERROR;
        internal_error = myformat("Server '%s' is not valid",server.c_str());
        return;
    }
    client->set_read_timeout(ANNA_CLIENT_TIMEOUT,0);
    client->set_write_timeout(ANNA_CLIENT_TIMEOUT,0);
    client->set_connection_timeout(ANNA_CLIENT_TIMEOUT,0);

    // set internal data
    config = *cfg;
    state = ANNA_READY;
    fixConfig(); // this changes internal config

    // begin the session
    if (request(true,"/sessionStart",ANNA_CLIENT_VERSION).empty()) {
        state = ANNA_ERROR;
        internal_error = "Unable to create remote session!";
        return;
    }

    // request model file presence and upload the file if necessary
    string mrq = request(false,"/checkModel",config.params.model);
    DBG("Model request: %s\n",mrq.c_str());
    // TODO: check file size and show a warning
    if (mrq.substr(0,2) != "OK") {
        DBG("Uploading model file %s...\n",cfg->params.model);
        if (!UploadModel(cfg->params.model,config.params.model)) {
            state = ANNA_ERROR;
            return;
        }
        DBG(" upload done\n");
    }

    // finally we can attempt to upload the config
    AnnaClient::setConfig(config);
}

AnnaClient::~AnnaClient()
{
    DBG("client d'tor\n");
    request(true,"/sessionEnd"," ","",true);
}

AnnaState AnnaClient::getState()
{
    if (state == ANNA_ERROR) return state; // internal error has already occured
    int r = atoi(request(false,"/getState").c_str());
    if (state == ANNA_ERROR) return state; // request failed
    return (r < 0 || r >= ANNA_NUM_STATES)? ANNA_ERROR : (AnnaState)r;
}

const string &AnnaClient::getError()
{
    // in non-ready state, we don't want to call the remote
    if (state != ANNA_READY) return internal_error;
    // check remote error first
    string err = fromBase64(request(false,"/getError"));
    // we might or might not have a remote error, but we also might have an internal error
    if (!err.empty()) internal_error = err;
    return internal_error;
}

int AnnaClient::getTokensUsed()
{
    return atoi(request(false,"/getTokensUsed").c_str());
}

AnnaConfig AnnaClient::getConfig()
{
    string r = request(false,"/getConfig");
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
    DBG("sizeof AnnaConfig = %zu\n",sizeof(AnnaConfig));
    config = cfg;
    fixConfig();

    // infill large strings
    codec_infill_str(config.params.model,sizeof(config.params.model));
    codec_infill_str(config.params.prompt,sizeof(config.params.prompt));

    // encode and send
    string enc = asBase64((void*)&(config),sizeof(config));
    DBG("encoded state len = %zu\n",enc.size());
    request(true,"/setConfig",enc);
}

string AnnaClient::getOutput()
{
    return fromBase64(request(false,"/getOutput"));
}

void AnnaClient::setInput(string inp)
{
    request(true,"/setInput",asBase64(inp));
}

void AnnaClient::setPrefix(string str)
{
    request(true,"/setPrefix",asBase64(str));
}

void AnnaClient::addEmbeddings(const std::vector<float>& emb)
{
    string enc = asBase64(emb.data(),emb.size()*sizeof(float));
    DBG("encoded embedding len = %zu\n",enc.size());
    request(true,"/addEmbeddings",enc);
}

string AnnaClient::PrintContext()
{
    return request(false,"/printContext");
}

bool AnnaClient::SaveState(string fname, const void* user_data, size_t user_size)
{
    return false;
}

bool AnnaClient::LoadState(string fname, void* user_data, size_t* user_size)
{
    return false;
}

bool AnnaClient::UploadModel(string fpath, string mname)
{
    FILE* f = fopen(fpath.c_str(),"rb");
    if (!f) {
        internal_error = myformat("Unable to open file '%s' for reading",fpath.c_str());
        return false;
    }

    // get size
    mseek(f,0,SEEK_END);
    size_t sz = mtell(f);
    mseek(f,0,SEEK_SET);

    // try to negotiate uploading
    string urq = request(false,"/uploadModel",myformat("%zu",sz),mname);
    if (urq != "OK") {
        fclose(f);
        internal_error = "uploadModel request failed: " + urq;
        return false;
    }

    // do the actual transfer
    DBG("Upload started... ");
    bool r = uploadFile(f,sz);
    DBG("Finished uploading\n");

    // finalize transfer
    request(true,"/endTransfer");
    if (wait_callback) wait_callback(-1,false,"");

    fclose(f);
    return r;
}

bool AnnaClient::EmbedImage(std::string imgfile)
{
    // being previously updated on the server, the config might have different amount of threads specified
    if (config.params.n_threads > (int)thread::hardware_concurrency())
        config.params.n_threads = thread::hardware_concurrency();
    // now we can use EmbedImage as usual
    return AnnaBrain::EmbedImage(imgfile);
}

AnnaState AnnaClient::Processing(bool skip_sampling)
{
    int r = atoi(request(false,"/processing",skip_sampling? "skip":"noskip").c_str());
    return (r < 0 || r >= ANNA_NUM_STATES)? ANNA_ERROR : (AnnaState)r;
}

void AnnaClient::Reset()
{
    request(true,"/reset");
}

void AnnaClient::Undo()
{
    request(true,"/undo");
}

void AnnaClient::KeepAlive()
{
    request(true,"/keepAlive");
}

void AnnaClient::fixConfig()
{
    // replace LLM file name with its hash
    string fn = config.params.model;
    string tmp = fn;
    transform(tmp.begin(),tmp.end(),tmp.begin(),[](char c) { return ::toupper(c); });
    if (fn.length() == MD5_DIGEST_STR && tmp.find(".GGUF") == string::npos) return; // already replaced

    string hash = hashFile(fn);
    if (hash.empty()) {
        state = ANNA_ERROR;
        internal_error = myformat("Unable to calculate hash for file '%s'",fn.c_str());
        return;
    }
    strncpy(config.params.model,hash.c_str(),sizeof(config.params.model)-1);
}

string AnnaClient::asBase64(const void *data, size_t len)
{
    uint8_t* tmp = (uint8_t*)malloc(len);
    if (!tmp) {
        state = ANNA_ERROR;
        internal_error = myformat("Unable to allocate %zu bytes of memory!\n",len);
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
    DBG("%zu bytes decoded from %zu bytes string\n",n,strlen(in.c_str()));
    return n;
}

string AnnaClient::request(bool post, const string cmd, const string arg, const string mod, bool force)
{
    if (!force && state != ANNA_READY) return "";

    // format the command/request URL and add modification part if needed
    string fcmd = cmd + myformat("/%d",clid);
    if (!mod.empty()) fcmd += "/" + mod;

    // create an async object to offload the actual Get/Post, allowing the main thread to continue its stuff
    auto rhnd = async(launch::async,[&]() -> auto {
        if (post) {
            DBG("post: %s\n",fcmd.c_str());
            return client->Post(fcmd,arg,"text/plain");
        } else {
            Params p;
            if (!arg.empty()) {
                p.insert(pair<string,string>("arg",arg));
                DBG("argument '%s' added\n",arg.c_str());
            }
            DBG("get: %s\n",fcmd.c_str());
            return client->Get(fcmd,p,Headers(),Progress());
        }
    });

    // use wait function while async object is busy
    bool dowait = true;
    while (rhnd.wait_for(ANNA_REQUEST_CHECK) != future_status::ready) {
        // don't fire up wait function again if it was rejected once (another thread is already using it)
        if (wait_callback && dowait)
            dowait = wait_callback(0,true,"Waiting for server response...");
    }
    if (wait_callback) wait_callback(-1,false,"");

    // finally we can acquire the request result
    Result r = rhnd.get();
    if (!r) {
        state = ANNA_ERROR;
        internal_error = myformat("Remote request failed: %s",fcmd.c_str());
        return "";
    }

    // response status handling - OK, RETRY or FAIL
    DBG("status = %d\n",r->status);
    switch (r->status) {
    case OK_200:
        if (wait_callback) wait_callback(-1,false,"");
        return post? "OK" : r->body;

    case ServiceUnavailable_503:
        DBG("Temporarily unavailable, retrying...\n");
        if (wait_callback) wait_callback(0,true,"Server is busy. Waiting in the queue...");
        else sleep(1);
        return request(post,cmd,arg,mod,force); // tail recursion

    default:
        state = ANNA_ERROR;
        if (wait_callback) wait_callback(-1,false,"");
        internal_error = myformat("Remote rejected request %s: %d",fcmd.c_str(),r->status);
        if (r->status == ImATeapot_418) internal_error += " " + r->body;
        return "";
    }
}

bool AnnaClient::uploadFile(FILE* f, size_t sz)
{
    uint8_t* buf = (uint8_t*)malloc(ANNA_CLIENT_CHUNK);
    if (!buf) {
        internal_error = "Unable to allocate buffer in uploadFile()";
        return false;
    }

    // temporarily substitute the waiting function to prevent generating "end-of-progress" events from request(true,...)
    waitfunction wcb = wait_callback;
    wait_callback = nullptr;

    // do the transfer chunk by chunk
    size_t i = 0;
    while (!feof(f)) {
        // read the data
        size_t r = ANNA_CLIENT_CHUNK;
        size_t p = mtell(f);
        if (!fread(buf,ANNA_CLIENT_CHUNK,1,f)) { // try bufferized read
            mseek(f,p,SEEK_SET);
            r = fread(buf,1,ANNA_CLIENT_CHUNK,f); // try partial read
            if (!r) break;
        }

        // make it appear responsive
        float prg = (float)i / (float)(sz / ANNA_CLIENT_CHUNK) * 100.f;
        if (wcb) wcb(ceil(prg),false,"Uploading file...");

        // encode and send
        string enc = asBase64(buf,r);
        bool flag = false;
        AnnaState oldst = state;
        for (int retry = 0; !flag && retry < ANNA_TRANSFER_RETRIES; retry++) {
            if (!request(true,"/setChunk",enc,myformat("%zu",i),true).empty()) // force request
                flag = true;
            else {
                state = oldst; // we know we'll be in ERROR state due to failed transfer, so restore what we knew before
                if (wcb) wcb(ceil(prg),true,myformat("Retrying upload %d/%d...",retry+1,ANNA_TRANSFER_RETRIES));
                else usleep(1000UL * ANNA_RETRY_WAIT_MS);
            }
        }
        if (!flag) {
            free(buf);
            wait_callback = wcb;
            return false;
        }
        i++;
    }

    free(buf);
    wait_callback = wcb;
    return true;
}

bool AnnaClient::downloadFile(FILE* f, size_t sz)
{
    // TODO
    return false;
}

string AnnaClient::hashFile(const std::string fn)
{
    string res;
    FILE* f = fopen(fn.c_str(),"rb");
    if (!f) {
        internal_error = myformat("Unable to open file %s",fn.c_str());
        return "";
    }

    // get size
    mseek(f,0,SEEK_END);
    size_t sz = mtell(f);
    mseek(f,0,SEEK_SET);

    // check if it's a dummy file (sz == sizeof(hash string))
    if (sz == MD5_DIGEST_STR) {
        res.resize(MD5_DIGEST_STR);
        size_t r = fread((void*)res.data(),MD5_DIGEST_STR,1,f);
        fclose(f);
        if (r) return res;
        else {
            internal_error = myformat("Unable to read from file %s",fn.c_str());
            return "";
        }
    }

    // otherwise get its hash
    auto lp = chrono::steady_clock::now();
    res = md5FileToStr(f,[&](size_t rb) {
        int ms = (chrono::steady_clock::now() - lp) / 1ms;
        if (wait_callback && ms > ANNA_HASH_UPDATE_MS) {
            lp = chrono::steady_clock::now();
            wait_callback(floor((double)rb / (double)sz * 100.f),false,"Calculating hash...");
        }
    });
    if (wait_callback) wait_callback(-1,false,"");
    fclose(f);

    // if we want to save dummy files, let's do it
    if (create_dummy) {
        string nfn = fn + ".dummy";
        f = fopen(nfn.c_str(),"wb");
        if (f) {
            fwrite(res.c_str(),res.length(),1,f);
            fclose(f);
        } else
            internal_error = myformat("Unable to write to file %s",nfn.c_str());
    }

    return res;
}
