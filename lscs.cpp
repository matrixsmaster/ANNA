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

AnnaLSCS::AnnaLSCS()
{
    state = ANNA_NOT_INITIALIZED;
}

AnnaLSCS::AnnaLSCS(string cfgfile)
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

void AnnaLSCS::setInput(string inp)
{
    for (auto &i : pods) {
        if (!i.second.ptr) continue;
        if (i.second.ptr->getState() != ARIA_READY) continue;
        i.second.ptr->setGlobalInput(inp);
    }
}

bool AnnaLSCS::SaveState(string fname, const void *user_data, size_t user_size)
{
    //TODO
    return false;
}

bool AnnaLSCS::LoadState(string fname, void *user_data, size_t *user_size)
{
    //TODO
    return false;
}

bool AnnaLSCS::EmbedImage(string imgfile)
{
    bool r = false;
    for (auto &i : pods) {
        if (!i.second.ptr) continue;
        if (i.second.ptr->getState() != ARIA_READY) continue;
        if (i.second.ptr->setUserImage(imgfile)) r = true;
    }
    return r;
}

AnnaState AnnaLSCS::Processing(bool /*skip_sampling*/)
{
    string tmp;
    AnnaState next = ANNA_TURNOVER;

    switch (state) {
    case ANNA_ERROR:
    case ANNA_NOT_INITIALIZED:
        return state;

    case ANNA_TURNOVER:
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
                i.second.mark = true;
                // grab whatever global output which has been produced
                tmp = i.second.ptr->getGlobalOutput();
                if (!tmp.empty()) accumulator += tmp;
                // propagate output signals
                FanOut(i.first);
                break;

            case ARIA_RUNNING:
                // normal situation, processing
                next = ANNA_PROCESSING;
                break;

            default:
                // wrong situation, stop
                state = ANNA_ERROR;
                internal_error = myformat("pod %s in in wrong state for processing() call: %d",i.first.c_str(),i.second.ptr->getState());
                return state;
            }
        }
        state = next;
        break;

    default:
        break;
    }

    return state;
}

void AnnaLSCS::Reset(int /*flags*/)
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

AriaPod* AnnaLSCS::addPod(string name)
{
    if (pods.count(name)) {
        internal_error = myformat("Aria pod named %s already exists!",name.c_str());
        return nullptr;
    }
    AriaPod npod;
    pods[name] = npod;
    return &(pods[name]);
}

AriaPod* AnnaLSCS::getPod(string name)
{
    if (!pods.count(name)) {
        internal_error = myformat("No Aria pod named %s",name.c_str());
        return nullptr;
    } else
        return &(pods[name]);
}

void AnnaLSCS::delPod(string name)
{
    if (!pods.count(name)) {
        internal_error = myformat("No Aria pod named %s",name.c_str());
        return;
    }

    AriaPod npod = pods[name];
    pods.erase(name);
    if (npod.ptr) delete npod.ptr;
}

void AnnaLSCS::delPod(AriaPod* pod)
{
    for (auto it = pods.begin(); it != pods.end();) {
        if (&(it->second) == pod) it = pods.erase(it);
        else ++it;
    }
}

string AnnaLSCS::getPodName(AriaPod* pod)
{
    for (auto &&i : pods) {
        if (&(i.second) == pod) return i.first;
    }
    return "";
}

void AnnaLSCS::setPodName(AriaPod* pod, string nname)
{
    if (!pod || !pod->ptr) return;

    AriaPod old = *pod;
    string oname = old.ptr->getName();

    // rename the pod internally and in the map
    old.ptr->setName(nname);
    pods.erase(oname);
    pods[nname] = old;

    // update links by replacing old name with the new one
    for (auto it = links.begin(); it != links.end();) {
        for (auto & j : it->second) {
            if (j.from == oname) j.from = nname;
            if (j.to == oname) j.to = nname;
        }
        if (it->first == oname) {
            auto oldvec = it->second;
            it = links.erase(it);
            links[nname] = oldvec;
        } else
            ++it;
    }
}

list<string> AnnaLSCS::getPods()
{
    list<string> res;
    for (auto &&i : pods) res.push_back(i.first);
    return res;
}

bool AnnaLSCS::setPodScript(string name, string path)
{
    if (!pods.count(name)) {
        internal_error = myformat("No Aria pod named %s",name.c_str());
        return false;
    }

    if (pods[name].ptr) {
        // remove old Aria first
        delete pods[name].ptr;
        pods[name].ptr = nullptr;
    }

    Aria* ptr = new Aria(path,name);
    if (ptr->getState() != ARIA_READY) {
        internal_error = myformat("Aria error: %s",ptr->getError().c_str());
        delete ptr;
        return false;
    }

    pods[name].ptr = ptr;
    return true;
}

bool AnnaLSCS::Link(AriaLink lnk)
{
    // check the link is possible
    if (lnk.from.empty() || lnk.to.empty()) return false;
    if (lnk.pin_from < 0 || lnk.pin_to < 0) return false;
    if (!pods.count(lnk.from) || !pods.count(lnk.to)) return false;

    Aria* from = pods[lnk.from].ptr;
    Aria* to = pods[lnk.to].ptr;
    if (!from || !to) return false;
    if (lnk.pin_from >= from->getNumOutPins() || lnk.pin_to >= to->getNumInPins()) return false;

    // make the link
    links[lnk.from].push_back(lnk);
    return true;
}

bool AnnaLSCS::Unlink(AriaLink lnk)
{
    if (lnk.from.empty() || lnk.to.empty()) return false;
    if (lnk.pin_from < 0 || lnk.pin_to < 0) return false;
    if (!links.count(lnk.from)) return false;

    for (auto it = links.at(lnk.from).begin(); it != links.at(lnk.from).end(); ++it) {
        if (it->pin_from == lnk.pin_from && it->to == lnk.to && it->pin_to == lnk.pin_to) {
            links[lnk.from].erase(it);
            return true;
        }
    }
    return false;
}

vector<AriaLink> AnnaLSCS::getLinksFrom(string name)
{
    if (!links.count(name)) return vector<AriaLink>();
    return links[name];
}

void AnnaLSCS::SanitizeLinks()
{
    for (auto it = links.begin(); it != links.end();) {
        if (!pods.count(it->first)) {
            DBG("Links sanitizer: branch is dead (%s doesn't exist)",it->first.c_str());
            it = links.erase(it);
            continue;
        }
        for (auto jt = it->second.begin(); jt != it->second.end();) {
            if ((!pods.count(jt->from)) || (!pods.count(jt->to))) {
                DBG("Links sanitizer: link is dead (%s or %s)",jt->from.c_str(),jt->to.c_str());
                jt = it->second.erase(jt);
                continue;
            }
            if (pods[jt->from].ptr) {
                if (jt->pin_from >= pods[jt->from].ptr->getNumOutPins()) {
                    DBG("Links sanitizer: wrong source pin (%s : %d)",jt->from.c_str(),jt->pin_from);
                    jt = it->second.erase(jt);
                    continue;
                }
            }
            if (pods[jt->to].ptr) {
                if (jt->pin_to >= pods[jt->to].ptr->getNumInPins()) {
                    DBG("Links sanitizer: wrong target pin (%s : %d)",jt->to.c_str(),jt->pin_to);
                    jt = it->second.erase(jt);
                    continue;
                }
            }
            ++jt;
        }
        ++it;
    }
}

bool AnnaLSCS::WriteTo(string fn)
{
    FILE* f = fopen(fn.c_str(),"w");
    if (!f) {
        internal_error = myformat("Unable to open file %s for writing",fn.c_str());
        return false;
    }

    // write pods
    int n = 0;
    for (auto &&i : pods) {
        if (!i.second.ptr) continue;
        fprintf(f,"Pod%dName = %s\n",n,i.first.c_str());
        fprintf(f,"Pod%dScript = %s\n",n,Aria::MakeRelativePath(fn,i.second.ptr->getFName()).c_str());
        fprintf(f,"Pod%dDims = %d %d %d %d\n",n,i.second.x,i.second.y,i.second.w,i.second.h);
        n++;
    }
    fprintf(f,"NPods = %d\n",n);

    // write links
    fprintf(f,"\n\nCONNECTIONS\n\n");
    for (auto &&i: links) {
        for (auto &&j : i.second)
            fprintf(f,"%s.%d -> %s.%d\n",j.from.c_str(),j.pin_from,j.to.c_str(),j.pin_to);
    }

    fclose(f);
    cfgmap.clear(); // not needed anymore
    config_fn = fn;
    return true;
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
        // extract and check pod's name
        string pnm = cfgmap[myformat("pod%dname",i)];
        if (pnm.empty()) {
            internal_error = myformat("No name (alias) provided for pod %d",i);
            return false;
        }
        // should be unique
        if (pods.count(pnm)) {
            internal_error = myformat("Duplicate pod name for pod %d",i);
            return false;
        }

        // extract pod's script
        string pscr = cfgmap[myformat("pod%dscript",i)];
        if (pscr.empty()) {
            internal_error = myformat("No script provided for pod %d",i);
            return false;
        }
        // and fix its path
        pscr = Aria::FixPath(config_fn,pscr);

        // actually create the pod
        AriaPod pod;
        pod.ptr = new Aria(pscr,pnm);
        if (pod.ptr->getState() != ARIA_READY) {
            internal_error = myformat("Unable to start pod: %s",pod.ptr->getError().c_str());
            delete pod.ptr;
            return false;
        }

        // extract pod's graphical dimensions (if exists)
        string dims = cfgmap[myformat("pod%ddims",i)];
        if (!dims.empty()) sscanf(dims.c_str(),"%d %d %d %d",&pod.x,&pod.y,&pod.w,&pod.h);

        // register the pod
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
                internal_error = myformat("Output pin %d doesn't exist in pod %s",j.pin_from,i.first.c_str());
                return false;
            }
            if (!pods.count(j.to)) {
                internal_error = myformat("Can't find a pod named %s as a target pod",j.to.c_str());
                return false;
            }
            if (j.pin_to < 0 || j.pin_to >= pods[j.to].ptr->getNumInPins()) {
                internal_error = myformat("Input pin %d doesn't exist in pod %s",j.pin_to,j.to.c_str());
                return false;
            }
        }
    }

    return true;
}

void AnnaLSCS::FanOut(string from)
{
    if (!pods.count(from) || !links.count(from)) return;
    Aria* pod = pods[from].ptr;
    if (!pod) return;

    map<int,string> outs;
    for (auto &&i : links[from]) {
        if (!outs.count(i.pin_from))
            outs[i.pin_from] = pod->getOutPin(i.pin_from);

        Aria* recv = pods[i.to].ptr;
        if (!recv) {
            internal_error = myformat("Receiver pod %s doesn't exist",i.to.c_str());
            return;
        }
        if (!outs[i.pin_from].empty())
            recv->setInPin(i.pin_to,outs[i.pin_from]);
    }
}
