#include "Filter.h"

#include <utility>

using namespace std;

EventFilter::EventFilter(const FilterConfig &cfg)
    : cfg_(cfg)
{
}

int EventFilter::sevRank(const string &s)
{
    if (s == "high")
        return 2;
    if (s == "medium")
        return 1;
    return 0;
}

bool EventFilter::containsAny(const string &hay, const vector<string> &needles) // проверка на вхождение любой из подстрок
{
    for (const auto &n : needles)
    {
        if (n.empty())
            continue;
        if (hay.find(n) != string::npos)
            return true;
    }
    return false;
}

string EventFilter::makeRateKey(const Event &e) // резать спам
{
    string k;
    k.reserve(128);
    k += e.source;
    k += '|';
    k += e.event_type;
    k += '|';
    k += e.process;
    k += '|';
    k += e.user;
    k += '|';
    k += e.ip;
    return k;
}

bool EventFilter::allow(const Event &e)
{

    if (cfg_.drop_empty_raw && e.raw_log.empty())
        return false;

    if (!cfg_.keep_if_raw_contains.empty() && containsAny(e.raw_log, cfg_.keep_if_raw_contains))
        return true;

    if (sevRank(e.severity) < sevRank(cfg_.min_severity))
        return false;

    if (!cfg_.drop_event_types.empty() && containsAny(e.event_type, cfg_.drop_event_types))
        return false;

    if (!cfg_.drop_process_contains.empty() && containsAny(e.process, cfg_.drop_process_contains))
        return false;

    if (!cfg_.drop_raw_contains.empty() && containsAny(e.raw_log, cfg_.drop_raw_contains))
        return false;

    // bash_history: подряд одинаковых команд
    if (cfg_.dedupe_bash_history && e.source == "bash_history")
    {
        if (!e.command.empty() && e.command == lastBashCommand_)
            return false;
        lastBashCommand_ = e.command;
    }

    if (cfg_.rate_limit_window_sec > 0)
    {
        const string key = makeRateKey(e);
        auto now = chrono::steady_clock::now();

        auto it = lastSeen_.find(key);
        if (it != lastSeen_.end())
        {
            auto sec = chrono::duration_cast<chrono::seconds>(now - it->second).count();
            if (sec < cfg_.rate_limit_window_sec)
                return false;
        }

        lastSeen_[key] = now;
    }

    return true;
}
