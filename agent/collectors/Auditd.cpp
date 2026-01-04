// agent/collectors/Auditd.cpp
#include "Auditd.h"

#include <ctime>
#include <cctype>
#include <utility>
#include <algorithm>

#include "../core/Normalize.h"

using namespace std;

//  helpers
static bool isSpace(char c) { return isspace((unsigned char)c) != 0; }

AuditdCollector::AuditdCollector(string path, string hostname, string stateFile)
    : hostname_(move(hostname)),
      tail_(move(path), "auditd", move(stateFile))
{
}

void AuditdCollector::startFromEnd()
{
    tail_.startFromEnd();
}

string AuditdCollector::nowIso8601Local() // если ошибка с msg
{
    time_t t = time(nullptr);
    tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return string(buf);
}

string AuditdCollector::stripQuotes(string s)
{
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

// Ищет key=VALUE в строке auditd, VALUE до пробела или до закрывающей кавычки "comm="
string AuditdCollector::findValue(const string &line, const string &key)
{
    auto p = line.find(key);
    if (p == string::npos)
        return "";
    p += key.size();

    size_t end = p;

    if (end < line.size() && (line[end] == '"' || line[end] == '\''))
    {
        char q = line[end];
        size_t close = line.find(q, p + 1);
        if (close == string::npos)
            return "";
        return stripQuotes(line.substr(p, close - p + 1));
    }

    while (end < line.size() && !isSpace(line[end]))
        ++end;
    return line.substr(p, end - p);
}

// msg=audit(1700000000.123:456) = "YYYY-MM-DDTHH:MM:SS.mmmZ"
string AuditdCollector::auditMsgToIso8601Utc(const string &line)
{
    auto p = line.find("msg=audit(");
    if (p == string::npos)
        return "";

    p += string("msg=audit(").size();
    auto q = line.find(')', p);
    if (q == string::npos)
        return "";

    string inside = line.substr(p, q - p);

    auto colon = inside.find(':');
    if (colon != string::npos)
        inside.resize(colon);

    string secPart = inside;
    string msPart;
    auto dot = inside.find('.');
    if (dot != string::npos)
    {
        secPart = inside.substr(0, dot);
        msPart = inside.substr(dot + 1);
    }

    long long secs = 0;
    try
    {
        secs = stoll(secPart);
    }
    catch (...)
    {
        return "";
    }

    time_t t = (time_t)secs;
    tm tm{};
    gmtime_r(&t, &tm);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    string out(buf);

    if (!msPart.empty())
    {
        if (msPart.size() > 3)
            msPart.resize(3);
        while (msPart.size() < 3)
            msPart.push_back('0');
        out += ".";
        out += msPart;
    }

    out += "Z";
    return out;
}

// вытащить serial
long long AuditdCollector::extractAuditSerial(const string &line)
{
    auto p = line.find("msg=audit(");
    if (p == string::npos)
        return -1;
    p += string("msg=audit(").size();
    auto q = line.find(')', p);
    if (q == string::npos)
        return -1;

    string inside = line.substr(p, q - p);
    auto colon = inside.rfind(':');
    if (colon == string::npos || colon + 1 >= inside.size())
        return -1;

    string serialStr = inside.substr(colon + 1);

    try
    {
        return stoll(serialStr);
    }
    catch (...)
    {
        return -1;
    }
}

//  собираем одну строку
Event AuditdCollector::parseLineFallback(const string &hostname, const string &line)
{
    Event e;

    e.timestamp = auditMsgToIso8601Utc(line);
    if (e.timestamp.empty())
        e.timestamp = nowIso8601Local();

    e.hostname = hostname;
    e.source = "auditd";
    e.raw_log = normalize::sanitizeRaw(line);

    string type = findValue(line, "type=");
    if (type.empty())
        type = "AUDIT";
    e.event_type = type;

    const string comm = stripQuotes(findValue(line, "comm="));
    const string exe = stripQuotes(findValue(line, "exe="));
    const string proctitle = stripQuotes(findValue(line, "proctitle="));

    e.process = comm;
    if (e.process.empty())
        e.process = exe;

    string acct = stripQuotes(findValue(line, "acct="));
    if (!acct.empty())
    {
        e.user = acct;
    }
    else
    {
        string auid = findValue(line, "auid=");
        if (!auid.empty())
            e.user = "auid:" + auid;
        else
        {
            string uid = findValue(line, "uid=");
            if (!uid.empty())
                e.user = "uid:" + uid;
        }
    }

    if (!exe.empty())
        e.command = exe;
    else if (!proctitle.empty())
        e.command = proctitle;
    else
        e.command = comm;

    bool denied =
        (line.find("DENIED") != string::npos) ||
        (line.find("apparmor=\"DENIED\"") != string::npos) ||
        (line.find("success=no") != string::npos);

    string res = findValue(line, "res=");
    string exitCode = findValue(line, "exit=");

    bool failed =
        (res == "failed") ||
        (!exitCode.empty() && exitCode[0] == '-');

    if (denied || failed)
        e.severity = "high";
    else if (type == "USER_LOGIN" || type == "USER_AUTH" || type == "SYSCALL" || type == "EXECVE")
        e.severity = "medium";
    else
        e.severity = "low";

    if (type == "PROCTITLE")
    {
        if (!proctitle.empty())
            e.command = proctitle;
        if (e.process.empty())
            e.process = "auditd";
        if (e.severity.empty())
            e.severity = "low";
    }

    normalize::normalizeEvent(e);
    return e;
}

//  объединяем несколько записей с одним serial в одно событие
Event AuditdCollector::buildMergedEvent(const string &hostname, const PendingAudit &p)
{
    Event e;
    e.hostname = hostname;
    e.source = "auditd";

    e.timestamp = p.ts.empty() ? nowIso8601Local() : p.ts;

    string raw;
    if (p.has_avc)
        raw += p.raw_avc;
    if (p.has_syscall)
    {
        if (!raw.empty())
            raw += " | ";
        raw += p.raw_syscall;
    }
    if (p.has_proctitle)
    {
        if (!raw.empty())
            raw += " | ";
        raw += p.raw_proctitle;
    }
    if (raw.empty())
        raw = "(merged audit event)";
    e.raw_log = normalize::sanitizeRaw(raw);

    e.process = !p.comm.empty() ? p.comm : p.exe;
    e.command = !p.exe.empty() ? p.exe : p.proctitle;

    // user
    if (!p.auid.empty())
        e.user = "auid:" + p.auid;
    else if (!p.uid.empty())
        e.user = "uid:" + p.uid;

    bool denied = false;
    if (p.has_avc)
        denied = true;
    if (p.success == "no")
        denied = true;
    if (!p.exitCode.empty() && p.exitCode[0] == '-')
        denied = true;

    if (denied)
    {
        e.event_type = "DENIED";
        e.severity = "high";
    }
    else if (p.has_syscall)
    {
        e.event_type = "SYSCALL";
        e.severity = "medium";
    }
    else
    {
        e.event_type = "AUDIT";
        e.severity = "low";
    }

    normalize::normalizeEvent(e);
    return e;
}

//  читаем новые стоки и парсим
void AuditdCollector::poll(vector<Event> &out)
{
    vector<string> lines;
    if (!tail_.readNewLines(lines))
        return;

    const auto now = chrono::steady_clock::now();

    // держим максимум ~2 секунды на сериал
    for (auto it = pending_.begin(); it != pending_.end();)
    {
        auto ageMs = chrono::duration_cast<chrono::milliseconds>(now - it->second.firstSeen).count();
        if (ageMs > 2000)
        {

            out.push_back(buildMergedEvent(hostname_, it->second));
            it = pending_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const auto &line : lines)
    {
        if (line.empty())
            continue;

        long long serial = extractAuditSerial(line);
        if (serial < 0)
        {
            // не смогли вытащить SERIAL — fallback
            out.push_back(parseLineFallback(hostname_, line));
            continue;
        }

        auto &p = pending_[serial];
        if (p.firstSeen.time_since_epoch().count() == 0)
            p.firstSeen = now;

        // timestamp из msg=audit()
        if (p.ts.empty())
        {
            string ts = auditMsgToIso8601Utc(line);
            if (!ts.empty())
                p.ts = move(ts);
        }

        string type = findValue(line, "type=");

        if (type == "AVC")
        {
            p.has_avc = true;
            p.raw_avc = line;

            if (p.comm.empty())
                p.comm = stripQuotes(findValue(line, "comm="));
        }
        else if (type == "SYSCALL")
        {
            p.has_syscall = true;
            p.raw_syscall = line;

            if (p.comm.empty())
                p.comm = stripQuotes(findValue(line, "comm="));
            if (p.exe.empty())
                p.exe = stripQuotes(findValue(line, "exe="));

            // user ids
            if (p.auid.empty())
                p.auid = stripQuotes(findValue(line, "auid="));
            if (p.uid.empty())
                p.uid = stripQuotes(findValue(line, "uid="));

            // success / exit
            if (p.success.empty())
                p.success = stripQuotes(findValue(line, "success="));
            if (p.exitCode.empty())
                p.exitCode = stripQuotes(findValue(line, "exit="));
        }
        else if (type == "PROCTITLE")
        {
            p.has_proctitle = true;
            p.raw_proctitle = line;

            if (p.proctitle.empty())
                p.proctitle = stripQuotes(findValue(line, "proctitle="));
        }
        else
        {

            out.push_back(parseLineFallback(hostname_, line));
            continue;
        }

        if (p.has_syscall && (p.has_avc || p.has_proctitle))
        {
            out.push_back(buildMergedEvent(hostname_, p));
            pending_.erase(serial);
        }
    }
}
