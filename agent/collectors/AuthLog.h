#pragma once
#include <string>
#include <vector>

#include "../core/Event.h"
#include "InotifyTailReader.h"

class AuthLogCollector
{
public:
    AuthLogCollector(std::string path, std::string hostname, std::string stateFile);

    void startFromEnd();
    void poll(std::vector<Event> &out);

private:
    static std::string nowIso8601Local();
    static Event parseLine(const std::string &hostname, const std::string &line);

private:
    std::string hostname_;
    InotifyTailReader tail_;
};
