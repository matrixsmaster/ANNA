#pragma once

#include <vector>
#include <string>
#include <deque>
#include "llama.h"
#include "dtypes.h"
#include "common.h"
#include "sampling.h"
#include "vecstore.h"

#define ANNA_VERSION "0.11.1"

#define ANNA_FORMAT_DEF_CHARS 1024
#define ANNA_STATE_VERSION 3
#define ANNA_STATE_MAGIC "ANNA"

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
    int verbose_level       = 0;
    bool convert_eos_to_nl  = true;
    bool nl_to_turnover     = true;
    bool no_pad_in_prefix   = false;
    gpt_params params;
    void* user              = nullptr;
};

struct __attribute__((packed)) AnnaSave
{
    char magic[4];
    uint32_t version;
    AnnaConfig cfg;
    int n_past, n_remain, n_consumed, ga_i;
    size_t data_size, vector_size, user_size;
};

class AnnaBrain
{
public:
    AnnaBrain(AnnaConfig* cfg = nullptr);
    virtual ~AnnaBrain();

    virtual AnnaState getState()                    { return state; }
    virtual const std::string & getError()          { return internal_error; }
    virtual int getTokensUsed()                     { return n_past; }
    virtual AnnaConfig getConfig()                  { return config; }
    virtual void setConfig(const AnnaConfig& cfg)   { config = cfg; }
    virtual void setClipModelFile(std::string fn)   { clip_file = fn; }
    virtual std::string getClipModelFile()          { return clip_file; }

    virtual std::string getOutput();
    virtual void setInput(std::string inp);
    virtual void setPrefix(std::string str);
    virtual void addEmbeddings(const std::vector<float>& emb);

    static std::string StateToStr(AnnaState s);
    virtual const char* TokenToStr(llama_token token);
    virtual std::string PrintContext();
    virtual std::vector<llama_token> getContext();

    virtual bool SaveState(std::string fname, const void* user_data, size_t user_size);
    virtual bool LoadState(std::string fname, void* user_data, size_t* user_size);

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
    int n_past = 0, n_remain = 0, n_consumed = 0, ga_i = 0;
    std::vector<llama_token> queue,prompt,inp_emb;
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
