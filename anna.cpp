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
#include <list>
#include <regex>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "brain.h"
#include "lscs.h"
#include "netclient.h"

#define CLI_VERSION "0.9.0"

#define ERR(X,...) fprintf(stderr, "[ANNA] ERROR: " X "\n", __VA_ARGS__)
#define ERRS(...) fprintf(stderr, "[ANNA] ERROR: " __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[ANNA DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

#define MAX_INPUT_LEN 2048
#define MAX_INPUT_WAIT_MS 250
#define MAX_CONVO_LEN (2UL * 1024UL * 1024UL)

using namespace std;

struct anna_requester {
    string prefix, suffix;
    string command;
    int fsm, lpos;
    //bool is_regex;
    //regex bex, eex;
};

const char* argstrings[] = {
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
    "[-F AI/User]",
    "[-M mirostat_version]",
    "[-V vision_projector]",
    "[-i image_file]",
    "[-g group_attn_n:group_attn_w]",
    "[-R server_URL]",
    NULL
};

AnnaBrain* brain = nullptr;
bool g_once = false, g_quit = false, g_pipemode = false;
int g_first = 0;
string g_inbuf, g_tokenf, g_scache, g_terminator, g_vclip, g_raw_output, g_server;
vector<string> g_uprefix;
deque<string> g_sprompts;
vector<anna_requester> g_requesters;
bool g_last_username = false;

void usage(const char* sname)
{
    fprintf(stderr,"\nUsage: %s ",sname);
    const char** p = argstrings;
    while (*p) fprintf(stderr,"%s ",*p++);
    fprintf(stderr,"\n\n");
}

string load_file(const string & fn)
{
    string out;
    ifstream file(fn);
    if (!file) {
        ERR("Failed to open file '%s'",fn.c_str());
        return out;
    }
    copy(istreambuf_iterator<char>(file),istreambuf_iterator<char>(),back_inserter(out));
    if (out.back() == '\n') out.pop_back();
    DBG("File '%s' read: '%s'",fn.c_str(),out.c_str());
    return out;
}

int set_params(AnnaConfig& cfg, int argc, char* argv[])
{
    int opt;
    gpt_params* p = &cfg.params;
    llama_sampling_params* sp = &p->sparams;

    while ((opt = getopt(argc,argv,"m:s:t:p:f:c:n:e:u:x:r:vT:PSNG:F:M:V:i:g:R:")) != -1) {
        switch (opt) {
        case 'm':
            strncpy(p->model,optarg,sizeof(p->model)-1);
            break;
        case 's':
            p->seed = atoi(optarg);
            break;
        case 't':
            p->n_threads = atoi(optarg);
            break;
        case 'p':
            if (!p->prompt[0])
                strncpy(p->prompt,load_file(optarg).c_str(),sizeof(p->prompt)-1);
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
                ar.fsm = 0;
                ar.lpos = 0;
                ar.prefix = argv[optind-1];
                ar.suffix = argv[optind++];
                ar.command = argv[optind++];
                g_requesters.push_back(ar);
                DBG("Requester added: ['%s' / '%s'] -> '%s'\n",ar.prefix.c_str(),ar.suffix.c_str(),ar.command.c_str());
            }
            break;
        case 'v':
            cfg.verbose_level++;
            break;
        case 'T':
            g_terminator = optarg;
            break;
        case 'P':
            g_pipemode = true;
            cfg.nl_to_turnover = false;
            break;
        case 'S':
            cfg.convert_eos_to_nl = false;
            break;
        case 'N':
            cfg.nl_to_turnover = false;
            break;
        case 'G':
            p->n_gpu_layers = atoi(optarg);
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
        case 'g':
            sscanf(optarg,"%d:%d",&p->grp_attn_n,&p->grp_attn_w);
            break;
        case 'R':
            g_server = optarg;
            break;
        default:
            usage(argv[0]);
            return -1;
        }
    }

    if (!p->model[0]) return 1;
    if (!p->seed) p->seed = time(NULL);
    DBG("seed = %u\n",p->seed);

    p->n_threads_batch = p->n_threads;

    return 0;
}

/*
 * Protocol:
 * Newline - end of string. Doesn't neccessarily mean newline itself will be present in the out string.
 * Last symbol before newline changes behavior of this function, and batching in general:
 * \ - Suppress newline, run batch, run sampling, don't force the prefix (if exists)
 * $ - Put newline in, run batch, don't run sampling
 * @ - Put newline in, don't run batch or sampling, buffer the input
 * */
string get_input(bool* skip_next, bool* force_prefix)
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
                    if (skip_next) *skip_next = false;
                    if (force_prefix) *force_prefix = false;
                    break;
                case '$':
                    *(s.end()-2) = '\n';
                    s.pop_back();
                    if (skip_next) *skip_next = true;
                    break;
                case '@':
                    *(s.end()-2) = '\n';
                    s.pop_back();
                    return s + get_input(skip_next,force_prefix);
                }
            }
            break;
        }
    }

    if (g_pipemode) {
        if (n && skip_next) *skip_next = true; // always skip sampling until EOF in pipe mode
        else if (n <= 0 && force_prefix) *force_prefix = true; // add prefix after EOF
    }

    return s;
}

string run_request(string cmd)
{
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

void default_config(AnnaConfig& cfg)
{
    cfg.convert_eos_to_nl = true;
    cfg.nl_to_turnover = true;
    cfg.no_pad_in_prefix = true;
}

/* Back-ported from AnnaGraphica */
list<string> complete_rqp(const string& in, anna_requester& st)
{
    list<string> res;
    int fsm = 0;
    string acc;
    for (int i = 0; i < (int)st.command.length(); i++) {
        char c = st.command[i];
        switch (c) {
        case ' ':
        case '\t':
        case '\"':
            if (!acc.empty() && ((!fsm) || (fsm && c == '\"'))) {
                res.push_back(acc);
                acc.clear();
            }
            if (c == '\"') fsm = 1 - fsm;
            else if (fsm) acc += st.command[i];
            break;
        default:
            acc += st.command[i];
        }
    }
    if (!acc.empty()) res.push_back(acc);

    for (auto & s : res) {
        if (s.find("%t") != string::npos) s.replace(s.find("%t"),2,in);
        // TODO: other reps
    }
    return res;
}

/* Back-ported from AnnaGraphica */
list<string> detect_rqp(const string& in, anna_requester& st)
{
    list<string> res;
    int i = -1, l = 0;
    switch (st.fsm) {
    case 0:
        //if (st.is_regex) {
            //st.bex = QRegExp(start);
            //st.eex = QRegExp(stop);
            //if (st.bex.isValid()) {
                //i = st.bex.indexIn(in,st.lpos);
                //l = st.bex.matchedLength();
            //}
        //} else {
            i = in.find(st.prefix,st.lpos);
            l = st.prefix.length();
        //}
        if (i >= 0) {
            st.fsm++;
            st.lpos = i + l;
        }
        break;

    case 1:
        //if (st.is_regex && st.bex.isValid()) {
            //i = st.eex.indexIn(in,st.lpos);
            //l = st.eex.matchedLength();
        //} else {
            i = in.find(st.suffix,st.lpos);
            l = st.suffix.length();
        //}
        if (i >= 0) {
            res = complete_rqp(in.substr(st.lpos,i-st.lpos),st);
            st.fsm = 0;
            st.lpos = i + l;
        }
        break;
    }

    return res;
}

void check_rqps(string buf)
{
    for (auto &i : g_requesters) {
        auto lst = detect_rqp(buf,i);
        if (lst.empty()) continue;

        string cmd;
        for (auto &j : lst) {
            if (cmd.empty()) cmd = j;
            else cmd += " \"" + j + "\"";
        }
        DBG("RQP compiled: '%s'\n",cmd.c_str());
        string out = run_request(cmd);
        DBG("Output: '%s'\n",out.c_str());
        brain->setInput(out);
        while (brain->Processing(true) == ANNA_PROCESSING) ;
    }
}

/* Back-ported from AnnaGraphica */
bool generate(bool skip, bool force)
{
    AnnaState s = ANNA_NOT_INITIALIZED;
    string str,convo;

    if (force) brain->setPrefix(g_tokenf);
    g_last_username = false;

    // main LLM generation loop
    while (s != ANNA_TURNOVER) {
        s = brain->Processing(skip);

        switch (s) {
        case ANNA_READY:
            if (skip) {
                s = ANNA_TURNOVER;
                break;
            }
            // fall-thru
        case ANNA_TURNOVER:
            str = brain->getOutput();
            //DBG("str = %s\n",str.c_str());
            printf("%s",str.c_str());
            fflush(stdout);
            convo += str;
            for (auto &i : g_uprefix) {
                if (convo.ends_with(i)) {
                    g_last_username = true;
                    s = ANNA_TURNOVER;
                    //convo.erase(convo.length()-i.length(),i.length());
                    break;
                }
            }
            break;

        case ANNA_PROCESSING:
            // nothing to do, waiting
            break;

        case ANNA_ERROR:
            ERR("Error: %s\n",brain->getError().c_str());
            return false;

        default:
            ERR("Wrong brain state: %s\n",AnnaBrain::StateToStr(s).c_str());
            return false;
        }

        string atm = g_raw_output + convo;

        // check for terminate condition
        if (!g_terminator.empty() && atm.ends_with(g_terminator)) {
            DBG("Terminator found, exiting...\n");
            g_quit = true;
            break;
        }

        // now we can check the RQPs, execute stuff and inject things into LLM, all in this one convenient call
        check_rqps(atm);
    }

    g_raw_output += convo;
    return true;
}

bool load_cache()
{
    string tmp(MAX_CONVO_LEN,0);
    size_t len = tmp.size();
    bool ok = brain->LoadState(g_scache,(void*)tmp.data(),&len);
    if (ok) {
        // restore the text history
        tmp.resize(len);
        g_raw_output = std::move(tmp);

        // and don't forget to update RQPs so they won't re-fire
        for (auto &i : g_requesters) {
            i.fsm = 0;
            i.lpos = g_raw_output.length();
        }
        printf("Success!\n");
    } else
        ERR("Unable to load state: %s\n",brain->getError().c_str());

    return ok;
}

bool save_cache()
{
    bool ok = brain->SaveState(g_scache,(void*)g_raw_output.data(),g_raw_output.size());
    if (!ok)
        ERR("Unable to save state: %s\n",brain->getError().c_str());
    else
        printf("Success!\n");
    return ok;
}

string cli(bool& skip_sampling, bool& force_prefix, bool& no_input)
{
    // Receive a string
    string out_str;
    string inp_str = get_input(&skip_sampling,&force_prefix);
    DBG("String received: '%s', sampling skip = %d, force_prefix = %d\n",inp_str.c_str(),skip_sampling,force_prefix);

    // Rudimentary internal "CLI"
    bool old_samp = skip_sampling;
    skip_sampling = true;
    if ((inp_str.empty() || inp_str == "\n") && !g_pipemode) {
        skip_sampling = false;
        force_prefix = false; // don't force prefix after skipped user input

    } else if (inp_str == "load_file()\n") {
        do {
            printf("Enter text file name\n");
            inp_str = get_input(NULL,NULL);
            if (inp_str.empty() || inp_str == "\n") break;
            if (inp_str.back() == '\n') inp_str.pop_back();
            out_str = load_file(inp_str);
        } while (out_str.empty());

    } else if (inp_str == "no_input()\n") {
        inp_str.clear();
        skip_sampling = false;
        no_input = true;

    } else if (inp_str == "save()\n" || inp_str == "load()\n") {
        bool load = (inp_str[0] == 'l');
        printf("Enter state cache file name\n");
        inp_str = get_input(NULL,NULL);
        if (!inp_str.empty() && inp_str != "\n") {
            if (inp_str.back() == '\n') inp_str.pop_back();
            string tmp = g_scache;
            g_scache = inp_str;
            if (load) {
                if (!load_cache())
                    ERR("Unable to load state from '%s'\n",g_scache.c_str());
            } else
                save_cache();
            g_scache = tmp;
        }

    } else if (inp_str == "add_user()\n") {
        string uname = get_input(NULL,NULL);
        if (uname.empty() || uname == "\n") return "";
        if (uname.back() == '\n') uname.pop_back();
        g_uprefix.push_back(uname);
        DBG("User prefix '%s' added\n",uname.c_str());

    } else if (inp_str == "force()\n") {
        inp_str = get_input(NULL,NULL);
        if (inp_str.empty() || inp_str == "\n") {
            g_tokenf.clear();
            DBG("Token enforcement removed\n");
        } else {
            if (inp_str.back() == '\n') inp_str.pop_back();
            g_tokenf = inp_str;
            DBG("Token enforcement: '%s' = ",inp_str.c_str());
        }

    } else if (inp_str == "image()\n") {
        if (g_vclip.empty()) {
            ERRS("Unable to load image file: vision projector file was not specified at startup!\n");
        } else {
            printf("Enter image file name\n");
            inp_str = get_input(NULL,NULL);
            if (!inp_str.empty() && inp_str != "\n") {
                if (inp_str.back() == '\n') inp_str.pop_back();
                DBG("Loading image file '%s'\n",inp_str.c_str());
                if (!brain->EmbedImage(inp_str))
                    ERR("Unable to load or convert image file '%s'",inp_str.c_str());
                inp_str.clear();
            }
        }

    } else if (inp_str == "print_context()\n") {
        printf("\n\n***CONTEXT***\n%s\n\n",brain->PrintContext().c_str());

    } else if (inp_str == "stats()\n") {
        // TODO: extend this
        printf("n_past = %d\n",brain->getTokensUsed());

    } else if (inp_str == "quit()\n") {
        g_quit = true;

    } else {
        skip_sampling = old_samp;
        return inp_str;
    }

    return out_str;
}

int main(int argc, char* argv[])
{
    fprintf(stderr,"ANNA version " ANNA_VERSION "\n");
    fprintf(stderr,"CLI version " CLI_VERSION "\n\n");

    // get CLI arguments
    AnnaConfig cfg;
    default_config(cfg);

    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }
    if (set_params(cfg,argc,argv)) return -1;

    // create new brain, LSCS, or brain connector
    if (g_server.empty()) {
        string fn = cfg.params.model;
        if (fn.ends_with(".lscs")) brain = new AnnaLSCS(fn);
        else brain = new AnnaBrain(&cfg);
    } else
        brain = dynamic_cast<AnnaBrain*>(new AnnaClient(&cfg,g_server,false,nullptr));

    if (!brain || brain->getState() == ANNA_ERROR) {
        ERR("Unable to create brain: %s\n",brain->getError().c_str());
        return 10;
    }

    // process the prompt
    brain->setInput(cfg.params.prompt);
    while (brain->Processing(true) == ANNA_PROCESSING) ;
    if (brain->getState() == ANNA_ERROR) {
        ERR("Unable to process the prompt: %s\n",brain->getError().c_str());
        return 11;
    }

    // decide who's turn the first and whether to skip sampling
    bool skip_sampling = false;
    if (cfg.params.prompt[0] == 0)
        skip_sampling = true;
    else if (cfg.params.prompt[strlen(cfg.params.prompt)-1] == '\n') {
        DBG("Newline token at the end of the prompt, skipping sampling for the first round...\n");
        skip_sampling = true;
    }
    if (g_first) skip_sampling = (g_first != 'A');
    bool force_prefix = !skip_sampling;
    bool no_input = false;

    if (cfg.verbose_level) {
        printf("\nSeed: %d\n",brain->getConfig().params.seed);
        printf("Prompt size: %d tokens\n",brain->getTokensUsed());
        // TODO more info?
    }

    // FIXME: return auto-cache functionality some day soon
    /*bool populate_cache = !g_scache.empty();
    if (populate_cache && load_cache(ctx,n_past,ctx_sampling->prev)) {
        inp_emb.clear();
        populate_cache = false;
        llama_set_rng_seed(ctx,params.seed);
        if (params.prompt[0] == 0) reload_on_reset = true;
    }*/

    while (!g_quit) {
        DBG("loop start: skip=%d; force=%d; noin=%d\n",skip_sampling,force_prefix,no_input);

        // FIXME: return auto-cache functionality some day soon
        // The model is ready now, so we can process one-shot actions
        /*if (!g_once) {
            g_once = true;
            if (populate_cache) save_cache(ctx,n_past,ctx_sampling->prev);
        }*/

        size_t pre_gen = g_raw_output.length();
        if (!generate(skip_sampling,force_prefix)) {
            ERR("Error: %s\n",brain->getError().c_str());
            g_quit = true;
            break;
        }

        if (no_input) {
            if (g_raw_output.length() == pre_gen) {
                DBG("Nothing has been generated, shutting down...\n");
                g_quit = true;
                break;
            } else
                continue;
        }

        skip_sampling = true;
        DBG("Waiting for input (%d tokens consumed so far)\n",brain->getTokensUsed());

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
                if (!brain->EmbedImage(inp_str))
                    ERR("Unable to load or convert image file '%s'",inp_str.c_str());
                inp_str.clear();

            } else if (inp_str.ends_with("\n")) {
                DBG("Newline token at the end of a secondary prompt, skipping sampling for the first round...\n");
            }
        } else {
            if (!g_last_username && g_uprefix.size() == 1) {
                printf("%s",g_uprefix.at(0).c_str());
                fflush(stdout);
            }
            skip_sampling = false;
            force_prefix = true;
        }

        inp_str = cli(skip_sampling,force_prefix,no_input);

        if (g_pipemode && !skip_sampling) {
            DBG("Going to no-input mode automatically.\n");
            no_input = true; // ignore anything past EOF in pipe mode
        }

        // don't run on empty
        if (inp_str.empty() || inp_str == "\n") continue;

        if (cfg.params.prompt[0] == 0) {
            // first input will be considered prompt now
            strncpy(cfg.params.prompt,inp_str.c_str(),sizeof(cfg.params.prompt)-1);
        } else {
            if (!g_last_username && g_uprefix.size() == 1)
                inp_str = g_uprefix.at(0) + inp_str;
        }

        // apply input
        DBG("Actual input string: '%s'\n",inp_str.c_str());
        brain->setInput(inp_str);
        if (brain->getState() == ANNA_ERROR) {
            ERR("Unable to process input: %s\n",brain->getError().c_str());
            return 11;
        }
    }
    puts("");

    // don't forget to free the memory, even though the process is terminating anyway :D
    delete brain;
    return 0;
}
