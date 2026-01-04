#include "Logger.h"

#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace std;

namespace logger
{

    static mutex g_mtx;
    static ofstream g_out;
    static Level g_level = Level::Info;
    static bool g_enabled = false;

    static Level parseLevel(string s)
    {
        transform(s.begin(), s.end(), s.begin(),
                  [](unsigned char c)
                  { return (char)tolower(c); });

        if (s == "debug")
            return Level::Debug;
        if (s == "info")
            return Level::Info;
        if (s == "warn" || s == "warning")
            return Level::Warn;
        if (s == "error")
            return Level::Error;
        return Level::Info;
    }

    static const char *levelName(Level l)
    {
        switch (l)
        {
        case Level::Error:
            return "ERROR";
        case Level::Warn:
            return "WARN";
        case Level::Info:
            return "INFO";
        case Level::Debug:
            return "DEBUG";
        }
        return "INFO";
    }

    static string nowIso8601Local()
    {
        time_t t = time(nullptr);
        tm tm{};
        localtime_r(&t, &tm);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return string(buf);
    }

    bool init(const string &filePath, const string &levelStr)
    {
        lock_guard<mutex> lk(g_mtx);

        g_level = parseLevel(levelStr);
        g_out.open(filePath, ios::app);
        if (!g_out.is_open())
        {
            g_enabled = false;
            return false;
        }
        g_enabled = true;
        g_out << nowIso8601Local() << " [" << levelName(Level::Info) << "] "
              << "logger started\n";
        g_out.flush();
        return true;
    }

    void shutdown()
    {
        lock_guard<mutex> lk(g_mtx);
        if (g_out.is_open())
        {
            g_out << nowIso8601Local() << " [" << levelName(Level::Info) << "] "
                  << "logger stopped\n";
            g_out.flush();
            g_out.close();
        }
        g_enabled = false;
    }

    void log(Level lvl, const string &msg)
    {
        lock_guard<mutex> lk(g_mtx);

        if (!g_enabled)
            return;
        if ((int)lvl > (int)g_level)
            return;

        g_out << nowIso8601Local()
              << " [" << levelName(lvl) << "] "
              << msg << "\n";
        g_out.flush();
    }

}
