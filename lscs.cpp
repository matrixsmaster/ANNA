#include "lscs.h"

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

AnnaState AnnaLSCS::Processing(bool skip_sampling)
{
    //FIXME: debug only
    return ANNA_TURNOVER;
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
        if (i.second) delete i.second;
    }
}

bool AnnaLSCS::ParseConfig()
{
    Aria test(config_fn); // FIXME: DEBUG ONLY!!
    return false;
}
