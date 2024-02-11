#pragma once

#include <vector>
#include <string>
#include <deque>
#include "llama.h"
#include "dtypes.h"
#include "common.h"
#include "sampling.h"

#define ANNA_VERSION "0.7.3"

#define ANNA_FORMAT_DEF_CHARS 1024

enum AnnaState
{
    ANNA_NOT_INITIALIZED = 0,
    ANNA_READY,
    ANNA_ERROR,
    ANNA_PROCESSING,
    ANNA_TURNOVER,
    ANNA_NUM_STATES
};

struct __attribute__((packed)) AnnaConfig
{
    int verbose_level;
    bool convert_eos_to_nl;
    bool nl_to_turnover;
    gpt_params params;
    void* user;
};

class AnnaBrain
{
public:
    AnnaBrain(AnnaConfig* cfg);
    virtual ~AnnaBrain();

    virtual AnnaState getState()                    { return state; }
    virtual const std::string & getError()          { return internal_error; }
    virtual int getTokensUsed()                     { return n_past; }
    virtual AnnaConfig getConfig()                  { return config; }
    virtual void setConfig(const AnnaConfig& cfg)   { config = cfg; }

    virtual std::string getOutput();
    virtual void setInput(std::string inp);
    virtual void setPrefix(std::string str);

    static std::string StateToStr(AnnaState s);
    virtual const char* TokenToStr(llama_token token);
    virtual std::string PrintContext();

    virtual bool SaveState(std::string fname);
    virtual bool LoadState(std::string fname);

    virtual void setClipModelFile(std::string fn);
    virtual bool EmbedImage(std::string imgfile);

    virtual AnnaState Processing(bool skip_sampling = false);
    virtual void Reset();
    virtual void Undo();

    static std::string myformat(const char* fmt, ...);

protected:
    AnnaState state = ANNA_NOT_INITIALIZED;
    AnnaConfig config;
    std::string internal_error;
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    llama_sampling_context* ctx_sp = nullptr;
    int n_past = 0, old_past = 0;
    int n_remain, n_consumed = 0;
    int ga_i = 0;
    std::vector<llama_token> queue,prompt,inp_emb;
    std::vector<llama_token> oldqueue,oldcontext;
    std::deque<llama_token> forced_start;
    std::vector<float> ext_emb;
    std::string accumulator,piecebuf;
    std::string clip_file;

    static void anna_no_log(ggml_log_level level, const char * text, void * user_data);
    static void backend_init();
    static void backend_free();

    llama_batch batch_embeddings(int n_tokens, float *embeds, int n_past);
    void print_vec(std::string& str, const std::vector<llama_token>& vec);

    void Evaluate();
    void Generate();
};
