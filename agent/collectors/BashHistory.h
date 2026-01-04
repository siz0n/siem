#pragma once
#include <string>
#include <vector>

#include "../core/Event.h"
#include "InotifyTailReader.h"

class BashHistoryCollector
{
public:
    BashHistoryCollector(std::string path, std::string hostname, std::string stateFile);

    void startFromEnd();
    void poll(std::vector<Event> &out);

private:
    static std::string nowIso8601Local();
    static std::string expandTilde(const std::string &path);

private:
    std::string hostname_;
    InotifyTailReader tail_;
};
