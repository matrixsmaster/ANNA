#pragma once

#include <list>
#include <map>
#include <string>
#include "brain.h"
#include "aria.h"

#define LSCS_VERSION "0.1.0"

struct AriaPod {
    Aria* ptr = nullptr;
    bool mark = false;
    int x = 0, y = 0, w = 0, h = 0;
};

struct AriaLink {
    std::string from, to;
    int pin_from, pin_to;
};

class AnnaLSCS : public AnnaBrain
{
public:
    AnnaLSCS();
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
    AriaPod* addPod(std::string name);
    AriaPod* getPod(std::string name);
    void delPod(std::string name);
    void delPod(AriaPod* pod);

    std::string getPodName(AriaPod* pod);
    void setPodName(AriaPod* pod, std::string nname);

    std::list<std::string> getPods();

    bool setPodScript(std::string name, std::string path);

    bool Link(AriaLink lnk);
    bool Unlink(AriaLink lnk);
    std::vector<AriaLink> getLinksFrom(std::string name);

    bool WriteTo(std::string fn);

private:
    std::string config_fn;
    std::map<std::string,std::string> cfgmap;
    std::map<std::string,AriaPod> pods;
    std::map<std::string,std::vector<AriaLink> > links;

    bool ParseConfig();
    bool CreatePods();
    void FanOut(std::string from);
};
