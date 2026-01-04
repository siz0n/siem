
#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

using namespace std;

static inline string trim(const string &s)
{
    size_t b = 0;
    while (b < s.size() && isspace((unsigned char)s[b]))
        ++b;
    size_t e = s.size();
    while (e > b && isspace((unsigned char)s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

static inline int indentOf(const string &s)
{
    int i = 0;
    while (i < (int)s.size() && s[(size_t)i] == ' ')
        ++i;
    return i;
}

static inline bool parseBool(const string &v, bool def = false)
{
    string t = v;
    transform(t.begin(), t.end(), t.begin(),
              [](unsigned char c)
              { return (char)tolower(c); });

    if (t == "true" || t == "yes" || t == "1")
        return true;
    if (t == "false" || t == "no" || t == "0")
        return false;
    return def;
}

static inline int parseInt(const string &v, int def = 0)
{
    try
    {
        return stoi(v);
    }
    catch (...)
    {
        return def;
    }
}

static inline string unquote(string v)
{
    v = trim(v);
    if (v.size() >= 2 &&
        ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
    {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

bool loadConfigFile(const string &path, AgentConfig &cfg)
{
    ifstream in(path);
    if (!in.is_open())
        return false;

    // секции вложенности: sec1 -> sec2 -> sec3 (хватает)
    string sec1, sec2, sec3;
    int ind1 = -1, ind2 = -1, ind3 = -1;

    // текущий список внутри filter:
    string currentListKey;

    string line;
    while (getline(in, line))
    {
        // убрать комментарии (# ...)
        auto hash = line.find('#');
        if (hash != string::npos)
            line = line.substr(0, hash);

        if (trim(line).empty())
            continue;

        int ind = indentOf(line);
        string t = trim(line);

         if (t.rfind("-", 0) == 0)
        {
            string item = trim(t.substr(1));
            item = unquote(item);

            if (currentListKey.empty())
                continue;

            if (sec1 == "filter")
            {
                if (currentListKey == "keep_if_raw_contains")
                    cfg.filter.keep_if_raw_contains.push_back(item);
                else if (currentListKey == "drop_process_contains")
                    cfg.filter.drop_process_contains.push_back(item);
                else if (currentListKey == "drop_event_types")
                    cfg.filter.drop_event_types.push_back(item);
                else if (currentListKey == "drop_raw_contains")
                    cfg.filter.drop_raw_contains.push_back(item);
            }
            continue;
        }

        if (!t.empty() && t.back() == ':' && t.find(':') == t.size() - 1)
        {
            string name = trim(t.substr(0, t.size() - 1));
            currentListKey.clear();

            if (ind1 < 0 || ind <= ind1)
            {
                sec1 = name;
                ind1 = ind;
                sec2.clear();
                ind2 = -1;
                sec3.clear();
                ind3 = -1;
            }
            else if (ind2 < 0 || ind <= ind2)
            {
                sec2 = name;
                ind2 = ind;
                sec3.clear();
                ind3 = -1;
            }
            else
            {
                sec3 = name;
                ind3 = ind;
            }
            continue;
        }

        auto colon = t.find(':');
        if (colon == string::npos)
            continue;

        string key = trim(t.substr(0, colon));
        string val = trim(t.substr(colon + 1));
        val = unquote(val);

        // если "key:" без значения -> дальше пойдут элементы списка
        if (val.empty())
        {
            currentListKey = key;
            continue;
        }
        currentListKey.clear();

        if (sec1.empty())
        {
            if (key == "data_dir")
                cfg.data_dir = val;
            continue;
        }

        // server
        if (sec1 == "server")
        {
            if (key == "host")
                cfg.server.host = val;
            else if (key == "port")
                cfg.server.port = parseInt(val, cfg.server.port);
            continue;
        }

        // agent
        if (sec1 == "agent")
        {
            if (key == "id")
                cfg.agent.id = val;
            else if (key == "database")
                cfg.agent.database = val;
            continue;
        }

        // send
        if (sec1 == "send")
        {
            if (key == "interval_sec")
                cfg.send.interval_sec = parseInt(val, cfg.send.interval_sec);
            else if (key == "batch_size")
                cfg.send.batch_size = parseInt(val, cfg.send.batch_size);
            else if (key == "ram_ring_capacity")
                cfg.send.ram_ring_capacity = parseInt(val, cfg.send.ram_ring_capacity);
            continue;
        }

        // logging
        if (sec1 == "logging")
        {
            if (key == "enabled")
                cfg.logging.enabled = parseBool(val, cfg.logging.enabled);
            else if (key == "level")
                cfg.logging.level = val;
            else if (key == "path")
                cfg.logging.path = val;
            continue;
        }

        // filter
        if (sec1 == "filter")
        {
            if (key == "min_severity")
                cfg.filter.min_severity = val;
            else if (key == "drop_empty_raw")
                cfg.filter.drop_empty_raw = parseBool(val, cfg.filter.drop_empty_raw);
            else if (key == "rate_limit_window_sec")
                cfg.filter.rate_limit_window_sec = parseInt(val, cfg.filter.rate_limit_window_sec);
            else if (key == "dedupe_bash_history")
                cfg.filter.dedupe_bash_history = parseBool(val, cfg.filter.dedupe_bash_history);
            continue;
        }

        // sources.<name>
        if (sec1 == "sources" && !sec2.empty())
        {
            auto &src = cfg.sources[sec2];
            if (key == "enabled")
                src.enabled = parseBool(val, src.enabled);
            else if (key == "path")
                src.path = val;
            continue;
        }

        if (key == "data_dir")
            cfg.data_dir = val;
    }

    return true;
}
