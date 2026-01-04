#pragma once
#include <string>

struct Event;

namespace normalize
{

    std::string toIsoUtcZ(const std::string &ts);

    std::string sanitizeRaw(const std::string &s);

    void normalizeEvent(Event &e);

}
