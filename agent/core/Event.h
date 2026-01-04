#pragma once
#include <string>

struct Event
{
    std::string timestamp;
    std::string hostname;
    std::string source;
    std::string event_type;
    std::string process;
    std::string severity;

    std::string user;
    std::string command;
    std::string ip;

    std::string raw_log;
};
