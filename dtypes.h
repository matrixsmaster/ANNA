#pragma once

#include "llama.h"
#include <string>
#include <vector>
#include <map>
#include <thread>

#define LLAMA_MAX_FILENAME_LEN 512
#define LLAMA_MAX_PROMPT_LEN (1*1024UL*1024UL)
#define LLAMA_MAX_SAMPLER_SEQ 32
// FIXME: grammars are not used atm, so this field is simply not needed;
// we'll get it back once it'll be required
#define LLAMA_MAX_GRAMMAR_LEN 4

// sampling parameters
struct __attribute__((packed)) llama_sampling_params {
    int32_t     n_prev                = 64;       // number of previous tokens to remember
    int32_t     n_probs               = 0;        // if greater than 0, output the probabilities of top n_probs tokens.
    int32_t     top_k                 = 40;       // <= 0 to use vocab size
    float       top_p                 = 0.95f;    // 1.0 = disabled
    float       min_p                 = 0.05f;    // 0.0 = disabled
    float       tfs_z                 = 1.0f;     // 1.0 = disabled
    float       typical_p             = 1.0f;     // 1.0 = disabled
    float       temp                  = 0.3f;     // <= 0.0 to sample greedily, 0.0 to not output probabilities
    float       dynatemp_range        = 0.0f;     // 0.0 = disabled
    float       dynatemp_exponent     = 1.0f;     // controls how entropy maps to temperature in dynamic temperature sampler
    int32_t     penalty_last_n        = 64;       // last n tokens to penalize (0 = disable penalty, -1 = context size)
    float       penalty_repeat        = 1.1f;     // 1.0 = disabled
    float       penalty_freq          = 0.0f;     // 0.0 = disabled
    float       penalty_present       = 0.0f;     // 0.0 = disabled
    int32_t     mirostat              = 0;        // 0 = disabled, 1 = mirostat, 2 = mirostat 2.0
    float       mirostat_tau          = 5.0f;     // target entropy
    float       mirostat_eta          = 0.1f;     // learning rate
    bool        penalize_nl           = true;     // consider newlines as a repeatable token
    char        samplers_sequence[LLAMA_MAX_SAMPLER_SEQ] = "kfypmt"; // top_k, tail_free, typical_p, top_p, min_p, temp

    char grammar[LLAMA_MAX_GRAMMAR_LEN] = {0};  // optional BNF-like grammar to constrain sampling

    // Classifier-Free Guidance
    // https://arxiv.org/abs/2306.17806
    //std::string cfg_negative_prompt; // string to help guidance
    //float       cfg_scale     = 1.f; // how strong is guidance

    //std::unordered_map<llama_token, float> logit_bias; // logit bias for specific tokens

    //std::vector<llama_token> penalty_prompt_tokens;
    //bool                     use_penalty_prompt_tokens = false;
};

struct __attribute__((packed)) gpt_params {
    float   tensor_split[128]     = {0};   // how split tensors should be distributed across GPUs
    uint32_t seed                 = 0;     // RNG seed
    int32_t n_threads             = std::thread::hardware_concurrency();
    int32_t n_threads_draft       = -1;
    int32_t n_threads_batch       = -1;    // number of threads to use for batch processing (-1 = use n_threads)
    int32_t n_threads_batch_draft = -1;
    int32_t n_predict             = -1;    // new tokens to predict
    int32_t n_ctx                 = 4096;  // context size
    int32_t n_batch               = 512;   // batch size for prompt processing (must be >=32 to use BLAS)
    int32_t n_keep                = 0;     // number of tokens to keep from initial prompt
    int32_t n_draft               = 8;     // number of tokens to draft during speculative decoding
    int32_t n_chunks              = -1;    // max number of chunks to process (-1 = unlimited)
    int32_t n_parallel            = 1;     // number of parallel sequences to decode
    int32_t n_sequences           = 1;     // number of sequences to decode
    float   p_accept              = 0.5f;  // speculative decoding accept probability
    float   p_split               = 0.1f;  // speculative decoding split probability
    int32_t n_gpu_layers          = 0;     // number of layers to store in VRAM (-1 - use default)
    int32_t n_gpu_layers_draft    = -1;    // number of layers to store in VRAM for the draft model (-1 - use default)
    llama_split_mode split_mode   = LLAMA_SPLIT_LAYER; // how to split the model across GPUs
    int32_t main_gpu              = 0;     // the GPU that is used for scratch and small tensors
    int32_t n_beams               = 0;     // if non-zero then use beam search of given width.
    int32_t grp_attn_n            = 1;     // group-attention factor
    int32_t grp_attn_w            = 512;   // group-attention width
    int32_t n_print               = -1;    // print token count every n tokens (-1 = disabled)
    float   rope_freq_base        = 0.0f;  // RoPE base frequency
    float   rope_freq_scale       = 0.0f;  // RoPE frequency scaling factor
    float   yarn_ext_factor       = -1.0f; // YaRN extrapolation mix factor
    float   yarn_attn_factor      = 1.0f;  // YaRN magnitude scaling factor
    float   yarn_beta_fast        = 32.0f; // YaRN low correction dim
    float   yarn_beta_slow        = 1.0f;  // YaRN high correction dim
    int32_t yarn_orig_ctx         = 0;     // YaRN original context length
    int  rope_scaling_type     = LLAMA_ROPE_SCALING_UNSPECIFIED; // TODO: better to be int32_t for alignment
                                                                    //       pinging @cebtenzzre

    // TODO: avoid tuple, use struct
    //std::vector<std::tuple<std::string, float>> lora_adapter; // lora adapter path with user defined scale
    //std::string lora_base  = "";                              // base model path for the lora adapter

    bool mul_mat_q         = true;  // if true, use mul_mat_q kernels instead of cuBLAS
    bool random_prompt     = false; // do not randomize prompt if none provided
    bool use_color         = false; // use color to distinguish generations and inputs
    bool interactive       = false; // interactive mode
    bool chatml            = false; // chatml mode (used for models trained on chatml syntax)
    bool prompt_cache_all  = false; // save user input and generations to prompt cache
    bool prompt_cache_ro   = false; // open the prompt cache read-only and do not update it

    bool embedding         = false; // get only sentence embedding
    bool escape            = false; // escape "\n", "\r", "\t", "\'", "\"", and "\\"
    bool interactive_first = false; // wait for user input immediately
    bool multiline_input   = false; // reverse the usage of `\`
    bool simple_io         = false; // improves compatibility with subprocesses and limited consoles
    bool cont_batching     = false; // insert new sequences for decoding on-the-fly

    bool input_prefix_bos  = false; // prefix BOS to user inputs, preceding input_prefix
    bool ignore_eos        = false; // ignore generated EOS tokens
    bool instruct          = false; // instruction mode (used for Alpaca models)
    bool logits_all        = false; // return logits for all tokens in the batch
    bool use_mmap          = true;  // use mmap for faster loads
    bool use_mlock         = false; // use mlock to keep model in memory
    bool numa              = false; // attempt optimizations that help on some NUMA systems
    bool verbose_prompt    = false; // print prompt tokens before generation
    bool display_prompt    = true;  // print prompt before generation
    bool infill            = false; // use infill mode
    bool dump_kv_cache     = false; // dump the KV cache contents for debugging purposes
    bool no_kv_offload     = false; // disable KV offloading

    ggml_type cache_type_k = GGML_TYPE_F16; // KV cache data type for the K
    ggml_type cache_type_v = GGML_TYPE_F16; // KV cache data type for the V

    llama_sampling_params sparams;

    char model[LLAMA_MAX_FILENAME_LEN] = {0}; // model path
    char prompt[LLAMA_MAX_PROMPT_LEN]  = {0};

    //std::vector<llama_model_kv_override> kv_overrides;
    //gpt_string_params* strings;
};
