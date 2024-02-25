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

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

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
    if (!config.params.seed) {
        config.params.seed = time(NULL);
        DBG("Setting seed to %u\n",config.params.seed);
    }
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

    // check if we should stop at new line (don't trust newline token, as there may be more than one token representing byte 0x0A)
    if (config.nl_to_turnover && !strcmp(TokenToStr(tok),"\n"))
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
    DBG("Input size: %d tokens\n",(int)inp_emb.size());
    n_consumed = 0;
    n_remain = config.params.n_predict;

    if (prompt.empty()) {
        prompt = emb; // save the first sequence as prompt
        config.params.n_keep = prompt.size();
    }
}

void AnnaBrain::Undo()
{
    // TODO: shift KV cache back
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
    auto tmp = ::llama_tokenize(ctx,str,false,true); //,true);
    for (auto &i : tmp) {
        forced_start.push_back(i);
        DBG("%d (%s) ",i,TokenToStr(i));
    }
    DBG(" \n");
}

void AnnaBrain::addEmbeddings(const std::vector<float>& emb)
{
    ext_emb.insert(ext_emb.end(),emb.begin(),emb.end());
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

bool AnnaBrain::SaveState(std::string fname, const void* user_data, size_t user_size)
{
    if (state == ANNA_NOT_INITIALIZED) return false;

    AnnaSave hdr;
    memset((void*)&hdr,0,sizeof(hdr));
    memcpy(hdr.magic,ANNA_STATE_MAGIC,sizeof(hdr.magic));
    hdr.version = ANNA_STATE_VERSION;
    hdr.cfg = config;
    hdr.n_past = n_past;
    hdr.n_remain = n_remain;
    hdr.n_consumed = n_consumed;
    hdr.ga_i = ga_i;
    hdr.data_size = llama_get_state_size(ctx);
    hdr.vector_size += vector_storage<llama_token>::size(queue);
    hdr.vector_size += vector_storage<llama_token>::size(prompt);
    hdr.vector_size += vector_storage<llama_token>::size(inp_emb);
    hdr.vector_size += vector_storage<float>::size(ext_emb);
    hdr.vector_size += vector_storage<llama_token>::size(forced_start);
    hdr.vector_size += vector_storage<char>::size(accumulator);
    hdr.vector_size += vector_storage<llama_token>::size(ctx_sp->prev);
    hdr.user_size = user_size;
    size_t total = sizeof(hdr) + hdr.data_size + hdr.vector_size + user_size;

#ifdef ANNA_USE_MMAP
    int fd = open(fname.c_str(),O_CREAT|O_TRUNC|O_RDWR,00664);
    if (fd < 0) {
        internal_error = myformat("Unable to open file '%s' for writing: %s",fname.c_str(),strerror(errno));
        return false;
    }

    if (ftruncate(fd,total)) {
        internal_error = myformat("Unable to truncate to %lu bytes\n",total);
        return false;
    }
    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);

    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map WR memory for cache buffer (%u bytes)",total);
        return false;
    }

    // 1. header
    uint8_t* ptr = data;
    memcpy(ptr,&hdr,sizeof(hdr));
    ptr += sizeof(hdr);

    // 2. state data
    llama_copy_state_data(ctx,ptr);
    ptr += hdr.data_size;

    // 3. vectors
    ptr = (uint8_t*)vector_storage<llama_token>::store(queue,ptr);
    ptr = (uint8_t*)vector_storage<llama_token>::store(prompt,ptr);
    ptr = (uint8_t*)vector_storage<llama_token>::store(inp_emb,ptr);
    ptr = (uint8_t*)vector_storage<float>::store(ext_emb,ptr);
    ptr = (uint8_t*)vector_storage<llama_token>::store(vector_storage<llama_token>::from_deque(forced_start),ptr);
    ptr = (uint8_t*)vector_storage<char>::store(vector_storage<char>::from_string(accumulator),ptr);
    ptr = (uint8_t*)vector_storage<llama_token>::store(ctx_sp->prev,ptr);

    // 4. user data
    if (user_data && user_size)
        memcpy(ptr,user_data,user_size);

    // done
    munmap(data,total);

#else
    FILE* f = fopen(fname.c_str(),"wb");
    if (!f) {
        internal_error = myformat("Unable to open file '%s' for writing",fname.c_str());
        return false;
    }

    uint8_t* sbuf = (uint8_t*)malloc(hdr.data_size);
    if (!sbuf) {
        internal_error = myformat("Unable to allocate temporary buffer for the state data (%lu bytes)",hdr.data_size);
        fclose(f);
        return false;
    }
    llama_copy_state_data(ctx,sbuf);

    size_t nh = fwrite(&hdr,sizeof(hdr),1,f); // 1. header
    size_t nd = fwrite(sbuf,hdr.data_size,1,f); // 2. state data
    size_t nv = vector_storage<llama_token>::store(queue,f); // 3. vectors
    nv += vector_storage<llama_token>::store(prompt,f);
    nv += vector_storage<llama_token>::store(inp_emb,f);
    nv += vector_storage<float>::store(ext_emb,f);
    nv += vector_storage<llama_token>::store(vector_storage<llama_token>::from_deque(forced_start),f);
    nv += vector_storage<char>::store(vector_storage<char>::from_string(accumulator),f);
    nv += vector_storage<llama_token>::store(ctx_sp->prev,f);
    size_t nu = (user_data && user_size)? fwrite(user_data,user_size,1,f) : 1; // 4. user data

    free(sbuf);
    fclose(f);

    if (nh+nd+nu != 3 || nv != hdr.vector_size) {
        internal_error = myformat("Data write failed: %lu,%lu,%lu,%lu -> %s\n",nh,nd,nv,nu,strerror(errno));
        return false;
    }
#endif

    DBG("Cache (%lu bytes) saved to %s\n",total,fname.c_str());
    return true;
}

bool AnnaBrain::LoadState(std::string fname, void* user_data, size_t* user_size)
{
    FILE* f = fopen(fname.c_str(),"rb");
    if (!f) {
        internal_error = myformat("Couldn't open cache file %s",fname.c_str());
        return false;
    }

    AnnaSave hdr;
    internal_error.clear();

    if (!fread(&hdr,sizeof(hdr),1,f))
        internal_error = "Couldn't read the header from the cache file";

    if (state == ANNA_NOT_INITIALIZED || !ctx) {
        config = hdr.cfg;
        fclose(f);
        return internal_error.empty(); // we have no context, so this means loading is complete (config acquired)
    }

    size_t dsize = llama_get_state_size(ctx);
    if (internal_error.empty() && strncmp(hdr.magic,ANNA_STATE_MAGIC,sizeof(hdr.magic)))
        internal_error = myformat("Wrong cache file magic ID: expected " ANNA_STATE_MAGIC ", got %4s",hdr.magic);
    if (internal_error.empty() && hdr.data_size != dsize)
        internal_error = myformat("Wrong state data size: expected %lu, got %lu bytes",dsize,hdr.data_size);
    if (internal_error.empty() && hdr.user_size > (user_size? (*user_size):0))
        internal_error = myformat("Unable to load user data: %lu bytes in the file, but can read only %lu bytes",hdr.user_size,user_size);

    size_t total = sizeof(hdr) + hdr.data_size + hdr.vector_size + hdr.user_size;

    fseek(f,0,SEEK_END);
    size_t fsize = ftell(f);
    if (internal_error.empty() && fsize != total)
        internal_error = myformat("Wrong file size: expected %lu, got %lu bytes",total,fsize);

    if (!internal_error.empty()) {
        fclose(f);
        return false;
    }

    if (user_size) *user_size = hdr.user_size;

#ifdef ANNA_USE_MMAP
    fclose(f);
    int fd = open(fname.c_str(),O_RDONLY);
    if (fd < 0) {
        internal_error = myformat("Unable to re-open() the file: %s",strerror(errno));
        return false;
    }

    uint8_t* data = (uint8_t*)mmap(NULL,total,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);

    if (data == MAP_FAILED) {
        internal_error = myformat("Unable to map RD memory for cache buffer (%u bytes)",total);
        return false;
    }

    // 1. skip header
    uint8_t* ptr = data + sizeof(hdr);

    // 2. load state data
    llama_set_state_data(ctx,ptr);
    ptr += hdr.data_size;

    // 3. vectors
    queue = vector_storage<llama_token>::load((void**)&ptr);
    prompt = vector_storage<llama_token>::load((void**)&ptr);
    inp_emb = vector_storage<llama_token>::load((void**)&ptr);
    ext_emb = vector_storage<float>::load((void**)&ptr);
    forced_start = vector_storage<llama_token>::to_deque(vector_storage<llama_token>::load((void**)&ptr));
    accumulator = vector_storage<char>::to_string(vector_storage<char>::load((void**)&ptr));
    ctx_sp->prev = vector_storage<llama_token>::load((void**)&ptr);

    // 4. user data
    if (user_data && hdr.user_size)
        memcpy(user_data,ptr,hdr.user_size);

    // done
    munmap(data,total);

#else
    fseek(f,sizeof(hdr),SEEK_SET);

    uint8_t* sbuf = (uint8_t*)malloc(dsize);
    if (!sbuf) {
        internal_error = myformat("Unable to allocate temporary buffer for the state data (%lu bytes)",dsize);
        fclose(f);
        return false;
    }

    size_t nd = fread(sbuf,dsize,1,f); // 2. state data
    queue = vector_storage<llama_token>::load(f); // 3. vectors
    prompt = vector_storage<llama_token>::load(f);
    inp_emb = vector_storage<llama_token>::load(f);
    ext_emb= vector_storage<float>::load(f);
    forced_start = vector_storage<llama_token>::to_deque(vector_storage<llama_token>::load(f));
    accumulator = vector_storage<char>::to_string(vector_storage<char>::load(f));
    ctx_sp->prev= vector_storage<llama_token>::load(f);
    size_t nu = (user_data && hdr.user_size)? fread(user_data,hdr.user_size,1,f) : 1; // 4. user data

    fclose(f);
    if (nd+nu == 2) llama_set_state_data(ctx,sbuf);
    free(sbuf);

    if (nd+nu != 2) {
        internal_error = myformat("Data read failed: %lu,%lu -> %s\n",nd,nu,strerror(errno));
        return false;
    }
#endif

    config = hdr.cfg;
    n_past = hdr.n_past;
    n_remain = hdr.n_remain;
    n_consumed = hdr.n_consumed;
    ga_i = hdr.ga_i;

    DBG("Cache (%lu bytes) loaded from %s\n",dsize,fname.c_str());
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
        addEmbeddings(emb);

    } catch (const std::exception & err) {
        internal_error = myformat("Error creating image embeddings: %s\n",err.what());
        if (ctx_clip) clip_free(ctx_clip);
        return false;
    }

    return true;
}
