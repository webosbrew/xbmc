#define _GLIBCXX_USE_CXX11_ABI 0
#include "DVDVideoCodecStarfish.h"

std::unique_ptr<char[]> CDVDVideoCodecStarfish::FeedLegacy(StarfishMediaAPIs* api, const char* payload)
{
    auto res = api->Feed(payload);

    auto p = std::unique_ptr<char[]>(new char[res.size() + 1]);
    std::strncpy(p.get(), res.c_str(), res.size() + 1);

    return p;
}
