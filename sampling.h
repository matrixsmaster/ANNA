#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "llama.h"
#include "grammar-parser.h"
#include "dtypes.h"

struct llama_sample_bias {
    llama_token tok;
    int op;
    double val;
};

// general sampler context
// TODO: move to llama.h
struct llama_sampling_context {
    // parameters that will be used for sampling
    llama_sampling_params params;

    // mirostat sampler state
    float mirostat_mu;

    llama_grammar * grammar;

    // internal
    grammar_parser::parse_state parsed_grammar;

    // TODO: replace with ring-buffer
    std::vector<llama_token>      prev;
    std::vector<llama_token_data> cur;

    // logit biases
    std::vector<llama_sample_bias> biases;

    // logit selection history (DEBUG ONLY)
    std::vector<float> logit_sel;
};

// Create a new sampling context instance.
llama_sampling_context * llama_sampling_init(const gpt_params &params);

void llama_sampling_free(struct llama_sampling_context * ctx);

// Reset the sampler context
// - clear prev tokens
// - reset grammar
void llama_sampling_reset(llama_sampling_context * ctx);

// Copy the sampler context
void llama_sampling_cp(llama_sampling_context * src, llama_sampling_context * dst);

// Get the last sampled token
llama_token llama_sampling_last(llama_sampling_context * ctx);

// Get a string representation of the last sampled tokens
std::string llama_sampling_prev_str(llama_sampling_context * ctx_sampling, llama_context * ctx_main, int n);

// Print sampling parameters into a string
std::string llama_sampling_print(const llama_sampling_params & params);

// Print sampling order into a string
std::string llama_sampling_order_print(const llama_sampling_params & params);

// this is a common sampling function used across the examples for convenience
// it can serve as a starting point for implementing your own sampling function
// Note: When using multiple sequences, it is the caller's responsibility to call
//       llama_sampling_reset when a sequence ends
//
// required:
//  - ctx_main:     context to use for sampling
//  - ctx_sampling: sampling-specific context
//
// optional:
//  - ctx_cfg:      context to use for classifier-free guidance
//  - idx:          sample from llama_get_logits_ith(ctx, idx)
//
// returns:
//  - token:      sampled token
//  - candidates: vector of candidate tokens
//
llama_token llama_sampling_sample(
        struct llama_sampling_context * ctx_sampling,
        struct llama_context * ctx_main,
        struct llama_context * ctx_cfg,
        int idx = 0);

void llama_sampling_accept(
        struct llama_sampling_context * ctx_sampling,
        struct llama_context * ctx_main,
        llama_token id,
        bool apply_grammar);
