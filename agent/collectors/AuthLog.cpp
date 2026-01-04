#include "AuthLog.h"

#include <ctime>
#include <cctype>
#include <utility>
#include <stdint.h>

#include "../core/Normalize.h"

using namespace std;

AuthLogCollector::AuthLogCollector(string path, string hostname, string stateFile)
    : hostname_(move(hostname)),
      tail_(move(path), "auth.log", move(stateFile))
{
}

void AuthLogCollector::startFromEnd()
{
    tail_.startFromEnd();
}

static string fmtNow() // если ошикбка тользуем текущее время
{
    time_t t = time(nullptr);
    tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return string(buf);
}

string AuthLogCollector::nowIso8601Local() { return fmtNow(); }

static string extractIsoTs(const string &line) // если время сломалось
{
    auto sp = line.find(' ');
    if (sp == string::npos)
        return "";
    string ts = line.substr(0, sp);
    if (ts.size() >= 19 && ts[4] == '-' && ts[7] == '-' && ts[10] == 'T')
        return ts;
    return "";
}

static bool contains(const string &s, const char *sub) // проверка подстроки
{
    return s.find(sub) != string::npos;
}

static string extractBetween(const string &s, const string &a, const string &b) // вытягивем её
{
    auto p = s.find(a);
    if (p == string::npos)
        return "";
    p += a.size();
    auto q = s.find(b, p);
    if (q == string::npos)
        return "";
    return s.substr(p, q - p);
}

static string extractAfter(const string &s, const string &a) // вытягиваем всё после
{
    auto p = s.find(a);
    if (p == string::npos)
        return "";
    return s.substr(p + a.size());
}

static void rtrimSpaces(string &s)
{
    while (!s.empty() && isspace((unsigned char)s.back()))
        s.pop_back();
}

Event AuthLogCollector::parseLine(const string &hostname, const string &line)
{
    Event e;
    e.timestamp = extractIsoTs(line);
    if (e.timestamp.empty())
        e.timestamp = nowIso8601Local();

    e.hostname = hostname;
    e.source = "auth.log";
    e.raw_log = normalize::sanitizeRaw(line);
    e.event_type = "auth_raw";
    e.severity = "low";

    // sudo
    if (contains(line, " sudo: "))
    {
        e.process = "sudo";

        if (contains(line, "pam_unix(sudo:session): session opened for user "))
        {
            e.event_type = "user_login";
            e.severity = "medium";
            e.process = "pam_unix";

            e.user = extractBetween(line, " by ", "(");
            if (e.user.empty())
            {
                e.user = extractAfter(line, " by ");
                auto p = e.user.find('(');
                if (p != string::npos)
                    e.user.resize(p);
                rtrimSpaces(e.user);
            }

            normalize::normalizeEvent(e);
            return e;
        }

        if (contains(line, "pam_unix(sudo:session): session closed for user "))
        {
            e.event_type = "sudo_session_close";
            e.severity = "low";
            e.process = "pam_unix";
            normalize::normalizeEvent(e);
            return e;
        }

        e.event_type = "sudo";
        e.severity = "medium";
        e.user = extractBetween(line, " sudo: ", " :");
        if (e.user.empty())
            e.user = extractBetween(line, "sudo: ", " :");
        e.command = extractAfter(line, " COMMAND=");
        normalize::normalizeEvent(e);
        return e;
    }

    if (contains(line, "Failed password") && contains(line, " from "))
    {
        e.event_type = "ssh_fail";
        e.severity = "high";
        e.process = "sshd";

        if (contains(line, "for invalid user "))
            e.user = extractBetween(line, "for invalid user ", " from ");
        else
            e.user = extractBetween(line, "for ", " from ");

        e.ip = extractBetween(line, " from ", " port ");
        if (e.ip.empty())
            e.ip = extractBetween(line, " from ", " ");
        normalize::normalizeEvent(e);
        return e;
    }

    normalize::normalizeEvent(e);
    return e;
}

void AuthLogCollector::poll(vector<Event> &out)
{
    vector<string> lines;
    if (!tail_.readNewLines(lines))
        return;

    for (const auto &line : lines)
        out.push_back(parseLine(hostname_, line));
}
