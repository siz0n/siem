
#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "Config.h"
#include "Event.h"

class EventFilter
{
public:
    explicit EventFilter(const FilterConfig &cfg);

    bool allow(const Event &e);

private:
    static int sevRank(const std::string &s);
    static bool containsAny(const std::string &hay, const std::vector<std::string> &needles);
    static std::string makeRateKey(const Event &e);

private:
    FilterConfig cfg_;

    std::string lastBashCommand_;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSeen_;
};
