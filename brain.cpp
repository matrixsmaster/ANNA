#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "brain.h"

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;

static int users = 0;

AnnaBrain::AnnaBrain(AnnaConfig* cfg)
{
    if (!cfg) return; // leave in partially initialized state, so it can be safely deleted later
    config = *cfg;

    // set logging if info is requested
    llama_log_set((cfg->verbose_level? NULL:anna_no_log),NULL);
    //cublas_enable_log(g_info); // FIXME: implement properly!

    // init backend if not intialized already
    backend_init();

    // load the model
    tie(model,ctx) = llama_init_from_gpt_params(cfg->params);
    if (!model) {
        internal_error = myformat("Failed to load model '%s'",cfg->params.model.c_str());
        state = ANNA_ERROR;
        return;
    }

    // initialize sampling
    ctx_sp = llama_sampling_init(cfg->params);
    state = ANNA_READY;
}

AnnaBrain::~AnnaBrain()
{
    if (ctx_sp) llama_sampling_free(ctx_sp);
    if (ctx) llama_free(ctx);
    if (model) llama_free_model(model);
    backend_free();
}

void AnnaBrain::backend_init()
{
    if (!users) llama_backend_init(false);
    users++;
}

void AnnaBrain::backend_free()
{
    if (--users <= 0) llama_backend_free();
}

string AnnaBrain::myformat(const char* fmt, ...)
{
    string res;
    res.resize(ANNA_FORMAT_DEF_CHARS);
    va_list vl;
    va_start(vl,fmt);
    vsnprintf((char*)&(res[0]),res.size(),fmt,vl); // tricky avoidance of a known "issue"
    va_end(vl);
    return res;
}

void AnnaBrain::Evaluate()
{
    if (state != ANNA_READY && state != ANNA_PROCESSING) return;
    DBG("EVALUATE\n");

    if (!queue.empty()) {
        if (n_past + (int)queue.size() > llama_n_ctx(ctx)) {
            int n_left = n_past - (int)prompt.size();
            //DBG("n_past = %d, prompt = %d, n_left = %d\n",n_past,(int)prompt.size(),n_left);

            // FIXME: instead of reeval, we might shift kv cache:
            //llama_kv_cache_seq_rm(ctx, 0, config.params.n_keep + 1, config.params.n_keep + n_discard + 1);
            //llama_kv_cache_seq_shift(ctx, 0, config.params.n_keep + 1 + n_discard, n_past, -n_discard);

            //vector<llama_token> tmp = queue;
            queue = prompt;
            queue.insert(queue.end(),ctx_sp->prev.end()-n_left/2,ctx_sp->prev.end());
            n_past = 0; // reset
            DBG("\nqueue now = %d, n_left now = %d\n",(int)queue.size(),n_left);
        }

        for (int i = 0; i < (int)queue.size(); i+=config.params.n_batch) {
            int n_eval = (int)queue.size() - i;
            if (n_eval > config.params.n_batch) n_eval = config.params.n_batch;

            if (llama_eval(ctx,&queue[i],n_eval,n_past)) {
                internal_error = myformat("Failed to eval '%s'",TokenToStr(queue[i]));
                state = ANNA_ERROR;
                return;
            }

            n_past += n_eval;
        }
    }
    queue.clear();

    if (!inp_emb.empty()) {
        while ((int)inp_emb.size() > n_consumed) {
            queue.push_back(inp_emb[n_consumed]);
            llama_sampling_accept(ctx_sp,ctx,inp_emb[n_consumed]);
            ++n_consumed;
            if ((int)queue.size() >= config.params.n_batch) break;
        }

        if (n_consumed >= (int)inp_emb.size()) inp_emb.clear();
        state = ANNA_PROCESSING;
    } else
        state = ANNA_READY;
}

void AnnaBrain::Generate()
{
    if (state != ANNA_READY) return;
    DBG("GENERATE\n");

    llama_token tok = -1;

    // token enforcement
    if (!forced_start.empty()) {
        tok = forced_start.front();
        forced_start.pop_front();
        DBG("Enforcing token %d...\n",tok);
    }

    // prediction max length reached, force EOS
    if (--n_remain == 0) {
        tok = llama_token_eos(ctx);
        n_remain = config.params.n_predict; // reset for next round
    }

    // usual sampling
    if (tok < 0) tok = llama_sampling_sample(ctx_sp,ctx,NULL);

    // Deal with EOS token
    if (tok == llama_token_eos(ctx)) {
        DBG("*** EOS detected ***\n");
        if (config.convert_eos_to_nl) tok = llama_token_nl(ctx);
        state = ANNA_TURNOVER; // turn over to the user or any other external information supplier
    }

    llama_sampling_accept(ctx_sp,ctx,tok);
    queue.push_back(tok);
    accumulator += llama_token_to_piece(ctx,tok);
}

AnnaState AnnaBrain::Processing(bool skip_sampling)
{
    //DBG("Main loop: n_remain = %d, queue size = %ld\n",n_remain,queue.size());

    Evaluate();
    if (!skip_sampling) Generate();

    return state;
}

string AnnaBrain::getOutput()
{
    string tmp = accumulator;
    accumulator.clear();
    return tmp;
}

void AnnaBrain::setInput(string inp)
{
    if (state == ANNA_TURNOVER) state = ANNA_READY; // revert the state
    else if (state != ANNA_READY) return;

    DBG("Input: '%s'\n",inp.c_str());
    auto emb = ::llama_tokenize(ctx,inp,true);
    if (emb.empty()) return;
    if ((int)emb.size() >= llama_n_ctx(ctx)) {
        internal_error = myformat("Too many tokens in input: %d tokens for a %d tokens context window!\n",(int)emb.size(),llama_n_ctx(ctx));
        state = ANNA_ERROR;
    }

    inp_emb.insert(inp_emb.end(),emb.begin(),emb.end());
    //DBG("Input size: %d tokens, only %d tokens left for a free conversation\n",(int)inp_emb.size(),llama_n_ctx(ctx)-(int)inp_emb.size());
    DBG("Input size: %d tokens\n",(int)inp_emb.size());
    n_consumed = 0;
    n_remain = config.params.n_predict;

    // save "undo point"
    oldcontext = ctx_sp->prev;
    oldqueue = queue;
    old_past = n_past;

    if (prompt.empty()) prompt = emb; // save the first sequence as prompt
}

void AnnaBrain::Undo()
{
    ctx_sp->prev = oldcontext;
    queue = oldqueue;
    n_past = old_past;
}

void AnnaBrain::setPrefix(string str)
{
    if (state != ANNA_READY) return;

    DBG("Token enforcement: '%s' = ",str.c_str());
    auto tmp = ::llama_tokenize(ctx,str,false,true);
    for (auto &i : tmp) {
        forced_start.push_back(i);
        DBG("%d (%s) ",i,TokenToStr(i));
    }
}

const char* AnnaBrain::TokenToStr(llama_token token)
{
    piecebuf = llama_token_to_piece(ctx,token);
    return piecebuf.c_str();
}

bool AnnaBrain::SaveState(std::string fname)
{
    if (state == ANNA_NOT_INITIALIZED) return false;

    int fd = open(fname.c_str(),O_CREAT|O_TRUNC|O_RDWR,00664);
    if (fd < 0) {
        internal_error = myformat("Unable to open file '%s' for writing",fname.c_str());
        state = ANNA_ERROR;
        return false;
    }

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

    if (ftruncate(fd,total)) {
        internal_error = myformat("Unable to truncate to %u\n",total);
        state = ANNA_ERROR;
        return false;
    }
    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_WRITE,MAP_SHARED,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map WR memory for cache buffer (%u bytes)",total);
        state = ANNA_ERROR;
        return false;
    }

    uint8_t* ptr = data;

    // 1. n of used tokens
    memcpy(ptr,&n_past,sizeof(int));
    ptr += sizeof(int);

    // 2. context tokens
    ctx_sp->prev.resize(llama_n_ctx(ctx));
    memcpy(ptr,ctx_sp->prev.data(),csize);
    ptr += csize;

    // 3. state data size
    memcpy(ptr,&dsize,4);
    ptr += 4;

    // 4. state data
    llama_copy_state_data(ctx,ptr);
    munmap(data,total);

    DBG("Cache (%u bytes) saved to %s\n",total,fname.c_str());
    return true;
}

bool AnnaBrain::LoadState(std::string fname)
{
    if (state == ANNA_NOT_INITIALIZED) return false;

    int fd = open(fname.c_str(),O_RDONLY|O_DIRECT);
    if (fd < 0) {
        DBG("Cache file doesn't exist. This might or might not be an error.\n");
        return false;
    }

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

    struct stat sb;
    if (fstat(fd,&sb)) {
        internal_error = myformat("Unable to stat() cache buffer (err %d)",errno);
        state = ANNA_ERROR;
        close(fd);
        return false;
    }
    if (sb.st_size != total) {
        internal_error = myformat("Unexpected cache file size! (expected %u, got %lu)",total,sb.st_size);
        state = ANNA_ERROR;
        close(fd);
        return false;
    }

    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_READ,MAP_PRIVATE,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map RD memory for cache buffer (%u bytes)",total);
        state = ANNA_ERROR;
        return false;
    }

    uint8_t* ptr = data;

    // 1. n of used tokens
    memcpy(&n_past,ptr,sizeof(int));
    ptr += sizeof(int);

    // 2. context tokens
    ctx_sp->prev.resize(llama_n_ctx(ctx));
    memcpy(ctx_sp->prev.data(),ptr,csize);
    ptr += csize;

    // 3. state data size
    memcpy(&dsize,ptr,4);
    ptr += 4;

    // 4. load state data
    llama_set_state_data(ctx,ptr);
    munmap(data,total);

    DBG("Cache (%u bytes) loaded from %s\n",dsize,fname.c_str());
    return true;
}

void AnnaBrain::anna_no_log(ggml_log_level level, const char * text, void * user_data)
{
    // This is an empty function
}
