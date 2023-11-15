#include "brain.h"

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;

AnnaBrain::AnnaBrain(AnnaConfig* cfg)
{
    if (!cfg) return; // leave in partially initialized state, so it can be safely deleted later

    // set logging if info is requested
    llama_log_set((cfg->verbose_level? NULL:anna_no_log),NULL);
    //cublas_enable_log(g_info); // FIXME: implement properly!

    // init backend if not intialized already
    backend_init();

    // load the model
    tie(model,ctx) = llama_init_from_gpt_params(cfg->params);
    if (!model) {
        internal_error = myformat("Failed to load model '%s'",params.model.c_str());
        state = ANNA_ERROR;
        return;
    }

    // initialize sampling
    ctx_sp = llama_sampling_init(params);
    state = ANNA_INITIALIZED;
}

AnnaBrain::~AnnaBrain()
{
    if (ctx_sp) llama_sampling_free(ctx_sp);
    if (ctx) llama_free(ctx);
    if (model) llama_free_model(model);
    backend_free();
}

AnnaBrain::backend_init()
{
    if (!users) llama_backend_init(false);
    users++;
}

AnnaBrain::backend_free()
{
    if (--users <= 0) llama_backend_free();
}

static string AnnaBrain::myformat(const char* fmt, ...)
{
    string res;
    res.resize(ANNA_FORMAT_DEF_CHARS);
    va_list vl;
    va_start(vl,fmt);
    vsnprintf(res.c_str(),res.size(),fmt,vl);
    va_end(vl);
    return res;
}

void AnnaBrain::Evaluate()
{
    if (!queue.empty()) {
        if (n_past + (int)queue.size() > llama_n_ctx(ctx)) {
            const int nseed = llama_get_rng_seed(ctx);
            if (reload_on_reset && load_cache(ctx,n_past,ctx_sampling->prev)) {
                queue.clear();
                inp_emb.clear();
                llama_set_rng_seed(ctx,nseed);
                n_remain = params.n_predict;
                DBG("State reloaded from cache due to context overflow. nseed = %d, n_past = %d\n",nseed,n_past);
                continue;
            }

            const int n_left = n_past - (int)prompt.size();
            //DBG("n_past = %d, prompt = %d, n_left = %d\n",n_past,(int)prompt.size(),n_left);

            // FIXME: instead of reeval, we might shift kv cache:
            //llama_kv_cache_seq_rm(ctx, 0, params.n_keep + 1, params.n_keep + n_discard + 1);
            //llama_kv_cache_seq_shift(ctx, 0, params.n_keep + 1 + n_discard, n_past, -n_discard);

            vector<llama_token> tmp = queue;
            queue = prompt;
            queue.insert(queue.end(),ctx_sampling->prev.end()-n_left/2,ctx_sampling->prev.end());
            n_past = 0; // reset
            DBG("\nqueue now = %d, n_left now = %d\n",(int)queue.size(),n_left);
        }

        for (int i = 0; i < (int)queue.size(); i+=params.n_batch) {
            int n_eval = (int)queue.size() - i;
            if (n_eval > params.n_batch) n_eval = params.n_batch;

            if (llama_eval(ctx,&queue[i],n_eval,n_past)) {
                ERR("Failed to eval '%s'",llama_token_to_str(ctx,queue[i]));
                return 1;
            }

            n_past += n_eval;
        }
    }
    queue.clear();

    // A kludge to replicate same batching as in llama.cpp - do input embedding after finalizing previous local queue
    if (!inp_emb.empty()) {
        //DBG("Doing input embedding!\n");
        //print_vec(ctx,inp_emb);
        while ((int)inp_emb.size() > n_consumed) {
            queue.push_back(inp_emb[n_consumed]);
            //context.erase(context.begin());
            //context.push_back(inp_emb[n_consumed]);
            llama_sampling_accept(ctx_sampling,ctx,inp_emb[n_consumed]);
            ++n_consumed;
            if ((int)queue.size() >= params.n_batch) break;
        }

        if (n_consumed >= (int)inp_emb.size()) inp_emb.clear();
        continue;
    }
}

void AnnaBrain::Generate()
{
    llama_token tok = -1;

    // token enforcement
    if ((force_prefix && !forced_start.empty()) || (i_fstart > 0)) {
        if (i_fstart < (int)forced_start.size()) {
            tok = forced_start[i_fstart++];
            DBG("Enforcing token %d...\n",tok);
        } else {
            i_fstart = 0;
            force_prefix = false;
        }
    }

    // prediction max length reached, force EOS
    if (--n_remain == 0) {
        if (no_input) break; // prevent infinite generation in non-interactive mode
        tok = llama_token_eos(ctx);
        n_remain = params.n_predict;
    }

    // usual sampling
    if (tok < 0) {

        tok = llama_sampling_sample(ctx_sampling,ctx,NULL);
        //llama_sampling_accept(ctx_sampling,ctx,tok);
        //DBG("last: %s\n",llama_token_to_str(ctx,ctx_sampling->prev.back()));

        //DBG("Sampled token %d '%s'\n",tok,llama_token_to_str(ctx,tok));
    }

    // Print the next token and always flush the stdout buffer to force the printing
    printf("%s",llama_token_to_str(ctx,tok));
    fflush(stdout);
    full_convo += llama_token_to_str(ctx,tok);

    // Deal with EOS token
    if (tok == llama_token_eos(ctx)) {
        DBG("*** EOS detected ***\n");
        if (g_eos) {
            if (no_input) break; // don't ignore EOS in non-interactive mode if EOS flag is set
        } else
            tok = tok_newline;
    }

    llama_sampling_accept(ctx_sampling,ctx,tok);
    queue.push_back(tok);
}

AnnaState AnnaBrain::Processing(skip_sampling)
{
    DBG("Main loop: n_remain = %d, queue size = %ld\n",n_remain,queue.size());

    if (state == ANNA_ERROR) return;
    Evaluate();
    if (state == ANNA_ERROR) return;
    if (!skip_sampling)
        Generate();

    return state;
}

void AnnaBrain::setForcedStart(string str)
{
    forced_start = ::llama_tokenize(ctx,str,false,true);
#ifndef NDEBUG
    DBG("Token enforcement: '%s' = ",g_tokenf.c_str());
    for (auto &i : forced_start) {
      DBG("%d (%s) ",i,llama_token_to_str(ctx,i));
    }
#endif
}

string AnnaBrain::getOutput()
{
    string tmp = accumulator;
    accumulator.clear();
    return tmp;
}

void AnnaBrain::setInput(string inp)
{
    inp_emb = ::llama_tokenize(ctx,params.prompt,true);
    prompt = inp_emb; // save first sequence as prompt
    DBG("Prompt size: %d tokens, only %d tokens left for a free conversation\n",(int)inp_emb.size(),params.llama_n_ctx(ctx)-(int)inp_emb.size());
    if ((int)inp_emb.size() >= llama_n_ctx(ctx)) {
        ERR("Too many tokens in prompt: %d tokens in prompt for a %d tokens context window!\n",(int)inp_emb.size(),llama_n_ctx(ctx));
        g_quit = true;
    }
    if (inp_emb.back() == tok_newline) {
        DBG("Newline token at the end of the prompt, skipping sampling for the first round...\n");
        skip_sampling = true;
    }

    if (params.prompt.empty()) {
            // first input will be considered prompt now
            inp_str.insert(0,1,' '); // add space
            inp_emb = ::llama_tokenize(ctx,inp_str,true);
            params.prompt = inp_str;
            prompt = inp_emb;
        } else {
            if (!g_uprefix.empty() && !full_convo.ends_with(g_uprefix) && !skip_sampling)
                inp_str = g_uprefix + inp_str;
            DBG("Actual string to be tokenized: '%s'\n",inp_str.c_str());
            inp_emb = ::llama_tokenize(ctx,inp_str,false);
        }
        if ((int)inp_emb.size() >= llama_n_ctx(ctx)) {
            ERR("Too many tokens in input: %d tokens for a %d tokens context window!\n",(int)inp_emb.size(),llama_n_ctx(ctx));
            g_quit = true;
        }
        n_consumed = 0;
        n_remain = params.n_predict;

            // save "undo point"
        oldcontext = ctx_sampling->prev;
        oldqueue = queue;
        old_past = n_past;
}

bool AnnaBrain::SaveState(std::string fname)
{
    int fd = open(g_scache.c_str(),O_CREAT|O_TRUNC|O_RDWR,00664);
    if (fd < 0) {
        ERR("Unable to open file '%s' for writing",g_scache.c_str());
        return;
    }

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

    if (ftruncate(fd,total)) {
        ERR("Unable to truncate to %u\n",total);
        return;
    }
    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_WRITE,MAP_SHARED,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        ERR("Unable to map WR memory for cache buffer (%u bytes)",total);
        return;
    }

    uint8_t* ptr = data;

    // 1. n of used tokens
    memcpy(ptr,&n_past,sizeof(int));
    ptr += sizeof(int);

    // 2. context tokens
    context.resize(llama_n_ctx(ctx));
    memcpy(ptr,context.data(),csize);
    ptr += csize;

    // 3. state data size
    memcpy(ptr,&dsize,4);
    ptr += 4;

    // 4. state data
    llama_copy_state_data(ctx,ptr);
    munmap(data,total);

    DBG("Cache (%u bytes) saved to %s\n",total,g_scache.c_str());
}

bool AnnaBrain::LoadState(std::string fname)
{
    int fd = open(g_scache.c_str(),O_RDONLY|O_DIRECT);
    if (fd < 0) {
        DBG("Cache file doesn't exist, so it will be populated after processing the prompt.\n");
        return false;
    }

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

    struct stat sb;
    if (fstat(fd,&sb)) {
        ERR("Unable to stat() cache buffer (err %d)",errno);
        close(fd);
        return false;
    }
    if (sb.st_size != total) {
        ERR("Unexpected cache file size! (expected %u, got %lu)",total,sb.st_size);
        close(fd);
        return false;
    }

    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_READ,MAP_PRIVATE,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        ERR("Unable to map RD memory for cache buffer (%u bytes)",total);
        return false;
    }

    uint8_t* ptr = data;

    // 1. n of used tokens
    memcpy(&n_past,ptr,sizeof(int));
    ptr += sizeof(int);

    // 2. context tokens
    context.resize(llama_n_ctx(ctx));
    memcpy(context.data(),ptr,csize);
    ptr += csize;

    // 3. state data size
    memcpy(&dsize,ptr,4);
    ptr += 4;

    // 4. load state data
    llama_set_state_data(ctx,ptr);
    munmap(data,total);

    DBG("Cache (%u bytes) loaded from %s\n",dsize,g_scache.c_str());
    return true;
}
