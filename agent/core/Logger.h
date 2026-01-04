#pragma once
#include <string>

namespace logger
{

    enum class Level
    {
        Error = 0,
        Warn = 1,
        Info = 2,
        Debug = 3
    };

    bool init(const std::string &filePath, const std::string &levelStr);
    void shutdown();

    void log(Level lvl, const std::string &msg);

    inline void error(const std::string &s) { log(Level::Error, s); }
    inline void warn(const std::string &s) { log(Level::Warn, s); }
    inline void info(const std::string &s) { log(Level::Info, s); }
    inline void debug(const std::string &s) { log(Level::Debug, s); }

}
