#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include "common.h"
#include "llama.h"

#define ERR(X,...) fprintf(stderr, "ERROR: " X "\n", __VA_ARGS__)
#ifndef NDEBUG
    #define DEBUG(...) do { fprintf(stderr,__VA_ARGS__); fflush(stderr); } while (0)
#else
    #define DEBUG(...)
#endif

static std::string load_file(const std::string & fn)
{
    std::string out;
    std::ifstream file(fn);
    if (!file) {
        ERR("Failed to open file '%s'\n",fn.c_str());
        return out;
        //abort();
    }
    std::copy(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>(),back_inserter(out));
    if (out.back() == '\n') out.pop_back();
    DEBUG("File '%s' read: '%s'\n",fn.c_str(),out.c_str());
    return out;
}

static void set_params(gpt_params* p, int argc, char* argv[])
{
    assert(argc > 2);
    p->model = argv[1];
    p->seed = atoi(argv[2]);
    p->prompt.clear();
    if (argc > 3) p->prompt = load_file(argv[3]);
    p->n_threads = 14;
    p->n_predict = -1;
    p->n_ctx = 2048;
    p->top_k = 40;
    p->top_p = 0.8;
    p->temp = 0.7;
    p->repeat_penalty = 1.2;
    p->n_batch = 512;
    p->n_parts = -1;
}

/*
 * Protocol:
 * Newline - end of string. Doesn't neccessarily mean newline itself will be present in the out string.
 * Last symbol before newline changes behavior of this function, and batching in general:
 * \ - Suppress newline, run batch, run sampling
 * $ - Put newline in, run batch, don't run sampling
 * @ - Put newline in, don't run batch or sampling, buffer the input
 * */
static std::string get_input(bool* skip_next)
{
    char cbuf[2];
    std::string s;
    for (;;) {
        ssize_t n = read(0,cbuf,1);
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
    return s;
}

static void printvec(llama_context* ctx, const std::vector<llama_token> & vec)
{
    putchar('\n');
    for (auto & i : vec) {
        if (i) printf("%d (%s) ",i,llama_token_to_str(ctx,i));
    }
    putchar('\n');
}

int main(int argc, char* argv[])
{
    gpt_params params;
    llama_context* ctx;

    set_params(&params,argc,argv);

    auto lparams = llama_context_default_params();
    lparams.n_ctx      = params.n_ctx;
    lparams.n_parts    = params.n_parts;
    lparams.seed       = params.seed;
    lparams.f16_kv     = params.memory_f16;

    ctx = llama_init_from_file(params.model.c_str(),lparams);
    if (!ctx) {
        ERR("Failed to load model '%s'",params.model.c_str());
        return 1;
    }

    int n_ctx = llama_n_ctx(ctx);
    int n_past = 0, old_past = 0;
    int n_remain = params.n_predict;
    int n_consumed = 0;
    bool skip_sampling = false;
    bool no_input = false;

    std::vector<llama_token> queue,prompt,inp_emb;
    std::vector<llama_token> oldqueue,oldcontext;
    std::vector<llama_token> context(n_ctx);
    std::fill(context.begin(),context.end(),0);

    const llama_token tok_newline = ::llama_tokenize(ctx,"\n",false).at(0);

    if (params.prompt.empty())
        skip_sampling = true;
    else {
        params.prompt.insert(0,1,' '); // add space
        inp_emb = ::llama_tokenize(ctx,params.prompt,true);
        prompt = inp_emb; // save first sequence as prompt
        DEBUG("Prompt size: %d tokens, only %d tokens left for a free conversation\n",(int)inp_emb.size(),params.n_ctx-(int)inp_emb.size());
    }

    while (n_remain--) {
        //DEBUG("Main loop: n_remain = %d, embedding size = %ld\n",n_remain,queue.size());

        if (!queue.empty()) {
#if 0
            DEBUG("Context:\n");
            for (auto & tok : queue) DEBUG("%s",llama_token_to_str(ctx,tok));
            DEBUG("\n***\n");
            DEBUG("History:\n");
            for (auto & tok : context) {
                if (tok)
                    //DEBUG("%s",llama_token_to_str(ctx,tok));
                    DEBUG("%d (%s) ",tok,llama_token_to_str(ctx,tok));
            }
            DEBUG("\n***\n");
#endif
            if (n_past + (int)queue.size() > n_ctx) {
                const int n_left = n_past - (int)prompt.size();
                //DEBUG("n_past = %d, prompt = %d, n_left = %d\n",n_past,(int)prompt.size(),n_left);

                std::vector<llama_token> tmp = queue;
                queue = prompt;
                queue.insert(queue.end(),context.end()-n_left/2,context.end());
                n_past = 0; // reset
                DEBUG("\nqueue now = %d, n_left now = %d\n",(int)queue.size(),n_left);

                /*DEBUG("resetting:\n");
                for (int i = 0; i < (int)queue.size(); i++) DEBUG("%s",llama_token_to_str(ctx,queue[i]));
                DEBUG(" \n");*/
            }

            for (int i = 0; i < (int)queue.size(); i+=params.n_batch) {
                int n_eval = (int)queue.size() - i;
                if (n_eval > params.n_batch) n_eval = params.n_batch;
                //DEBUG("Evaluating %d tokens (n_past = %d now), starting from '%s' (%d)...\n",n_eval,n_past,llama_token_to_str(ctx,queue[i]),queue[i]);
#if 0
                DEBUG("Calling eval( ");
                for (int j = 0; j < n_eval; j++) DEBUG("%d (%s) ",queue[i+j],llama_token_to_str(ctx,queue[i+j]));
                DEBUG(")\nn_past = %d, n_threads = %d\n",n_past,params.n_threads);
#endif
                if (llama_eval(ctx,&queue[i],n_eval,n_past,params.n_threads)) {
                    ERR("Failed to eval '%s'",llama_token_to_str(ctx,queue[i]));
                    return 1;
                }
                n_past += n_eval;
            }
        }
        queue.clear();

        // A kludge to replicate same batching as in llama.cpp - do input embedding after finalizing previous local queue
        if (!inp_emb.empty()) {
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

        llama_token tok;
        if (!skip_sampling) {
            tok = llama_sample_top_p_top_k(ctx, context.data() + n_ctx - params.repeat_last_n, params.repeat_last_n, params.top_k, params.top_p, params.temp, params.repeat_penalty);
            //DEBUG("Sampled token %d '%s'\n",tok,llama_token_to_str(ctx,tok));
//#ifdef NDEBUG
            printf("%s",llama_token_to_str(ctx,tok));
//#endif
            fflush(stdout);

            // In OG llama.cpp they pushed eos into global queue before replacing it for newline, and injecting into next embedding sequence
            //context.erase(context.begin());
            //context.push_back(tok);

            if (tok == llama_token_eos()) {
                DEBUG("*** EOS detected, converting to newline\n");
                tok = tok_newline;
            }

            context.erase(context.begin());
            context.push_back(tok);

            queue.push_back(tok);

        } else {
            tok = tok_newline;
            skip_sampling = false;
        }

        if (tok == tok_newline && !no_input) {
nextline:
            DEBUG("Waiting for input (%d tokens consumed so far)\n",n_past);
            std::string inp_str = get_input(&skip_sampling);
            DEBUG("String received: '%s'\n",inp_str.c_str());
            if (inp_str.empty() || inp_str == "\n") continue;
#if 1
            else if (inp_str == "get_context()\n") {
                printvec(ctx,context);
                goto nextline;
            } else if (inp_str == "get_queue()\n") {
                printvec(ctx,queue);
                goto nextline;
            } else if (inp_str == "load_file()\n") {
                do {
                    DEBUG("Enter file name\n");
                    inp_str = get_input(NULL);
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
            }
#endif
            if (params.prompt.empty()) {
                // first input will be considered prompt now
                inp_str.insert(0,1,' '); // add space
                inp_emb = ::llama_tokenize(ctx,inp_str,true);
                params.prompt = inp_str;
                prompt = inp_emb;
            } else
                inp_emb = ::llama_tokenize(ctx,inp_str,false);
            n_consumed = 0;

            oldcontext = context;
            oldqueue = queue;
            old_past = n_past;
        }
    }
    puts(" ");

    llama_free(ctx);
    return 0;
}
