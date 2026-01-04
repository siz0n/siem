// agent/collectors/Auditd.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

#include "../core/Event.h"
#include "InotifyTailReader.h"

class AuditdCollector
{
public:
    AuditdCollector(std::string path, std::string hostname, std::string stateFile);

    void startFromEnd();
    void poll(std::vector<Event> &out);

private:
    static std::string nowIso8601Local();
    static std::string stripQuotes(std::string s);
    static std::string findValue(const std::string &line, const std::string &key);
    static std::string auditMsgToIso8601Utc(const std::string &line);

    //  (если не смогли склеить или SERIAL не нашли)
    static Event parseLineFallback(const std::string &hostname, const std::string &line);

    //
    static long long extractAuditSerial(const std::string &line);

    struct PendingAudit // собираем несколько строк с одним serial
    {
        std::string ts;
        std::string comm;
        std::string exe;
        std::string proctitle;
        std::string name;
        std::string auid;
        std::string uid;

        std::string success;
        std::string exitCode;

        std::string raw_avc;
        std::string raw_syscall;
        std::string raw_proctitle;

        bool has_avc = false;
        bool has_syscall = false;
        bool has_proctitle = false;

        std::chrono::steady_clock::time_point firstSeen{};
    };

    // SERIAL -> 1 merged Event
    static Event buildMergedEvent(const std::string &hostname, const PendingAudit &p);

private:
    std::string hostname_;
    InotifyTailReader tail_;

    std::unordered_map<long long, PendingAudit> pending_;
};
