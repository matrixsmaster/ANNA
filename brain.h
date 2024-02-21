#pragma once

#include <vector>
#include <string>
#include <deque>
#include "llama.h"
#include "dtypes.h"
#include "common.h"
#include "sampling.h"

#define ANNA_VERSION "0.9.0-pre"

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
    int verbose_level;
    bool convert_eos_to_nl;
    bool nl_to_turnover;
    gpt_params params;
    void* user;
};

struct __attribute__((packed)) AnnaSave
{
    char magic[4];
    uint32_t version;
    AnnaConfig cfg;
    int n_past;
    int ga_i;
    size_t data_size;
    size_t user_size;
};

template <class T> class vector_storage {
public:
    static size_t store(const std::vector<T> & vec, FILE* f) {
        uint64_t sz = vec.size();
        size_t r = fwrite(&sz,sizeof(sz),1,f);
        r += fwrite(vec.data(),sizeof(T),sz,f);
        return r;
    }

#if 0
    static size_t store(const std::vector<T> & vec, void* mem, size_t maxlen) {
        if (!mem) return 0;
        size_t wr = 0;
        uint64_t* sptr = static_cast<uint64_t*> (mem);
        *sptr = vec.size();
        T* ptr = static_cast<T*> (sptr+1);
        for (auto & i: vec) {
            if (wr >= maxlen) break;
            *ptr++ = i;
            wr += sizeof(T);
        }
        return wr;
    }
#else
    static void* store(const std::vector<T> & vec, void* mem) {
        if (!mem) return nullptr;
        uint64_t* sptr = static_cast<uint64_t*> (mem);
        *sptr = vec.size();
        T* ptr = reinterpret_cast<T*> (sptr+1);
        for (auto & i: vec) *ptr++ = i;
        return ptr;
    }
#endif

    static size_t load(std::vector<T> & vec, FILE* f) {
        uint64_t sz;
        size_t r = fread(&sz,sizeof(sz),1,f);
        vec.resize(sz);
        r += fread(vec.data(),sizeof(T),sz,f);
        return r;
    }

    static size_t load(std::vector<T> & vec, const void* mem, size_t maxlen) {
        if (!mem || !maxlen) return 0;
        size_t rd = 0;
        const uint64_t* sptr = static_cast<const uint64_t*> (mem);
        const T* ptr = reinterpret_cast<const T*> (sptr+1);
        for (uint64_t i = 0; i < *sptr && rd < maxlen; i++) {
            vec.push_back(*ptr++);
            rd += sizeof(T);
        }
        return rd;
    }
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

    virtual std::string getOutput();
    virtual void setInput(std::string inp);
    virtual void setPrefix(std::string str);

    static std::string StateToStr(AnnaState s);
    virtual const char* TokenToStr(llama_token token);
    virtual std::string PrintContext();

    virtual bool SaveState(std::string fname, const void* user_data, size_t user_size);
    virtual bool LoadState(std::string fname, void* user_data, size_t* user_size);

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
