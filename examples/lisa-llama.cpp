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
    //#define DEBUG(...)
    #define DEBUG(...) do { fprintf(stderr,__VA_ARGS__); fflush(stderr); } while (0)
#endif

void set_params(gpt_params* p, int argc, char* argv[])
{
    assert(argc > 2);
    p->model = argv[1];
    p->seed = atoi(argv[2]);
    p->n_threads = 8;
    p->n_predict = 64; // FIXME
    p->n_ctx = 32; //2048;
    p->top_k = 40;
    p->top_p = 0.8;
    p->temp = 0.7;
    p->repeat_penalty = 1.2;
    p->n_batch = 16;
    p->n_parts = -1;
}

std::string get_input()
{
    char cbuf[2];
    std::string s;
    for (;;) {
        ssize_t n = read(0,cbuf,1);
        if (n <= 0) break;
        //s += *cbuf;
        if (*cbuf == '\n') break;
        s += *cbuf;
    }
    return s;
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

    DEBUG("Waiting for first prompt\n");
    std::string inp_str = get_input();
    DEBUG("String received: '%s'\n",inp_str.c_str());
    inp_str.insert(0,1,' '); // add space
    auto inp_emb = ::llama_tokenize(ctx,inp_str,true);

    int n_ctx = llama_n_ctx(ctx);
    int n_past = 0;
    int n_remain = params.n_predict;
    int n_consumed = 0;

    std::vector<llama_token> context;
    std::vector<llama_token> history(n_ctx);
    std::fill(history.begin(),history.end(),0);

    while ((int)inp_emb.size() > n_consumed) {
        context.push_back(inp_emb[n_consumed]);
        history.erase(history.begin());
        history.push_back(inp_emb[n_consumed]);
        ++n_consumed;
        if ((int)context.size() >= params.n_batch) break;
    }

    DEBUG("Context:");
    for (auto tok : context) DEBUG("%s",llama_token_to_str(ctx,tok));

    while (n_remain--) {
        DEBUG("Main loop: n_remain = %d, embedding size = %ld\n",n_remain,context.size());
        if ((!context.empty()) && (n_past + (int)context.size() > n_ctx)) {
            const int n_left = n_past - (int)inp_emb.size();
            DEBUG("n_past = %d, inp_emb = %d, n_left = %d\n",n_past,(int)inp_emb.size(),n_left);
            //n_past = inp_emb.size();

            // insert n_left/2 tokens at the start of embd from last_n_tokens
            //context.insert(context.begin(),(history.begin() + n_ctx - n_left/2 - context.size()),history.end()-context.size());
            // WRONG! that action overwrites the prompt.

            std::vector<llama_token> tmp = context;
            context = inp_emb;
            context.insert(context.end(),history.end()-n_left/2,history.end());
            n_past = 0;//context.size();
            DEBUG("context now = %d, n_left now = %d\n",(int)context.size(),n_left);

            printf("\n---\n");
            printf("resetting: '");
            for (int i = 0; i < (int)context.size(); i++) {
                printf("%s", llama_token_to_str(ctx,context[i]));
            }
            printf("'\n");
            printf("\n---\n");
        }

        for (int i = 0; i < (int)context.size(); i+=params.n_batch) {
            int n_eval = (int)context.size() - i;
            if (n_eval > params.n_batch) n_eval = params.n_batch;
            DEBUG("Evaluating %d tokens (n_past = %d now), starting from '%s' (%d)...\n",n_eval,n_past,llama_token_to_str(ctx,context[i]),context[i]);
            if (llama_eval(ctx,&context[i],n_eval,n_past,params.n_threads)) {
                ERR("failed to eval '%s'",llama_token_to_str(ctx,context[i]));
                return 1;
            }
            n_past += n_eval;
        }
        context.clear();

        auto tok = llama_sample_top_p_top_k(ctx, history.data() + n_ctx - params.repeat_last_n, params.repeat_last_n, params.top_k, params.top_p, params.temp, params.repeat_penalty);
        DEBUG("Sampled token %d '%s'\n",tok,llama_token_to_str(ctx,tok));
#ifdef NDEBUG
        printf("%s",llama_token_to_str(ctx,tok));
#endif
        fflush(stdout);

        history.erase(history.begin());
        history.push_back(tok);
        context.push_back(tok);
    }
    puts(" ");

    llama_free(ctx);
    return 0;
}
