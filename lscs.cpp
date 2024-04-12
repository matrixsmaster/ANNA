#include <stdio.h>
#include <string.h>
#include <string>
#include "lscs.h"

#define ERR(X,...) fprintf(stderr, "[LSCS] ERROR: " X "\n", __VA_ARGS__)

#ifndef NDEBUG
#define DBG(...) do { fprintf(stderr,"[LSCS DBG] " __VA_ARGS__); fflush(stderr); } while (0)
#else
#define DBG(...)
#endif

using namespace std;

AnnaLSCS::AnnaLSCS(std::string cfgfile)
{
    config_fn = cfgfile;
    AnnaLSCS::Reset();
}

AnnaLSCS::~AnnaLSCS()
{
    Clear();
}

AnnaConfig AnnaLSCS::getConfig()
{
    //TODO: make adjustments needed
    return config;
}

void AnnaLSCS::setConfig(const AnnaConfig &cfg)
{
    //TODO: make some sort of config application?
}

void AnnaLSCS::setInput(std::string inp)
{
    for (auto &i : pods) {
        if (!i.second.ptr) continue;
        if (i.second.ptr->getState() != ARIA_READY) continue;
        i.second.ptr->setGlobalInput(inp);
    }
}

bool AnnaLSCS::SaveState(std::string fname, const void *user_data, size_t user_size)
{
    //TODO
    return false;
}

bool AnnaLSCS::LoadState(std::string fname, void *user_data, size_t *user_size)
{
    //TODO
    return false;
}

bool AnnaLSCS::EmbedImage(std::string imgfile)
{
    //TODO
    return false;
}

AnnaState AnnaLSCS::Processing(bool /*skip_sampling*/)
{
    string tmp;
    AnnaState next = ANNA_TURNOVER;

    switch (state) {
    case ANNA_ERROR:
    case ANNA_NOT_INITIALIZED:
        return state;

    case ANNA_READY:
        // prepare and run the pods asynchronously
        for (auto &i : pods) {
            i.second.mark = false;
            if (i.second.ptr) {
                // if pod is immediately available, then we can mark it right now
                if (i.second.ptr->Processing() == ARIA_READY) i.second.mark = true;
                else next = ANNA_PROCESSING; // this means we have more cycles to do in the future
            }
        }
        state = next;
        break;

    case ANNA_PROCESSING:
        // check that all pods have joined their threads
        for (auto &i : pods) {
            if (!i.second.ptr) continue;
            if (i.second.mark) continue; // has already been processed

            switch (i.second.ptr->Processing()) {
            case ARIA_ERROR:
                state = ANNA_ERROR;
                internal_error = myformat("pod %s error: %s",i.first.c_str(),i.second.ptr->getError().c_str());
                return state;

            case ARIA_READY:
                // grab whatever global output which has been produced
                tmp = i.second.ptr->getGlobalOutput();
                if (!tmp.empty()) accumulator += tmp;
                // propagate output signals
                FanOut(i.first);

            case ARIA_RUNNING:
                // normal situation, waiting
                break;

            default:
                // wrong situation, stop
                state = ANNA_ERROR;
                internal_error = myformat("pod %s in in wrong state for processing() call: %d",i.first.c_str(),i.second.ptr->getState());
                return state;
            }
        }
        state = ANNA_READY;
        break;

    default:
        break;
    }

    return state;
}

void AnnaLSCS::Reset()
{
    state = ANNA_ERROR;
    Clear();
    if (!ParseConfig()) return;
    state = ANNA_READY;
}

void AnnaLSCS::Clear()
{
    for (auto &i : pods) {
        if (i.second.ptr) delete i.second.ptr;
    }
    pods.clear();
    cfgmap.clear();
    links.clear();
}

bool AnnaLSCS::ParseConfig()
{
    FILE* f = fopen(config_fn.c_str(),"r");
    if (!f) {
        internal_error = myformat("Failed to open file '%s'",config_fn.c_str());
        return false;
    }

    int r,fsm = 0;
    char key[256],val[2048];
    AriaLink lnk;
    while (!feof(f)) {
        switch (fsm) {
        case 0:
            r = fscanf(f,"%255s = %2047[^\n]",key,val);
            switch (r) {
            case 0: goto endparse;
            case 1:
                if (!strcasecmp(key,"connections")) ++fsm;
                else {
                    internal_error = myformat("Unrecognized section name '%s'",key);
                    fclose(f);
                    return false;
                }
                break;
            case 2:
                for (size_t i = 0; i < sizeof(key) && key[i]; i++) key[i] = tolower(key[i]);
                cfgmap[key] = val;
                DBG("key/val read: '%s' / '%s'\n",key,val);
                break;
            }
            break;

        case 1:
            r = fscanf(f,"%255[^.].%255s",key,val);
            if (r == 2) {
                lnk.from = key;
                lnk.pin_from = atoi(val);
                r = fscanf(f," -> %255[^.].%255s\n",key,val);
                if (r == 2) {
                    lnk.to = key;
                    lnk.pin_to = atoi(val);
                    links[lnk.from].push_back(lnk);
                }
            }
            if (r != 2) {
                internal_error = myformat("Can't parse link data in %s",config_fn.c_str());
                fclose(f);
                return false;
            }
            break;
        }
    }

endparse:
    fclose(f);

    if (cfgmap.empty()) {
        internal_error = myformat("No config loaded from %s",config_fn.c_str());
        return false;
    }

    return CreatePods();
}

bool AnnaLSCS::CreatePods()
{
    int npods = atoi(cfgmap["npods"].c_str());
    if (!npods) {
        internal_error = "No pods have been registered";
        return false;
    }

    // create pods first
    for (int i = 0; i < npods; i++) {
        string pnm = cfgmap[myformat("pod%dname",i)];
        if (pnm.empty()) {
            internal_error = myformat("No name (alias) provided for pod %d",i);
            return false;
        }
        if (pods.count(pnm)) {
            internal_error = myformat("Duplicate pod name for pod %d",i);
            return false;
        }
        string pscr = cfgmap[myformat("pod%dscript",i)];
        if (pscr.empty()) {
            internal_error = myformat("No script provided for pod %d",i);
            return false;
        }

        AriaPod pod;
        pod.ptr = new Aria(pscr,pnm);
        if (pod.ptr->getState() != ARIA_READY) {
            internal_error = myformat("Unable to start pod: %s",pod.ptr->getError().c_str());
            delete pod.ptr;
            return false;
        }
        pods[pnm] = pod;
    }

    // now check the connections
    for (auto &&i : links) {
        if (!pods.count(i.first)) {
            internal_error = myformat("Can't find a pod named %s as a source pod",i.first.c_str());
            return false;
        }
        for (auto &&j : i.second) {
            if (j.pin_from < 0 || j.pin_from >= pods[i.first].ptr->getNumOutPins()) {
                internal_error = myformat("Output pin %d doesn't exist in pod %s",j.pin_from,j.to.c_str());
                return false;
            }
            if (!pods.count(j.to)) {
                internal_error = myformat("Can't find a pod named %s as a target pod",j.to.c_str());
                return false;
            }
            if (j.pin_to < 0 || j.pin_to >= pods[j.to].ptr->getNumOutPins()) {
                internal_error = myformat("Input pin %d doesn't exist in pod %s",j.pin_to,i.first.c_str());
                return false;
            }
        }
    }

    return true;
}

void AnnaLSCS::FanOut(std::string from)
{
    //TODO: send outputs
}
