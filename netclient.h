#pragma once

#include <vector>
#include <string>
#include <functional>
#include "brain.h"

#define ANNA_CLIENT_TIMEOUT (4*60)

// Avoid inclusion of httplib.h into any header files
namespace httplib {
    class Client;
}

class AnnaClient : public AnnaBrain
{
public:
    AnnaClient(AnnaConfig* cfg, std::string server, std::function<void(bool)> wait_cb);
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

    AnnaState Processing(bool skip_sampling = false) override;
    void Reset() override;
    void Undo() override;

private:
    httplib::Client* client = nullptr;
    uint32_t clid = 0;
    std::function<void(bool)> wait_callback = nullptr;

    std::string asBase64(const void* data, size_t len);
    std::string asBase64(const std::string& in);
    std::string fromBase64(const std::string& in);
    size_t fromBase64(void *data, size_t len, std::string in);

    std::string request(const std::string cmd, const std::string arg = "");
    bool command(const std::string cmd, bool force = false);
    bool command(const std::string cmd, const std::string arg, bool force = false);

    bool uploadFile(FILE* f);
    bool downloadFile(FILE* f, size_t sz);
};
