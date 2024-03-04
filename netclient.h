#pragma once

#include <vector>
#include <string>
#include <functional>
#include "brain.h"

#define ANNA_CLIENT_VERSION "0.3.3"

#define ANNA_CLIENT_TIMEOUT (4*60)
#define ANNA_CLIENT_CHUNK (8ULL * 1024ULL * 1024ULL)
#define ANNA_TRANSFER_RETRIES 15
#define ANNA_RETRY_WAIT_MS 500
#define ANNA_HASH_UPDATE_MS 100

// Avoid inclusion of httplib.h into any header files
namespace httplib {
    class Client;
}

typedef std::function<void (int,bool,const std::string&)> waitfunction;

class AnnaClient : public AnnaBrain
{
public:
    AnnaClient(AnnaConfig* cfg, std::string server, bool mk_dummy, waitfunction wait_cb);
    virtual ~AnnaClient();

    AnnaState getState() override;
    const std::string & getError() override;
    int getTokensUsed() override;
    AnnaConfig getConfig() override;
    void setConfig(const AnnaConfig& cfg) override;

    std::string getOutput() override;
    void setInput(std::string inp) override;
    void setPrefix(std::string str) override;
    void addEmbeddings(const std::vector<float>& emb) override;

    std::string PrintContext() override;

    bool SaveState(std::string fname, const void* user_data, size_t user_size) override;
    bool LoadState(std::string fname, void* user_data, size_t* user_size) override;
    bool UploadModel(std::string fpath, std::string mname);

    bool EmbedImage(std::string imgfile) override;

    AnnaState Processing(bool skip_sampling = false) override;
    void Reset() override;
    void Undo() override;

    void KeepAlive();

private:
    httplib::Client* client = nullptr;
    uint32_t clid = 0;
    bool create_dummy;
    waitfunction wait_callback;

    void fixConfig();

    std::string asBase64(const void* data, size_t len);
    std::string asBase64(const std::string& in);
    std::string fromBase64(const std::string& in);
    size_t fromBase64(void *data, size_t len, std::string in);

    std::string request(bool post, const std::string cmd, const std::string arg = "", const std::string mod = "", bool force = false);

    bool uploadFile(FILE* f, size_t sz);
    bool downloadFile(FILE* f, size_t sz);

    std::string hashFile(const std::string fn);
};
