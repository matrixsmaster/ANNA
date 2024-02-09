#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include "brain.h"
#include "clip.h"

#ifdef ANNA_USE_MMAP
#include <sys/mman.h>
#include <sys/stat.h>
#endif

//#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
//#else
//#define DBG(...)
//#endif

using namespace std;

static int users = 0;

static const char* states_to_strings[ANNA_NUM_STATES] = {
    "not initialized",
    "ready",
    "error",
    "processing",
    "turnover",
};

AnnaBrain::AnnaBrain(AnnaConfig* cfg)
{
    if (!cfg) return; // leave in partially initialized state, so it can be safely deleted later
    config = *cfg;

    // prepare config
    if (!config.params.seed) config.params.seed = time(NULL);
    config.params.n_threads_batch = config.params.n_threads;

    // set logging if info is requested
    llama_log_set((cfg->verbose_level? NULL:anna_no_log),NULL);
    //cublas_enable_log(g_info); // FIXME: implement properly!

    // init backend if not intialized already
    backend_init();

    // load the model
    tie(model,ctx) = llama_init_from_gpt_params(cfg->params);
    if (!model) {
        internal_error = myformat("Failed to load model '%s'",cfg->params.model);
        state = ANNA_ERROR;
        return;
    }
    if (config.params.grp_attn_n <= 1)
        llama_adjust_rope_freq(ctx,config.params.n_ctx);

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
    int r = vsnprintf((char*)&(res[0]),res.size(),fmt,vl); // tricky avoidance of a known "issue"
    va_end(vl);
    res.resize(r);
    return res;
}

void AnnaBrain::Evaluate()
{
    if (state != ANNA_READY && state != ANNA_PROCESSING) return;
    DBG("EVALUATE\n");

    if (!queue.empty() || !ext_emb.empty()) {
        int n_embd  = llama_n_embd(llama_get_model(ctx));
        int n_ext_emb = (int)ext_emb.size() / n_embd;
        int ga_n = config.params.grp_attn_n;
        int ga_w = config.params.grp_attn_w;

        // check context window
        if (ga_n == 1) {
            if (n_past + (int)queue.size() + n_ext_emb > (int)llama_n_ctx(ctx)) {
                // context overflow
                if (!n_past) {
                    internal_error = myformat("Impossible queue length for the context window size: queue = %lu, ext_emb = %d\n",queue.size(),n_ext_emb);
                    state = ANNA_ERROR;
                    return;
                }

                DBG("Context overflow: n_past = %d, queue = %lu, ext_emb = %d\n",n_past,queue.size(),n_ext_emb);

#if 0 /* FIXME: Bring back reload-on-reset feature later */
                int nseed = llama_get_rng_seed(ctx);
                if (reload_on_reset && load_cache(ctx,n_past,ctx_sampling->prev)) {
                    queue.clear();
                    ext_emb.clear();
                    inp_emb.clear();
                    llama_set_rng_seed(ctx,nseed);
                    n_remain = params.n_predict;
                    DBG("State reloaded from cache due to context overflow. nseed = %d, n_past = %d\n",nseed,n_past);
                    continue;
                }
#endif

                int n_left    = n_past - config.params.n_keep - 1;
                int n_discard = n_left/2;
                DBG("n_past = %d, n_left = %d, n_discard = %d\n",n_past,n_left,n_discard);

                llama_kv_cache_seq_rm(ctx,0,config.params.n_keep + 1,config.params.n_keep + n_discard + 1);
                llama_kv_cache_seq_shift(ctx,0,config.params.n_keep + 1 + n_discard,n_past,-n_discard);

                n_past -= n_discard;
                //state = ANNA_PROCESSING;
                //return;
            }

        } else {
            // context extension via Self-Extend
            while (n_past >= ga_i + ga_w) {
                const int ib = (ga_n*ga_i)/ga_w;
                const int bd = (ga_w/ga_n)*(ga_n - 1);
                const int dd = (ga_w/ga_n) - ib*bd - ga_w;

                DBG("\n");
                DBG("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n", ga_i, n_past, ib*bd, ga_i + ib*bd, n_past + ib*bd);
                DBG("div:   [%6d, %6d] / %6d -> [%6d, %6d]\n", ga_i + ib*bd, ga_i + ib*bd + ga_w, ga_n, (ga_i + ib*bd)/ga_n, (ga_i + ib*bd + ga_w)/ga_n);
                DBG("shift: [%6d, %6d] + %6d -> [%6d, %6d]\n", ga_i + ib*bd + ga_w, n_past + ib*bd, dd, ga_i + ib*bd + ga_w + dd, n_past + ib*bd + dd);

                llama_kv_cache_seq_shift(ctx, 0, ga_i,                n_past,              ib*bd);
                llama_kv_cache_seq_div  (ctx, 0, ga_i + ib*bd,        ga_i + ib*bd + ga_w, ga_n);
                llama_kv_cache_seq_shift(ctx, 0, ga_i + ib*bd + ga_w, n_past + ib*bd,      dd);

                n_past -= bd;

                ga_i += ga_w/ga_n;

                DBG("\nn_past_old = %d, n_past = %d, ga_i = %d\n\n", n_past + bd, n_past, ga_i);
            }
        }

        // token queue
        for (int i = 0; i < (int)queue.size(); i+=config.params.n_batch) {
            int n_eval = (int)queue.size() - i;
            if (n_eval > config.params.n_batch) n_eval = config.params.n_batch;

            //FIXME: DEBUG ONLY
            //for (int i = n_past; i < n_past+n_eval; i++) {
            //    DBG("%05d: %d (%s)\n",i,queue[i],llama_token_to_piece(ctx,queue[i]).c_str());
            //}
            ///
            int r = llama_decode(ctx,llama_batch_get_one(&queue[i],n_eval,n_past,0));
            if (r) {
                string ts = llama_token_to_piece(ctx,queue[i]);
                internal_error = myformat("Failed to eval token %d ('%s') - error %d",queue[i],ts.c_str(),r);
                state = ANNA_ERROR;
                return;
            }
            n_past += n_eval;
        }

        // external embeddings
        for (int i = 0; i < n_ext_emb; i+=config.params.n_batch) {
            int n_eval = n_ext_emb - i;
            if (n_eval > config.params.n_batch) n_eval = config.params.n_batch;

            int r = llama_decode(ctx,batch_embeddings(n_eval,&ext_emb[i*n_embd],n_past));
            if (r) {
                internal_error = myformat("Failed to embed #%d (error %d)",i,r);
                state = ANNA_ERROR;
                return;
            }
            n_past += n_eval;
        }
    }
    queue.clear();
    ext_emb.clear();

    if (!inp_emb.empty()) {
        while ((int)inp_emb.size() > n_consumed) {
            queue.push_back(inp_emb[n_consumed]);
            llama_sampling_accept(ctx_sp,ctx,inp_emb[n_consumed],false);
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
    if (state != ANNA_READY && state != ANNA_TURNOVER) return;
    DBG("GENERATE\n");
    state = ANNA_READY;

    llama_token tok = -1;

    // token enforcement
    if (!forced_start.empty()) {
        tok = forced_start.front();
        forced_start.pop_front();
        DBG("Enforcing token %d...\n",tok);
    }

    // prediction max length reached, force EOS
    if (--n_remain == 0) {
        tok = llama_token_eos(llama_get_model(ctx));
        n_remain = config.params.n_predict; // reset for next round
    }

    // usual sampling
    if (tok < 0) tok = llama_sampling_sample(ctx_sp,ctx,NULL);

    // check if we should stop at new line
    if (config.nl_to_turnover && tok == llama_token_nl(llama_get_model(ctx)))
        state = ANNA_TURNOVER;

    // Deal with EOS token
    if (tok == llama_token_eos(llama_get_model(ctx))) {
        DBG("*** EOS detected ***\n");
        if (config.convert_eos_to_nl) tok = llama_token_nl(llama_get_model(ctx));
        state = ANNA_TURNOVER; // turn over to the user or any other external information supplier
    }

    llama_sampling_accept(ctx_sp,ctx,tok,false);
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

void AnnaBrain::Reset()
{
    llama_kv_cache_seq_rm(ctx,0,0,n_past);

    n_past = 0;
    n_remain = 0;
    n_consumed = 0;
    ga_i = 0;
    queue.clear();
    inp_emb.clear();
    ext_emb.clear();
    forced_start.clear();
    accumulator.clear();

    if (ctx_sp) llama_sampling_free(ctx_sp);
    ctx_sp = llama_sampling_init(config.params);
    state = ANNA_READY;
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
    if (inp.empty()) return;

    auto emb = prompt.empty()? ::llama_tokenize(ctx,inp,true) : ::llama_tokenize(ctx,inp,false,true);
    if (emb.empty()) return;
    if (emb.size() >= llama_n_ctx(ctx)) {
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

    if (prompt.empty()) {
        prompt = emb; // save the first sequence as prompt
        config.params.n_keep = prompt.size();
    }
}

void AnnaBrain::Undo()
{
    ctx_sp->prev = oldcontext;
    queue = oldqueue;
    n_past = old_past;
}

void AnnaBrain::setPrefix(string str)
{
    if (state != ANNA_READY && state != ANNA_TURNOVER) return;

    if (str.empty()) {
        DBG("Token enforcement removed\n");
        forced_start.clear();
        return;
    }

    DBG("Token enforcement: '%s' = ",str.c_str());
    auto tmp = ::llama_tokenize(ctx,str,false,true);
    for (auto &i : tmp) {
        forced_start.push_back(i);
        DBG("%d (%s) ",i,TokenToStr(i));
    }
    DBG(" \n");
}

const char* AnnaBrain::TokenToStr(llama_token token)
{
    piecebuf = llama_token_to_piece(ctx,token);
    return piecebuf.c_str();
}

void AnnaBrain::print_vec(string& str, const vector<llama_token>& vec)
{
    for (auto & i: vec) {
        if (i) str += llama_token_to_piece(ctx,i);
    }
}

string AnnaBrain::PrintContext()
{
    string out;
    print_vec(out,ctx_sp->prev);
    return out;
}

bool AnnaBrain::SaveState(std::string fname)
{
    if (state == ANNA_NOT_INITIALIZED) return false;

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

#ifdef ANNA_USE_MMAP
    int fd = open(fname.c_str(),O_CREAT|O_TRUNC|O_RDWR,00664);
    if (fd < 0) {
        internal_error = myformat("Unable to open file '%s' for writing",fname.c_str());
        return false;
    }

    if (ftruncate(fd,total)) {
        internal_error = myformat("Unable to truncate to %u\n",total);
        return false;
    }
    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_WRITE,MAP_SHARED,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map WR memory for cache buffer (%u bytes)",total);
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

#else
    FILE* f = fopen(fname.c_str(),"wb");
    if (!f) {
        internal_error = myformat("Unable to open file '%s' for writing",fname.c_str());
        return false;
    }

    uint8_t* sbuf = (uint8_t*)malloc(dsize);
    if (!sbuf) {
        internal_error = myformat("Unable to allocate temporary buffer for the state data (%u bytes)",dsize);
        fclose(f);
        return false;
    }
    llama_copy_state_data(ctx,sbuf);
    ctx_sp->prev.resize(llama_n_ctx(ctx)); // make context buffer size constant

    ssize_t np = fwrite(&n_past,sizeof(int),1,f); // 1. n of used tokens
    ssize_t nc = fwrite(ctx_sp->prev.data(),csize,1,f); // 2. context tokens
    ssize_t ns = fwrite(&dsize,4,1,f); // 3. state data size
    ssize_t nd = fwrite(sbuf,dsize,1,f); // 4. state data
    if (np+nc+ns+nd != 4) {
        internal_error = myformat("Data write failed: %lu,%lu,%lu,%lu -> %d\n",np,nc,ns,nd,errno);
        return false;
    }

    free(sbuf);
    fclose(f);
#endif

    DBG("Cache (%u bytes) saved to %s\n",total,fname.c_str());
    return true;
}

bool AnnaBrain::LoadState(std::string fname)
{
    if (state == ANNA_NOT_INITIALIZED) return false;

    uint32_t dsize = llama_get_state_size(ctx);
    uint32_t csize = llama_n_ctx(ctx) * sizeof(llama_token);
    uint32_t total = sizeof(int) + csize + 4 + dsize;

#ifdef ANNA_USE_MMAP
    int fd = open(fname.c_str(),O_RDONLY|O_DIRECT);
    if (fd < 0) {
        internal_error = myformat("Cache file doesn't exist.");
        return false;
    }

    struct stat sb;
    if (fstat(fd,&sb)) {
        internal_error = myformat("Unable to stat() cache buffer (err %d)",errno);
        close(fd);
        return false;
    }
    if (sb.st_size != total) {
        internal_error = myformat("Unexpected cache file size! (expected %u, got %lu)",total,sb.st_size);
        close(fd);
        return false;
    }

    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_READ,MAP_PRIVATE,fd,0);

    close(fd);
    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map RD memory for cache buffer (%u bytes)",total);
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

#else
    FILE* f = fopen(fname.c_str(),"rb");
    if (!f) {
        internal_error = myformat("Cache file doesn't exist.");
        return false;
    }

    uint8_t* sbuf = (uint8_t*)malloc(dsize);
    if (!sbuf) {
        internal_error = myformat("Unable to allocate temporary buffer for the state data (%u bytes)",dsize);
        fclose(f);
        return false;
    }
    ctx_sp->prev.resize(llama_n_ctx(ctx)); // make context buffer size constant

    ssize_t np = fread(&n_past,sizeof(int),1,f); // 1. n of used tokens
    ssize_t nc = fread(ctx_sp->prev.data(),csize,1,f); // 2. context tokens
    ssize_t ns = fread(&dsize,4,1,f); // 3. state data size
    ssize_t nd = fread(sbuf,dsize,1,f); // 4. state data

    if (np+nc+ns+nd == 4) llama_set_state_data(ctx,sbuf);

    free(sbuf);
    fclose(f);

    if (np+nc+ns+nd != 4) {
        internal_error = myformat("Data read failed: %lu,%lu,%lu,%lu -> %d\n",np,nc,ns,nd,errno);
        return false;
    }
#endif

    DBG("Cache (%u bytes) loaded from %s\n",dsize,fname.c_str());
    return true;
}

void AnnaBrain::anna_no_log(ggml_log_level, const char*, void*)
{
    // This is an empty function
}

std::string AnnaBrain::StateToStr(AnnaState s)
{
    if (s < ANNA_NUM_STATES) return states_to_strings[s];
    else return "";
}

llama_batch AnnaBrain::batch_embeddings(int n_tokens, float* embeds, int n_past)
{
    llama_batch r;
    memset(&r,0,sizeof(r));

    r.n_tokens = n_tokens;
    r.embd = embeds;
    r.all_pos_0 = n_past;
    r.all_pos_1 = 1;

    return r;
}

void AnnaBrain::setClipModelFile(std::string fn)
{
    clip_file = fn;
}

bool AnnaBrain::EmbedImage(string imgfile)
{
    if (clip_file.empty() || imgfile.empty()) {
        internal_error = myformat("No image encoder or image path specified");
        return false;
    }

    clip_ctx* ctx_clip = NULL;
    try {
        ctx_clip = clip_model_load(clip_file.c_str(),config.verbose_level);

        // load and preprocess the image
        clip_image_u8 img;
        clip_image_f32 img_res;

        if (!clip_image_load_from_file(imgfile.c_str(),&img)) {
            internal_error = myformat("Unable to load image '%s'",imgfile.c_str());
            clip_free(ctx_clip);
            return false;
        }

        if (!clip_image_preprocess(ctx_clip,&img,&img_res,true)) {
            internal_error = myformat("Unable to preprocess image\n");
            clip_free(ctx_clip);
            return false;
        }

        vector<float> emb;
        emb.resize(clip_n_patches(ctx_clip) * clip_n_mmproj_embd(ctx_clip));
        if (!clip_image_encode(ctx_clip,config.params.n_threads,&img_res,emb.data())) {
            internal_error = myformat("Unable to encode image\n");
            clip_free(ctx_clip);
            return false;
        }

        DBG("Image loaded and encoded\n");
        clip_free(ctx_clip);
        ext_emb.insert(ext_emb.end(),emb.begin(),emb.end());

    } catch (const std::exception & err) {
        internal_error = myformat("Error creating image embeddings: %s\n",err.what());
        if (ctx_clip) clip_free(ctx_clip);
        return false;
    }

    return true;
}
