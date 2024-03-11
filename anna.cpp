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

#define ERR(X,...) fprintf(stderr, "ERROR: " X "\n", __VA_ARGS__)
#define ERRS(...) fprintf(stderr, "ERROR: " __VA_ARGS__)

//#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[DBG] " __VA_ARGS__); fflush(stderr); } while (0)
//#else
//#define DBG(...)
//#endif

#define MAX_INPUT_LEN 2048
#define MAX_INPUT_WAIT_MS 250
#define DEFAULT_CONTEXT 4096
#define DEFAULT_BATCH 512
#define DEFAULT_TEMP 0.4

using namespace std;

struct anna_requester {
    string prefix,suffix;
    string command;
    int fsm, lpos;
    regex bex, eex;
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
    NULL
};

AnnaBrain* brain = nullptr;
bool g_once = false, g_quit = false, g_pipemode = false;
int g_info = 0, g_first = 0;
string g_inbuf, g_tokenf, g_scache, g_piecebuf, g_outcache, g_reqcache, g_terminator, g_vclip;
vector<string> g_uprefix;
deque<string> g_sprompts;
vector<anna_requester> g_requesters;
int g_request_active = 0;
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

    while ((opt = getopt(argc,argv,"m:s:t:p:f:c:n:e:u:x:r:vT:PSNG:F:M:V:i:g:")) != -1) {
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
            cfg.convert_eos_to_nl = false;
            break;
        case 'N':
            cfg.nl_to_turnover = true;
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

/* This is for backward compatibility as OG llama removed the 'const char * llama_token_to_str()' function.
 * Yet another point to abandon upstream completely soon. */
const char* llama_token_to_str(llama_context* ctx, llama_token token)
{
    g_piecebuf = llama_token_to_piece(ctx,token);
    return g_piecebuf.c_str(); // to make pointer available after returning from this stack frame
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

    if (skip_next && g_pipemode && n) *skip_next = true; // always skip sampling until EOF in pipe mode
    return s;
}

bool check_last_piece(string & pattern)
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

string run_request()
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

/* Back-ported from AnnaGraphica */
void default_config(AnnaConfig& cfg)
{
    cfg.convert_eos_to_nl = true;
    cfg.nl_to_turnover = false;
    cfg.verbose_level = 0;

    gpt_params* p = &cfg.params;
    p->seed = 0;
    p->n_threads = thread::hardware_concurrency();
    if (p->n_threads < 1) p->n_threads = 1;
    p->n_predict = -1;
    p->n_ctx = DEFAULT_CONTEXT;
    p->n_batch = DEFAULT_BATCH;
    p->n_gpu_layers = 0;
    p->model[0] = 0;
    p->prompt[0] = 0;
    p->sparams.temp = DEFAULT_TEMP;
}

#if 0
/* Back-ported from AnnaGraphica */
list<string> complete_rqp(const string& in, anna_requester& st)
{
    QStringList res;
    QString arg = st.s->value("args").toString();
    int fsm = 0;
    QString acc;
    for (int i = 0; i < arg.length(); i++) {
        char c = arg[i].toLatin1();
        switch (c) {
        case ' ':
        case '\t':
        case '\"':
            if (!acc.isEmpty() && ((!fsm) || (fsm && c == '\"'))) {
                res.append(acc);
                acc.clear();
            }
            if (c == '\"') fsm = 1 - fsm;
            else if (fsm) acc += arg[i];
            break;
        default:
            acc += arg[i];
        }
    }
    if (!acc.isEmpty()) res.append(acc);

    for (auto & s : res) {
        s = s.replace("%t",in);
        // TODO: other reps
    }
    return res;
}

/* Back-ported from AnnaGraphica */
list<string> detect_rqp(const string& in, anna_requester& st)
{
    if (!st.s) return QStringList();

    st.s->endGroup();
    st.s->beginGroup("MAIN");

    QString start = st.s->value("start_tag").toString();
    QString stop = st.s->value("stop_tag").toString();
    if (start.isEmpty() || stop.isEmpty()) return QStringList();

    bool regex = st.s->value("regex",false).toBool();
    QStringList res;
    int i = -1, l = 0;
    switch (st.fsm) {
    case 0:
        if (regex) {
            st.bex = QRegExp(start);
            st.eex = QRegExp(stop);
            if (st.bex.isValid()) {
                i = st.bex.indexIn(in,st.lpos);
                l = st.bex.matchedLength();
            }
        } else {
            i = in.indexOf(start,st.lpos);
            l = start.length();
        }
        if (i >= 0) {
            st.fsm++;
            st.lpos = i + l;
        }
        break;

    case 1:
        if (regex && st.bex.isValid()) {
            i = st.eex.indexIn(in,st.lpos);
            l = st.eex.matchedLength();
        } else {
            i = in.indexOf(stop,st.lpos);
            l = stop.length();
        }
        if (i >= 0) {
            res = CompleteRQP(in.mid(st.lpos,i-st.lpos),st);
            st.fsm = 0;
            st.lpos = i + l;
        }
        break;
    }

    return res;
}
#endif

/* Back-ported from AnnaGraphica */
bool generate(bool skip, bool force)
{
    AnnaState s = ANNA_NOT_INITIALIZED;
    string str,convo;
    bool stop = false;

    if (force) brain->setPrefix(g_tokenf);

    // main LLM generation loop
    while (!stop && s != ANNA_TURNOVER) {
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
            DBG("str = %s\n",str.c_str());
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
            stop = true;
            break;

        default:
            ERR("Wrong brain state: %s\n",AnnaBrain::StateToStr(s).c_str());
            stop = true;
        }

        // now we can check the RQPs, execute stuff and inject things into LLM, all in this one convenient call
        //CheckRQPs(raw_output + convo);
    }

    //cur_chat += convo + "\n";

}

bool load_cache()
{
    //TODO
}

bool save_cache()
{
    //TODO
}

int main(int argc, char* argv[])
{
    fprintf(stderr,"ANNA version " ANNA_VERSION "\n\n");

    // get CLI arguments
    AnnaConfig cfg;
    default_config(cfg);

    if (argc < 2) {
        usage(argv[0]);
        return -1;
    }
    if (set_params(cfg,argc,argv)) return -1;

    // load the model
    brain = new AnnaBrain(&cfg);
    if (brain->getState() == ANNA_ERROR) {
        ERR("Unable to load AnnaBrain: %s\n",brain->getError().c_str());
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

    if (g_info) {
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

        // FIXME: return auto-cache functionality some day soon
        // The model is ready now, so we can process one-shot actions
        /*if (!g_once) {
            g_once = true;
            if (populate_cache) save_cache(ctx,n_past,ctx_sampling->prev);
        }*/

        if (!generate(skip_sampling,force_prefix)) {
            ERR("Error: %s\n",brain->getError().c_str());
            break;
        }

        if (!no_input) {
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
                if (!g_last_username) {
                    printf("%s",g_uprefix.at(0).c_str());
                    fflush(stdout);
                }
                inp_str = get_input(&skip_sampling,&force_prefix);
            }

            DBG("String received: '%s', sampling skip = %d\n",inp_str.c_str(),skip_sampling);
            if (g_pipemode && !skip_sampling) {
                DBG("Going to no-input mode automatically.\n");
                no_input = true; // ignore anything past EOF in pipe mode
            }

            // Rudimentary internal "CLI"
            if (inp_str.empty() || inp_str == "\n") {
                skip_sampling = false;
                force_prefix = false; // don't force prefix after skipped user input
                continue;

            } else if (inp_str == "load_file()\n") {
                do {
                    printf("Enter text file name\n");
                    inp_str = get_input(NULL,NULL);
                    if (inp_str.empty() || inp_str == "\n") break;
                    if (inp_str.back() == '\n') inp_str.pop_back();
                    inp_str = load_file(inp_str);
                } while (inp_str.empty());

            } else if (inp_str == "no_input()\n") {
                inp_str.clear();
                skip_sampling = false;
                no_input = true;
                continue;

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
                continue;

            } else if (inp_str == "add_user()\n") {
                string uname = get_input(NULL,NULL);
                if (uname.empty() || uname == "\n") continue;
                if (uname.back() == '\n') uname.pop_back();
                g_uprefix.push_back(uname);
                DBG("User prefix '%s' added\n",uname.c_str());
                continue;

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
                continue;

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
                continue;

            } else if (inp_str == "print_context()\n") {
                //brain->get
                continue;

            } else if (inp_str == "stats()\n") {
                // TODO: extend this
                printf("n_past = %d\n",brain->getTokensUsed());
                continue;
            }

            // don't run on empty
            if (inp_str.empty() || inp_str == "\n") continue;

            if (cfg.params.prompt[0] == 0) {
                // first input will be considered prompt now
                strncpy(cfg.params.prompt,inp_str.c_str(),sizeof(cfg.params.prompt)-1);
            } else {
                if (!g_last_username) inp_str = g_uprefix.at(0) + inp_str;
            }

            // apply input
            DBG("Actual input string: '%s'\n",inp_str.c_str());
            brain->setInput(inp_str);
            if (brain->getState() == ANNA_ERROR) {
                ERR("Unable to process input: %s\n",brain->getError().c_str());
                return 11;
            }
        }
    }
    puts("");

    // don't forget to free the memory, even though the process is terminating anyway :D
    delete brain;
    return 0;
}
