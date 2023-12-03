#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "common.h"
#include "llama.h"
#include "clip.h"
#include "ggml-cuda.h"

#define ANNA_VERSION "0.5.2"

#define ERR(X,...) fprintf(stderr, "ERROR: " X "\n", __VA_ARGS__)
#define ERRS(...) fprintf(stderr, "ERROR: " __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

#define MAX_INPUT_LEN 2048
#define MAX_INPUT_WAIT_MS 250

#ifndef MYLLAMATHREADS
#define MYLLAMATHREADS 8
#endif

using namespace std;

struct anna_requester {
    string prefix,suffix;
    string command;
};

static const char* argstrings[] = {
    "<-m model file>",
    "[-s seed]",
    "[-t n_threads]",
    "[-p prompt_file]",
    "[-f forced_token]",
    "[-c cache_file]",
    "[-n n_tokens]",
    "[-e temperature]",
    "[-u user_input_prefix]",
    "[-x context_length]",
    "[-r request_prefix request_suffix request_command]",
    "[-v] (verbose flag)",
    "[-T terminator_string]",
    "[-P] (pipeline flag)",
    "[-S] (use BOS/EOS tokens flag)",
    "[-N] (ignore newline flag)",
    "[-G number_of_layers_to_offload_to_GPU]",
    "[-H path_to_prompt_hub]",
    "[-F AI/User]",
    "[-M mirostat_version]",
    "[-V vision_projector]",
    "[-i image_file]",
    NULL
};

static bool g_once = false, g_quit = false, g_pipemode = false, g_eos = false, g_nonl = false;
static int g_info = 0, g_first = 0;
static string g_inbuf, g_tokenf, g_scache, g_piecebuf, g_outcache, g_reqcache, g_terminator, g_vclip;
static vector<string> g_uprefix;
static deque<string> g_sprompts;
static vector<anna_requester> g_requesters;
static int g_request_active = 0;

static void usage(const char* sname)
{
    fprintf(stderr,"\nUsage: %s ",sname);
    const char** p = argstrings;
    while (*p) fprintf(stderr,"%s ",*p++);
    fprintf(stderr,"\n\n");
}

static string load_file(const string & fn)
{
    string out;
    ifstream file(fn);
    if (!file) {
        ERR("Failed to open file '%s'",fn.c_str());
        return out;
        //abort();
    }
    copy(istreambuf_iterator<char>(file),istreambuf_iterator<char>(),back_inserter(out));
    if (out.back() == '\n') out.pop_back();
    DBG("File '%s' read: '%s'",fn.c_str(),out.c_str());
    return out;
}

static int set_params(gpt_params* p, int argc, char* argv[])
{
    int opt;
    llama_sampling_params* sp = &p->sampling_params;
    p->model.clear();
    p->prompt.clear();
    p->seed = 0;
    p->n_threads = MYLLAMATHREADS;
    p->n_predict = -1;
    p->n_ctx = 4096;
    p->n_batch = 512;

    while ((opt = getopt(argc,argv,"m:s:t:p:f:c:n:e:u:x:r:vT:PSNG:H:F:M:V:i:")) != -1) {
        switch (opt) {
        case 'm':
            p->model = optarg;
            break;
        case 's':
            p->seed = atoi(optarg);
            break;
        case 't':
            p->n_threads = atoi(optarg);
            break;
        case 'p':
            if (p->prompt.empty())
                p->prompt = load_file(optarg);
            else
                g_sprompts.push_back(load_file(optarg));
            break;
        case 'f':
            g_tokenf = optarg;
            break;
        case 'c':
            g_scache = optarg;
            break;
        case 'n':
            p->n_predict = atoi(optarg);
            break;
        case 'e':
            sp->temp = atof(optarg);
            break;
        case 'u':
            g_uprefix.push_back(optarg);
            break;
        case 'x':
            p->n_ctx = atoi(optarg);
            break;
        case 'r':
            {
                anna_requester ar;
                ar.prefix = argv[optind-1];
                ar.suffix = argv[optind++];
                ar.command = argv[optind++];
                g_requesters.push_back(ar);
                DBG("Requester added: ['%s' / '%s'] -> '%s'\n",ar.prefix.c_str(),ar.suffix.c_str(),ar.command.c_str());
            }
            break;
        case 'v':
            g_info++;
            break;
        case 'T':
            g_terminator = optarg;
            break;
        case 'P':
            g_pipemode = true;
            break;
        case 'S':
            g_eos = true;
            break;
        case 'N':
            g_nonl = true;
            break;
        case 'G':
#ifdef LLAMA_SUPPORTS_GPU_OFFLOAD
            p->n_gpu_layers = atoi(optarg);
#endif
            break;
        case 'H':
            // TODO: prompt Hub support
            break;
        case 'F':
            g_first = toupper(optarg[0]);
            break;
        case 'M':
            sp->mirostat = atoi(optarg);
            break;
        case 'V':
            g_vclip = optarg;
            break;
        case 'i':
            g_sprompts.push_back(string("::") + optarg);
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (p->model.empty()) return 1;
    if (!p->seed) p->seed = time(NULL);
    DBG("seed = %u\n",p->seed);

    p->n_threads_batch = p->n_threads;

    return 0;
}

/* This is for backward compatibility as OG llama removed the 'const char * llama_token_to_str()' function.
 * Yet another point to abandon upstream completely soon. */
static const char* llama_token_to_str(llama_context* ctx, llama_token token)
{
    g_piecebuf = llama_token_to_piece(ctx,token);
    return g_piecebuf.c_str(); // to make pointer available after returning from this stack frame
}

/*
 * Protocol:
 * Newline - end of string. Doesn't neccessarily mean newline itself will be present in the out string.
 * Last symbol before newline changes behavior of this function, and batching in general:
 * \ - Suppress newline, run batch, run sampling
 * $ - Put newline in, run batch, don't run sampling
 * @ - Put newline in, don't run batch or sampling, buffer the input
 * */
static string get_input(bool* skip_next)
{
    string s;
    ssize_t n = -1;
    char cbuf[2];

    while (!g_quit) {
        n = read(0,cbuf,1);
        if (n <= 0) break;
        s += *cbuf;
        if (*cbuf == '\n') {
            if (s.length() > 1) {
                switch (*(s.end()-2)) {
                case '\\':
                    s.pop_back();
                    s.pop_back();
                    break;
                case '$':
                    *(s.end()-2) = '\n';
                    s.pop_back();
                    if (skip_next) *skip_next = true;
                    break;
                case '@':
                    *(s.end()-2) = '\n';
                    s.pop_back();
                    return s + get_input(skip_next);
                }
            }
            break;
        }
    }

    if (skip_next && g_pipemode && n) *skip_next = true; // always skip sampling until EOF in pipe mode
    return s;
}

static bool load_cache(llama_context* ctx, int & n_past, vector<llama_token> & context)
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

static void save_cache(llama_context* ctx, int n_past, vector<llama_token> & context)
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

static bool check_last_piece(string & pattern)
{
    if (pattern.empty()) return false;
    // We assume that previous token was just sampled, and g_piecebuf represents its string
    for (size_t i = 1; i <= g_piecebuf.length(); i++) {
        string s = g_outcache + g_piecebuf.substr(0,i);
        //DBG("s='%s'\n",s.c_str());
        if (s.ends_with(pattern)) {
            DBG("Pattern found.\n");
            return true;
        }
    }
    return false;
}

static void print_vec(llama_context* ctx, vector<llama_token> & vec)
{
    printf("-------------------------\n");
    string s;
    for (auto & i: vec) {
        if (i) s += llama_token_to_str(ctx,i);
    }
    printf("%s\n",s.c_str());
    printf("-------------------------\n");
}

static string run_request()
{
    string cmd = g_requesters.at(g_request_active-1).command + " \"" + g_reqcache + "\"";
    FILE* proc = popen(cmd.c_str(),"r");
    if (!proc) {
        ERR("Unable to execute process '%s'\n",cmd.c_str());
        return string();
    }

    int c;
    string res;
    while ((c = fgetc(proc)) != EOF) res += c;

    pclose(proc);
    return res;
}

static void anna_no_log(ggml_log_level level, const char * text, void * user_data)
{
    /* NOTHING TO SEE HERE */
}

static bool prepare_clip_and_image(int n_threads, string imgfile, vector<float> & embs)
{
    if (g_vclip.empty() || imgfile.empty()) {
        ERRS("No image encoder or image path specified\n");
        return false;
    }

    clip_ctx* ctx_clip = NULL;
    try {
        ctx_clip = clip_model_load(g_vclip.c_str(),g_info);

        // load and preprocess the image
        clip_image_u8 img;
        clip_image_f32 img_res;

        if (!clip_image_load_from_file(imgfile.c_str(),&img)) {
            ERR("Unable to load image '%s'",imgfile.c_str());
            clip_free(ctx_clip);
            return false;
        }

        if (!clip_image_preprocess(ctx_clip,&img,&img_res,true)) {
            ERRS("Unable to preprocess image\n");
            clip_free(ctx_clip);
            return false;
        }

        int n_img_pos  = clip_n_patches(ctx_clip);
        int n_img_embd = clip_n_mmproj_embd(ctx_clip);
        embs.resize(clip_n_patches(ctx_clip) * clip_n_mmproj_embd(ctx_clip));
        if (!clip_image_encode(ctx_clip,n_threads,&img_res,embs.data())) {
            ERRS("Unable to encode image\n");
            clip_free(ctx_clip);
            return false;
        }

        DBG("Image loaded and encoded\n");
        clip_free(ctx_clip);

    } catch (const std::exception & err) {
        ERR("Error creating image embeddings: %s\n",err.what());
        if (ctx_clip) clip_free(ctx_clip);
        return false;
    }

    return true;
}

static llama_batch batch_embeddings(int n_tokens, float* embeds, int n_past)
{
    llama_batch r;
    memset(&r,0,sizeof(r));

    r.n_tokens = n_tokens;
    r.embd = embeds;
    r.all_pos_0 = n_past;
    r.all_pos_1 = 1;

    return r;
}

int main(int argc, char* argv[])
{
    fprintf(stderr,"ANNA version " ANNA_VERSION "\n\n");

    // get CLI arguments
    gpt_params params;
    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }
    if (set_params(&params,argc,argv)) return -1;

    // prepare logging if info is requested
    llama_log_set((g_info? NULL:anna_no_log),NULL);
    //cublas_enable_log(g_info);

    // load the model
    llama_model* model;
    llama_context* ctx;
    llama_backend_init(false);
    tie(model,ctx) = llama_init_from_gpt_params(params);
    if (!model) {
        ERR("Failed to load model '%s'",params.model.c_str());
        return 1;
    }

    int n_ctx = llama_n_ctx(ctx);
    int n_vocab = llama_n_vocab(llama_get_model(ctx));
    int n_past = 0, old_past = 0;
    int n_remain = params.n_predict;
    int n_consumed = 0;
    int i_fstart = 0;
    bool skip_sampling = false, was_skipped = false;
    bool no_input = false;
    bool reload_on_reset = false;

    vector<llama_token> queue,prompt,inp_emb,forced_start;
    vector<llama_token> oldqueue,oldcontext;
    vector<float> ext_emb;
    //vector<llama_token> context(n_ctx);
    //fill(context.begin(),context.end(),0);
    string output_line;
    string full_convo = params.prompt;
    llama_sampling_context * ctx_sampling = llama_sampling_init(params);
    llama_adjust_rope_freq(ctx,params.n_ctx);

    const llama_token tok_newline = llama_token_nl(ctx);
    DBG("Newline token = %d\n",tok_newline);

    if (!g_tokenf.empty()) {
        // Special mode: start token enforcement
        forced_start = ::llama_tokenize(ctx,g_tokenf,false,true);
        DBG("Token enforcement: '%s' = ",g_tokenf.c_str());
        for (auto &i : forced_start) {
          DBG("%d (%s) ",i,llama_token_to_str(ctx,i));
        }
    }

    if (params.prompt.empty())
        skip_sampling = true;
    else {
        //params.prompt.insert(0,1,' '); // ~add space~, no needed as tokenizer does it now
        inp_emb = ::llama_tokenize(ctx,params.prompt,true);
        prompt = inp_emb; // save first sequence as prompt
        params.n_keep = prompt.size(); // always keep the prompt
        DBG("Prompt size: %d tokens, only %d tokens left for a free conversation\n",(int)inp_emb.size(),params.n_ctx-(int)inp_emb.size());
        if ((int)inp_emb.size() >= n_ctx) {
            ERR("Too many tokens in prompt: %d tokens in prompt for a %d tokens context window!\n",(int)inp_emb.size(),n_ctx);
            g_quit = true;
        }
        if (inp_emb.back() == tok_newline) {
            DBG("Newline token at the end of the prompt, skipping sampling for the first round...\n");
            skip_sampling = true;
        }
    }

    if (g_info) {
        if (g_info > 1) {
            puts("");
            for (auto &i : inp_emb)
                printf("%s",llama_token_to_str(ctx,i));
            puts("\n");
        }
        printf("\nSeed: %d\n",params.seed);
        printf("Prompt size: %lu tokens\n",inp_emb.size());
        vector<llama_token> spt;
        for (auto & sp : g_sprompts) {
            if (sp.starts_with("::")) continue;
            spt = ::llama_tokenize(ctx,sp,false);
            printf("Next prompt size: %lu tokens\n",spt.size());
        }
        //g_quit = true;
    }

    bool populate_cache = !g_scache.empty();
    if (populate_cache && load_cache(ctx,n_past,ctx_sampling->prev)) {
        inp_emb.clear();
        populate_cache = false;
        llama_set_rng_seed(ctx,params.seed);
        if (params.prompt.empty()) reload_on_reset = true;
    }

    if (g_first) skip_sampling = (g_first != 'A');

    bool user_turn = skip_sampling;
    bool force_prefix = !skip_sampling;
    bool image_served = false;
    while (!g_quit) {
        //DBG("Main loop: n_remain = %d, queue size = %ld\n",n_remain,queue.size());
        if (!queue.empty() || !ext_emb.empty()) {
            int n_embd  = llama_n_embd(llama_get_model(ctx));
            int n_ext_emb = (int)ext_emb.size() / n_embd;

            // check context window
            if (n_past + (int)queue.size() + n_ext_emb > n_ctx) {
                if (!n_past) {
                    ERR("Impossible queue length for the context window size: queue = %lu, ext_emb = %d\n",queue.size(),n_ext_emb);
                    break;
                }
                DBG("Context overflow: n_past = %d, queue = %lu, ext_emb = %d\n",n_past,queue.size(),n_ext_emb);
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

                int n_left    = n_past - params.n_keep - 1;
                int n_discard = n_left/2;
                DBG("n_past = %d, n_left = %d, n_discard = %d\n",n_past,n_left,n_discard);

                llama_kv_cache_seq_rm(ctx,0,params.n_keep + 1,params.n_keep + n_discard + 1);
                llama_kv_cache_seq_shift(ctx,0,params.n_keep + 1 + n_discard,n_past,-n_discard);

                n_past -= n_discard;
                continue;
            }

            // token queue
            for (int i = 0; i < (int)queue.size(); i+=params.n_batch) {
                int n_eval = (int)queue.size() - i;
                if (n_eval > params.n_batch) n_eval = params.n_batch;

                int r = llama_decode(ctx,llama_batch_get_one(&queue[i],n_eval,n_past,0));
                if (r) {
                //if (llama_eval(ctx,&queue[i],n_eval,n_past)) {
                    ERR("Failed to eval token %d ('%s') - error %d",queue[i],llama_token_to_str(ctx,queue[i]),r);
                    g_quit = true;
                    break;
                }
                n_past += n_eval;
            }

            // external embeddings
            for (int i = 0; i < n_ext_emb; i+=params.n_batch) {
                int n_eval = n_ext_emb - i;
                if (n_eval > params.n_batch) n_eval = params.n_batch;

                int r = llama_decode(ctx,batch_embeddings(n_eval,&ext_emb[i*n_embd],n_past));
                if (r) {
                    ERR("Failed to embed #%d (error %d)",i,r);
                    g_quit = true;
                    break;
                }
                n_past += n_eval;
            }
        }
        queue.clear();
        ext_emb.clear();

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

        // The model is ready now, so we can process one-shot actions
        if (!g_once) {
            g_once = true;
            if (populate_cache) save_cache(ctx,n_past,ctx_sampling->prev);
        }

        llama_token tok;
        if (!skip_sampling) {
            tok = -1;

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

            //context.erase(context.begin());
            //context.push_back(tok);
            llama_sampling_accept(ctx_sampling,ctx,tok);
            queue.push_back(tok);

            // Now check for terminating condition or request condition
            if (check_last_piece(g_terminator)) break;
            if (g_request_active) {
                string cursuffix = g_requesters.at(g_request_active-1).suffix;
                if (check_last_piece(cursuffix)) {
                    // remove leftover parts of the suffix
                    int rem = cursuffix.length() - g_piecebuf.length();
                    g_reqcache.erase(g_reqcache.length()-rem,rem);
                    // run request and try to make sense of it
                    string res = "\n" + run_request();
                    if (!res.empty()) {
#if 0
                        inp_emb = ::llama_tokenize(ctx,res,g_eos);
                        if (g_eos) inp_emb.push_back(llama_token_eos(ctx));
#else
                        inp_emb = ::llama_tokenize(ctx,res,false);
#endif
                        printf("\n"); // by this point the model would think a newline was pushed to the output already
                        // don't forget to reset consumption count
                        n_consumed = 0;
                        n_remain = params.n_predict;
                        full_convo += res;
                        DBG("Request result = '%s' (%lu tokens)\n",res.c_str(),inp_emb.size());
                    } else
                        ERR("Request %s returned an empty string!",g_requesters.at(g_request_active-1).command.c_str());
                    g_request_active = 0;
                    g_outcache.clear();
                    continue; // go back so we can process it
                } else
                    g_reqcache += g_piecebuf;

            } else {
                int rc = 0;
                for (auto & rq : g_requesters) {
                    ++rc;
                    if (check_last_piece(rq.prefix)) {
                        g_request_active = rc;
                        g_reqcache.clear();
                        break;
                    }
                }
            }
            if (!g_uprefix.empty())
                for (auto & _i : g_uprefix) {
                    if (check_last_piece(_i)) {
                        user_turn = true;
                        break;
                    }
                }
            g_outcache += g_piecebuf;
            was_skipped = false;

        } else {
            user_turn = true;
            skip_sampling = false;
            //was_skipped = true;
        }

        if (tok == tok_newline || tok == llama_token_eos(ctx))
            g_outcache.clear(); // terminator/request pattern can't include newline or EOS

        // different conditions to turn control back to user (or external input)
        if (g_eos && tok == llama_token_eos(ctx)) user_turn = true;
        if (!g_nonl && tok == tok_newline) user_turn = true;

        if (user_turn && !no_input) {
            force_prefix = true;
            user_turn = false;

            DBG("Waiting for input (%d tokens consumed so far)\n",n_past);

            // Input next string or use next secondary prompt
            string inp_str;
            if (!g_sprompts.empty()) {
                inp_str = g_sprompts.front();
                g_sprompts.pop_front();
                DBG("Using secondary prompt '%s'\n",inp_str.c_str());
                if (inp_str.starts_with("::")) {
                    // image embedding requested
                    inp_str.erase(0,2);
                    DBG("Loading image file '%s'\n",inp_str.c_str());
                    ext_emb.clear();
                    if (!prepare_clip_and_image(params.n_threads,inp_str,ext_emb)) {
                        ERR("Unable to load or convert image file '%s'",inp_str.c_str());
                        inp_str.clear();
                    }
                    skip_sampling = true; // never sample right after the image
                } else if (inp_str.ends_with("\n")) {
                    DBG("Newline token at the end of a secondary prompt, skipping sampling for the first round...\n");
                    skip_sampling = true;
                }
            } else {
                if (g_uprefix.size() == 1 && !full_convo.ends_with(g_uprefix.at(0)) && !was_skipped) {
                    printf("%s",g_uprefix.at(0).c_str());
                    fflush(stdout);
                }
                inp_str = get_input(&skip_sampling);
            }

            DBG("String received: '%s', sampling skip = %d\n",inp_str.c_str(),skip_sampling);
            if (g_pipemode && !skip_sampling) {
                DBG("Going to no-input mode automatically.\n");
                no_input = true; // ignore anything past EOF in pipe mode
            }

            // Rudimentary internal "CLI"
            if (inp_str.empty() || inp_str == "\n") {
                force_prefix = false; // don't force prefix after skipped user input
                continue;

            } else if (inp_str == "load_file()\n") {
                do {
                    printf("Enter text file name\n");
                    inp_str = get_input(NULL);
                    if (inp_str.empty() || inp_str == "\n") break;
                    if (inp_str.back() == '\n') inp_str.pop_back();
                    inp_str = load_file(inp_str);
                } while (inp_str.empty());

            } else if (inp_str == "no_input()\n") {
                inp_str.clear();
                no_input = true;
                continue;

            } else if (inp_str == "undo()\n") {
                // FIXME: these leftovers from the old version with controllable eval() doesn't work anymore
                // need to use llama_kv_cache_seq_rm()
                ctx_sampling->prev = oldcontext;
                queue = oldqueue;
                n_past = old_past;
                skip_sampling = true;
                continue;

            } else if (inp_str == "save()\n" || inp_str == "load\n") {
                bool load = (inp_str[0] == 'l');
                printf("Enter state cache file name\n");
                inp_str = get_input(NULL);
                if (!inp_str.empty() && inp_str != "\n") {
                    if (inp_str.back() == '\n') inp_str.pop_back();
                    string tmp = g_scache;
                    g_scache = inp_str;
                    if (load)
                        if (load_cache(ctx,n_past,ctx_sampling->prev)) {
                            queue.clear();
                            ext_emb.clear();
                            inp_emb.clear();
                            n_remain = params.n_predict;
                        } else
                            ERR("Unable to load state from '%s'\n",g_scache.c_str());
                    else
                        save_cache(ctx,n_past,ctx_sampling->prev);
                    g_scache = tmp;
                }
                skip_sampling = true;
                continue;

            } else if (inp_str == "add_user()\n") {
                string uname = get_input(NULL);
                if (uname.empty() || uname == "\n") continue;
                if (uname.back() == '\n') uname.pop_back();
                g_uprefix.push_back(uname);
                DBG("User prefix '%s' added\n",uname.c_str());
                skip_sampling = true;
                continue;

            } else if (inp_str == "image()\n") {
                if (g_vclip.empty()) {
                    ERRS("Unable to load image file: vision projector file was not specified at startup!\n");
                } else {
                    printf("Enter image file name\n");
                    inp_str = get_input(NULL);
                    if (!inp_str.empty() && inp_str != "\n") {
                        if (inp_str.back() == '\n') inp_str.pop_back();
                        DBG("Loading image file '%s'\n",inp_str.c_str());
                        ext_emb.clear();
                        if (!prepare_clip_and_image(params.n_threads,inp_str,ext_emb))
                            ERR("Unable to load or convert image file '%s'",inp_str.c_str());
                        inp_str.clear();
                    }
                }
                skip_sampling = true;
                continue;

            } else if (inp_str == "print_context()\n") {
                print_vec(ctx,ctx_sampling->prev);
                skip_sampling = true;
                continue;

            } else if (inp_str == "print_queue()\n") {
                print_vec(ctx,queue);
                skip_sampling = true;
                continue;

            } else if (inp_str == "stats()\n") {
                // TODO: extend this
                printf("n_past = %d\n",n_past);
                skip_sampling = true;
                continue;
            }

            // don't run on empty
            if (inp_str.empty() || inp_str == "\n") continue;

            if (params.prompt.empty()) {
                // first input will be considered prompt now
                //inp_str.insert(0,1,' '); // add space
                inp_emb = ::llama_tokenize(ctx,inp_str,true);
                params.prompt = inp_str;
                prompt = inp_emb;
            } else {
                if (g_uprefix.size() == 1 && !full_convo.ends_with(g_uprefix.at(0)) && !was_skipped)
                    inp_str = g_uprefix.at(0) + inp_str;
                DBG("Actual string to be tokenized: '%s'\n",inp_str.c_str());
                inp_emb = ::llama_tokenize(ctx,inp_str,false,true);
            }
            if ((int)inp_emb.size() >= n_ctx) {
                ERR("Too many tokens in input: %d tokens for a %d tokens context window!\n",(int)inp_emb.size(),n_ctx);
                g_quit = true;
            }
            n_consumed = 0;
            n_remain = params.n_predict;
            was_skipped = skip_sampling;
            full_convo += inp_str;

            // save "undo point"
            oldcontext = ctx_sampling->prev;
            oldqueue = queue;
            old_past = n_past;
        }
    }
    puts("");

    // don't forget to free the memory, even though the process is terminating anyway :D
    llama_sampling_free(ctx_sampling);
    llama_free(ctx);
    llama_free_model(model);
    llama_backend_free();
    return 0;
}
