#pragma once

#include <list>
#include <map>
#include <string>
#include "brain.h"
#include "aria.h"

#define LSCS_VERSION "0.0.3"

struct AriaPod {
    Aria* ptr;
    bool mark;
};

struct AriaLink {
    std::string from, to;
    int pin_from, pin_to;
};

class AnnaLSCS : public AnnaBrain
{
public:
    AnnaLSCS(std::string cfgfile);
    virtual ~AnnaLSCS();

    //AnnaState getState() override;
    //const std::string & getError() override;
    int getTokensUsed() override                                { return 0; }
    AnnaConfig getConfig() override;
    void setConfig(const AnnaConfig& cfg) override;

    //std::string getOutput() override;
    void setInput(std::string inp) override;
    void setPrefix(std::string) override                        {}
    void addEmbeddings(const std::vector<float>&) override      {}

    std::string PrintContext() override                         { return ""; }

    bool SaveState(std::string fname, const void* user_data, size_t user_size) override;
    bool LoadState(std::string fname, void* user_data, size_t* user_size) override;

    bool EmbedImage(std::string imgfile) override;

    AnnaState Processing(bool skip_sampling = false) override;
    void Reset() override;
    void Undo() override                                        {}

    void Clear();

private:
    std::string config_fn;
    std::map<std::string,std::string> cfgmap;
    std::map<std::string,AriaPod> pods;
    std::map<std::string,std::vector<AriaLink> > links;

    bool ParseConfig();
    bool CreatePods();
    void FanOut(std::string from);
};
