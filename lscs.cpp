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
        if (i.second.ptr->getState() == ARIA_ERROR) continue;
        if (!i.second.ptr->setInput(inp) && i.second.ptr->getState() == ARIA_ERROR) {
            state = ANNA_ERROR;
            internal_error = myformat("pod %s error: %s",i.first.c_str(),i.second.ptr->getError().c_str());
            break;
        }
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
    state = ANNA_TURNOVER;
    for (auto &i : pods) {
        if (!i.second.ptr) continue;
        if (i.second.ptr->Processing() == ARIA_ERROR) {
            state = ANNA_ERROR;
            internal_error = myformat("pod %s error: %s",i.first.c_str(),i.second.ptr->getError().c_str());
            break;
        }

        string tmp = i.second.ptr->getOutput();
        if (tmp.empty()) continue;
        accumulator += tmp;
        state = ANNA_READY;
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
}

bool AnnaLSCS::ParseConfig()
{
    FILE* f = fopen(config_fn.c_str(),"r");
    if (!f) {
        internal_error = myformat("Failed to open file '%s'",config_fn.c_str());
        return false;
    }

    char key[256],val[2048];
    while (!feof(f)) {
        if (fscanf(f,"%255s = %2047[^\n]",key,val) != 2) break;
        for (size_t i = 0; i < sizeof(key) && key[i]; i++) key[i] = tolower(key[i]);
        cfgmap[key] = val;
        DBG("key/val read: '%s' / '%s'\n",key,val);
    }
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
        if (pod.ptr->getState() != ARIA_LOADED) {
            internal_error = myformat("Unable to start pod: %s",pod.ptr->getError().c_str());
            delete pod.ptr;
            return false;
        }
        pods[pnm] = pod;
    }

    // now check and create connections
    for (int i = 0; i < npods; i++) {
        string pnm = cfgmap[myformat("pod%dname",i)];
        string pconns = cfgmap[myformat("pod%doutputs",i)];

        while (!pconns.empty()) {
            auto pos = pconns.find(' ');
            string to = pconns.substr(0,pos);
            if (pos != string::npos) pconns.erase(0,pos+1);
            else pconns.clear();

            if (!pods.count(to)) {
                internal_error = myformat("Pod %d (%s) trying to connect to '%s', but it's not registered",i,pnm.c_str(),to.c_str());
                return false;
            }

            pods[pnm].outs.push_back(to);
            DBG("Pod %s now outputs to %s\n",pnm.c_str(),to.c_str());
        }
    }

    return true;
}
