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

void set_params(gpt_params* p, int argc, char* argv[])
{
    assert(argc > 3);
    p->model = argv[1];
    p->seed = atoi(argv[2]);
    p->n_threads = 8;
    p->n_predict = -1;
    p->n_ctx = 2048;
    p->top_k = 40;
    p->top_p = 0.8;
    p->temp = 0.7;
    p->repeat_penalty = 1.2;
    p->n_batch = 512;
    p->n_parts = -1;

    std::ifstream file(argv[3]);
    if (!file) {
        ERR("Failed to open file '%s'\n",argv[3]);
        abort();
    }
    std::copy(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>(),back_inserter(p->prompt));
    if (p->prompt.back() == '\n') p->prompt.pop_back();
}

/*
 * Protocol:
 * Newline - end of string. Doesn't neccessarily mean newline itself will be present in the out string.
 * Last symbol before newline changes behavior of this function, and batching in general:
 * \ - Suppress newline, run batch, run sampling
 * $ - Put newline in, run batch, don't run sampling
 * @ - Put newline in, don't run batch or sampling, buffer the input
 * */
std::string get_input(bool* skip_next)
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

void printvec(llama_context* ctx, const std::vector<llama_token> & vec)
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

    std::mt19937 rng(params.seed);

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

    params.prompt.insert(0,1,' '); // add space
    auto inp_emb = ::llama_tokenize(ctx,params.prompt,true);
    auto prompt = inp_emb; // save first sequence as prompt

    int n_ctx = llama_n_ctx(ctx);
    int n_past = 0;
    int n_remain = params.n_predict;
    int n_consumed = 0;
    bool skip_sampling = false;

    std::vector<llama_token> context;
    std::vector<llama_token> history(n_ctx);
    std::fill(history.begin(),history.end(),0);

    const llama_token tok_newline = ::llama_tokenize(ctx,"\n",false).at(0);

    while (n_remain--) {
        //DEBUG("Main loop: n_remain = %d, embedding size = %ld\n",n_remain,context.size());

        if (!context.empty()) {
#if 0
            DEBUG("Context:\n");
            for (auto & tok : context) DEBUG("%s",llama_token_to_str(ctx,tok));
            DEBUG("\n***\n");
            DEBUG("History:\n");
            for (auto & tok : history) {
                if (tok)
                    //DEBUG("%s",llama_token_to_str(ctx,tok));
                    DEBUG("%d (%s) ",tok,llama_token_to_str(ctx,tok));
            }
            DEBUG("\n***\n");
#endif
            if (n_past + (int)context.size() > n_ctx) {
                const int n_left = n_past - (int)prompt.size();
                //DEBUG("n_past = %d, prompt = %d, n_left = %d\n",n_past,(int)prompt.size(),n_left);

                std::vector<llama_token> tmp = context;
                context = prompt;
                context.insert(context.end(),history.end()-n_left/2,history.end());
                n_past = 0; // reset
                //DEBUG("context now = %d, n_left now = %d\n",(int)context.size(),n_left);

                /*DEBUG("resetting:\n");
                for (int i = 0; i < (int)context.size(); i++) DEBUG("%s",llama_token_to_str(ctx,context[i]));
                DEBUG(" \n");*/
            }

            for (int i = 0; i < (int)context.size(); i+=params.n_batch) {
                int n_eval = (int)context.size() - i;
                if (n_eval > params.n_batch) n_eval = params.n_batch;
                //DEBUG("Evaluating %d tokens (n_past = %d now), starting from '%s' (%d)...\n",n_eval,n_past,llama_token_to_str(ctx,context[i]),context[i]);
#if 0
                DEBUG("Calling eval( ");
                for (int j = 0; j < n_eval; j++) DEBUG("%d (%s) ",context[i+j],llama_token_to_str(ctx,context[i+j]));
                DEBUG(")\nn_past = %d, n_threads = %d\n",n_past,params.n_threads);
#endif
                if (llama_eval(ctx,&context[i],n_eval,n_past,params.n_threads)) {
                    ERR("Failed to eval '%s'",llama_token_to_str(ctx,context[i]));
                    return 1;
                }
                n_past += n_eval;
            }
        }
        context.clear();

        // A kludge to replicate same batching as in llama.cpp - do input embedding after finalizing previous local context
        if (!inp_emb.empty()) {
            while ((int)inp_emb.size() > n_consumed) {
                context.push_back(inp_emb[n_consumed]);
                history.erase(history.begin());
                history.push_back(inp_emb[n_consumed]);
                ++n_consumed;
                if ((int)context.size() >= params.n_batch) break;
            }

            if (n_consumed >= (int)inp_emb.size()) inp_emb.clear();
            continue;
        }

        llama_token tok;
        if (!skip_sampling) {
            tok = llama_sample_top_p_top_k(ctx, history.data() + n_ctx - params.repeat_last_n, params.repeat_last_n, params.top_k, params.top_p, params.temp, params.repeat_penalty);
            //DEBUG("Sampled token %d '%s'\n",tok,llama_token_to_str(ctx,tok));
//#ifdef NDEBUG
            printf("%s",llama_token_to_str(ctx,tok));
//#endif
            fflush(stdout);

            // In OG llama.cpp they pushed eos into global context before replacing it for newline, and injecting into next embedding sequence
            history.erase(history.begin());
            history.push_back(tok);

            if (tok == llama_token_eos()) {
                DEBUG("*** EOS detected, converting to newline\n");
                tok = tok_newline;
            }

            context.push_back(tok);

        } else {
            tok = tok_newline;
            skip_sampling = false;
        }

        if (tok == tok_newline) {
nextline:
            DEBUG("Waiting for input\n");
            std::string inp_str = get_input(&skip_sampling);
            DEBUG("String received: '%s'\n",inp_str.c_str());
            if (inp_str == "get_history()\n") {
                printvec(ctx,history);
                goto nextline;
            } else if (inp_str == "get_context()\n") {
                printvec(ctx,context);
                goto nextline;
            }
            //inp_str.insert(0,1,' '); // add space
            inp_emb = ::llama_tokenize(ctx,inp_str,false);
            n_consumed = 0;
        }
    }
    puts(" ");

    llama_free(ctx);
    return 0;
}
