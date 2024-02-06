#pragma once

#include <vector>
#include <string>
#include "brain.h"

#define ANNA_CLIENT_MASK 0x3FFFFFFFUL

// Avoid inclusion of httplib.h into any header files
namespace httplib {
    class Client;
}

class AnnaClient : public AnnaBrain
{
public:
    AnnaClient(AnnaConfig* cfg, std::string server);
    virtual ~AnnaClient();

    AnnaState getState() override;
    const std::string & getError() override;
    int getTokensUsed() override;
    AnnaConfig getConfig() override;
    void setConfig(const AnnaConfig& cfg) override;

    std::string getOutput() override;
    void setInput(std::string inp) override;
    void setPrefix(std::string str) override;

    //const char* TokenToStr(llama_token token) override;
    std::string PrintContext() override;

    bool SaveState(std::string fname) override;
    bool LoadState(std::string fname) override;

    void setClipModelFile(std::string fn) override;
    bool EmbedImage(std::string imgfile) override;

    AnnaState Processing(bool skip_sampling = false) override;
    void Reset() override;
    void Undo() override;

private:
    httplib::Client* client;
    uint32_t clid;

    std::string asBase64(void* data, int len);
    std::string request(std::string cmd);
    std::string request(std::string cmd, std::string arg);
    void command(std::string cmd);
    void command(std::string cmd, std::string arg);
};
