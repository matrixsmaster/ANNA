#pragma once

#include <vector>
#include <string>
#include "brain.h"

class AnnaClient : public AnnaBrain
{
public:
    AnnaClient(AnnaConfig* cfg);
    virtual ~AnnaClient();

    AnnaState getState() override;
    /*const std::string & getError() override;
    int getTokensUsed() override;
    AnnaConfig* getConfig() override;

    std::string getOutput() override;
    void setInput(std::string inp) override;
    void setPrefix(std::string str) override;

    const char* TokenToStr(llama_token token) override;
    std::string PrintContext() override;

    bool SaveState(std::string fname) override;
    bool LoadState(std::string fname) override;

    void setClipModelFile(std::string fn) override;
    bool EmbedImage(std::string imgfile) override;

    AnnaState Processing(bool skip_sampling = false) override;
    void Reset() override;
    void Undo() override;*/

private:

};
