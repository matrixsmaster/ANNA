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
#include "ggml-cuda.h"

#define ANNA_VERSION "0.3.4b"

#define ERR(X,...) fprintf(stderr, "ERROR: " X "\n", __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

#define DEBUG_TIMING 0

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
    "[-T terminator_string]",
    "[-P](pipeline flag)",
    "[-S](use BOS/EOS tokens flag)",
    "[-I](info flag)",
    "[-N](ignore newline flag)",
    "[-G number_of_layers_to_offload_to_GPU]",
    "[-H path_to_prompt_hub]",
    NULL
};

static bool g_once = false, g_quit = false, g_pipemode = false, g_eos = false, g_info = false, g_nonl = false;
static string g_inbuf, g_tokenf, g_scache, g_uprefix, g_piecebuf, g_outcache, g_reqcache, g_terminator;
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
    p->model.clear();
    p->prompt.clear();
    p->seed = 0;
    p->n_threads = MYLLAMATHREADS;
    p->n_predict = -1;
    p->n_ctx = 4096;
    //p->top_k = 40;
    //p->top_p = 0.8;
    p->temp = -1;
    //p->repeat_penalty = 1.2;
    p->n_batch = 512;

    while ((opt = getopt(argc,argv,"m:s:t:p:f:c:n:e:u:x:r:T:PSING:H:")) != -1) {
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
            p->temp = atof(optarg);
            break;
        case 'u':
            g_uprefix = optarg;
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
        case 'T':
            g_terminator = optarg;
            break;
        case 'P':
            g_pipemode = true;
            break;
        case 'S':
            g_eos = true;
            break;
        case 'I':
            g_info = true;
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
        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (p->model.empty()) return 1;
    if (!p->seed) p->seed = time(NULL);
    DBG("seed = %u\n",p->seed);

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
    DBG("-------------------------\n");
    string s;
    for (auto & i: vec) {
        if (i) s += llama_token_to_str(ctx,i);
    }
    DBG("%s\n",s.c_str());
    DBG("-------------------------\n");
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

static void anna_no_log(llama_log_level level, const char * text, void * user_data)
{
    /* NOTHING TO SEE HERE */
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
    int n_vocab = llama_n_vocab(ctx);
    int n_past = 0, old_past = 0;
    int n_remain = params.n_predict;
    int n_consumed = 0;
    int i_fstart = 0;
    bool skip_sampling = false;
    bool no_input = false;
    bool reload_on_reset = false;

    vector<llama_token> queue,prompt,inp_emb,forced_start;
    vector<llama_token> oldqueue,oldcontext;
    vector<llama_token> context(n_ctx);
    fill(context.begin(),context.end(),0);
    string output_line;
    string full_convo = params.prompt;

    const llama_token tok_newline = llama_token_nl(ctx);
    DBG("Newline token = %d\n",tok_newline);

    if (!g_tokenf.empty()) {
        // Special mode: start token enforcement
        forced_start = ::llama_tokenize(ctx,g_tokenf,false);
        DBG("Token enforcement: '%s' = ",g_tokenf.c_str());
        for (auto &i : forced_start) {
          DBG("%d (%s) ",i,llama_token_to_str(ctx,i));
        }
    }

    if (params.prompt.empty())
        skip_sampling = true;
    else {
        params.prompt.insert(0,1,' '); // add space
        inp_emb = ::llama_tokenize(ctx,params.prompt,true);
        prompt = inp_emb; // save first sequence as prompt
        DBG("Prompt size: %d tokens, only %d tokens left for a free conversation\n",(int)inp_emb.size(),params.n_ctx-(int)inp_emb.size());
        if (inp_emb.back() == tok_newline) {
            DBG("Newline token at the end of the prompt, skipping sampling for the first round...\n");
            skip_sampling = true;
        }
    }

    if (g_info) {
        printf("\nPrompt size: %lu tokens\n",inp_emb.size());
        vector<llama_token> spt;
        for (auto & sp : g_sprompts) {
            spt = ::llama_tokenize(ctx,sp,false);
            printf("Next prompt size: %lu tokens\n",spt.size());
        }
        //g_quit = true;
    }

    bool populate_cache = !g_scache.empty();
    if (populate_cache && load_cache(ctx,n_past,context)) {
        inp_emb.clear();
        populate_cache = false;
        llama_set_rng_seed(ctx,params.seed);
        if (params.prompt.empty()) reload_on_reset = true;
    }

    bool user_turn = skip_sampling;
    bool force_prefix = false;
    while (!g_quit) {
        //DBG("Main loop: n_remain = %d, embedding size = %ld\n",n_remain,queue.size());
        if (!queue.empty()) {
            if (n_past + (int)queue.size() > n_ctx) {
                const int nseed = llama_get_rng_seed(ctx);
                if (reload_on_reset && load_cache(ctx,n_past,context)) {
                    queue.clear();
                    inp_emb.clear();
                    llama_set_rng_seed(ctx,nseed);
                    n_remain = params.n_predict;
                    DBG("State reloaded from cache due to context overflow. nseed = %d, n_past = %d\n",nseed,n_past);
                    continue;
                }

                const int n_left = n_past - (int)prompt.size();
                //DBG("n_past = %d, prompt = %d, n_left = %d\n",n_past,(int)prompt.size(),n_left);

                vector<llama_token> tmp = queue;
                queue = prompt;
                queue.insert(queue.end(),context.end()-n_left/2,context.end());
                n_past = 0; // reset
                DBG("\nqueue now = %d, n_left now = %d\n",(int)queue.size(),n_left);
            }

            for (int i = 0; i < (int)queue.size(); i+=params.n_batch) {
                int n_eval = (int)queue.size() - i;
                if (n_eval > params.n_batch) n_eval = params.n_batch;
#if 0
                DBG("Evaluating %d tokens (n_past = %d now), starting from '%s' (%d)...\n",n_eval,n_past,llama_token_to_str(ctx,queue[i]),queue[i]);
                DBG("Calling eval( ");
                for (int j = 0; j < n_eval; j++) DBG("%d (%s) ",queue[i+j],llama_token_to_str(ctx,queue[i+j]));
                DBG(")\nn_past = %d, n_threads = %d\n",n_past,params.n_threads);
                auto pre_eval = chrono::steady_clock::now();
#elif DEBUG_TIMING
                auto pre_eval = chrono::steady_clock::now();
#endif
                if (llama_eval(ctx,&queue[i],n_eval,n_past,params.n_threads)) {
                    ERR("Failed to eval '%s'",llama_token_to_str(ctx,queue[i]));
                    return 1;
                }
#if DEBUG_TIMING
                auto eval_time = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - pre_eval);
                DBG("Eval time = %lu\n",eval_time.count());
#endif
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
                context.erase(context.begin());
                context.push_back(inp_emb[n_consumed]);
                ++n_consumed;
                if ((int)queue.size() >= params.n_batch) break;
            }

            if (n_consumed >= (int)inp_emb.size()) inp_emb.clear();
            continue;
        }

        // The model is ready now, so we can process one-shot actions
        if (!g_once) {
            g_once = true;
            if (populate_cache) save_cache(ctx,n_past,context);
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
#if DEBUG_TIMING
                auto pre_sample = chrono::steady_clock::now();
#endif
                //tok = llama_sample_top_p_top_k(ctx, context.data() + n_ctx - params.repeat_last_n, params.repeat_last_n, params.top_k, params.top_p, params.temp, params.repeat_penalty);
                float* logits = llama_get_logits(ctx);
                vector<llama_token_data> cand;
                cand.reserve(n_vocab);

                for (llama_token token_id = 0; token_id < n_vocab; token_id++)
                    cand.emplace_back(llama_token_data{token_id,logits[token_id],0.0f});

                llama_token_data_array cand_p = {cand.data(),cand.size(),false};

                if (params.temp <= 0)
                    tok = llama_sample_token_greedy(ctx,&cand_p); // Select it using the "Greedy sampling" method
                else {
                    // FIXME: make it more controllable, or throw it out completely
                    const float mirostat_eta = 0.3f;
                    const float mirostat_tau = 3.0f;
                    float mirostat_mu = 2.0f * mirostat_tau;
                    llama_sample_temperature(ctx,&cand_p,params.temp);
                    tok = llama_sample_token_mirostat_v2(ctx,&cand_p,mirostat_tau,mirostat_eta,&mirostat_mu);
                }
#if DEBUG_TIMING
                auto sample_time = chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - pre_sample);
                DBG("Sample time = %lu\n",sample_time.count());
#endif
                //DBG("Sampled token %d '%s'\n",tok,llama_token_to_str(ctx,tok));
            }

            // Print the next token and always flush the stdout buffer to force the printing
            printf("%s",llama_token_to_str(ctx,tok));
            fflush(stdout);
            full_convo += llama_token_to_str(ctx,tok);

            // In OG llama.cpp they pushed eos into global queue before replacing it for newline, and injecting into next embedding sequence
            //context.erase(context.begin());
            //context.push_back(tok);

            // ... But we do it much better way :)
            if (tok == llama_token_eos(ctx)) {
                DBG("*** EOS detected ***\n");
                if (g_eos) {
                    if (no_input) break; // don't ignore EOS in non-interactive mode if EOS flag is set
                } else
                    tok = tok_newline;
            }

            context.erase(context.begin());
            context.push_back(tok);
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
            if (!g_uprefix.empty() && check_last_piece(g_uprefix)) user_turn = true;
            g_outcache += g_piecebuf;

        } else {
            user_turn = true;
            skip_sampling = false;
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

            if (!g_uprefix.empty() && !full_convo.ends_with(g_uprefix)) {
                printf("%s",g_uprefix.c_str());
                fflush(stdout);
            }

            // Input next string or use next secondary prompt
            string inp_str;
            if (!g_sprompts.empty()) {
                inp_str = g_sprompts.front();
                g_sprompts.pop_front();
            } else
                inp_str = get_input(&skip_sampling);

            DBG("String received: '%s', sampling skip = %d\n",inp_str.c_str(),skip_sampling);
            if (g_pipemode && !skip_sampling) {
                DBG("Going to no-input mode automatically.\n");
                no_input = true; // ignore anything past EOF in pipe mode
            }

            // Rudimentary internal "CLI"
            if (inp_str.empty() || inp_str == "\n") {
                continue;

            } else if (inp_str == "load_file()\n") {
                do {
                    printf("Enter file name\n");
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
                context = oldcontext;
                queue = oldqueue;
                n_past = old_past;
                skip_sampling = true;
                continue;

            } else if (inp_str == "save()\n") {
                printf("Enter file name\n");
                inp_str = get_input(NULL);
                if (!inp_str.empty() && inp_str != "\n") {
                    if (inp_str.back() == '\n') inp_str.pop_back();
                    string tmp = g_scache;
                    g_scache = inp_str;
                    save_cache(ctx,n_past,context);
                    g_scache = tmp;
                }
                skip_sampling = true;
                continue;

            } else if (inp_str == "print_context()\n") {
                print_vec(ctx,context);
                skip_sampling = true;
                continue;

            } else if (inp_str == "print_queue()\n") {
                print_vec(ctx,queue);
                skip_sampling = true;
                continue;
            }

            // don't run on empty
            if (inp_str.empty() || inp_str == "\n") continue;

            if (params.prompt.empty()) {
                // first input will be considered prompt now
                inp_str.insert(0,1,' '); // add space
                inp_emb = ::llama_tokenize(ctx,inp_str,true);
                params.prompt = inp_str;
                prompt = inp_emb;
            } else {
                if (!g_uprefix.empty() && !full_convo.ends_with(g_uprefix))
                    inp_str = g_uprefix + inp_str;
                DBG("Actual string to be tokenized: '%s'\n",inp_str.c_str());
                inp_emb = ::llama_tokenize(ctx,inp_str,false);
            }
            n_consumed = 0;
            n_remain = params.n_predict;
            full_convo += inp_str;

            // save "undo point"
            oldcontext = context;
            oldqueue = queue;
            old_past = n_past;
        }
    }
    puts("");

    // don't forget to free the memory, even though the process is terminating anyway :D
    llama_free(ctx);
    llama_free_model(model);
    llama_backend_free();
    return 0;
}