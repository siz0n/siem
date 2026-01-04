// agent/collectors/Syslog.cpp
#include "Syslog.h"

#include <ctime>
#include <cctype>
#include <utility>
#include <string>

#include "../core/Normalize.h"

using namespace std;

SyslogCollector::SyslogCollector(string path, string hostname, string stateFile)
    : hostname_(move(hostname)),
      tail_(move(path), "syslog", move(stateFile))
{
}

void SyslogCollector::startFromEnd()
{
    tail_.startFromEnd();
}

string SyslogCollector::nowIso8601Local()
{
    time_t t = time(nullptr);
    tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return string(buf);
}

static void toLowerInplace(string &s) // все мательное
{
    for (char &c : s)
        c = (char)tolower((unsigned char)c);
}

static bool containsInsensitive(string hay, string needle) // поиск без учёта регистра
{
    toLowerInplace(hay);
    toLowerInplace(needle);
    return hay.find(needle) != string::npos;
}

static string extractIsoTs(const string &line) // вытаскиваем временную метку из начала строки
{
    auto sp = line.find(' ');
    if (sp == string::npos)
        return "";
    string ts = line.substr(0, sp);

    if (ts.size() >= 19 && ts[4] == '-' && ts[7] == '-' && ts[10] == 'T')
        return ts;

    return "";
}

struct SyslogHeader
{
    string program;
    string ident;   // TAG before ':'
    string message; // text after ": "
    int pid = -1;
};

static SyslogHeader parseSyslogLine(const string &line)
{
    SyslogHeader h{};

    // 1) пропускаем "<ts> <host> "
    size_t p1 = line.find(' ');
    if (p1 == string::npos)
        return h;
    size_t p2 = line.find(' ', p1 + 1);
    if (p2 == string::npos)
        return h;

    // 2)  до пробела после host
    size_t p3 = line.find(' ', p2 + 1);
    if (p3 == string::npos)
        return h;

    string tagChunk = line.substr(p2 + 1, p3 - (p2 + 1));
    if (!tagChunk.empty() && tagChunk.back() == ':')
        tagChunk.pop_back();

    string rest = line.substr(p3 + 1);

    if (!rest.empty() && rest.front() == ':')
    {
        rest.erase(rest.begin());
        if (!rest.empty() && rest.front() == ' ')
            rest.erase(rest.begin());
    }
    h.message = rest;

    // 4) извлекаем program и pid
    string ident = tagChunk;
    int pid = -1;

    auto lb = tagChunk.find('[');
    auto rb = tagChunk.find(']');
    if (lb != string::npos && rb != string::npos && rb > lb)
    {
        ident = tagChunk.substr(0, lb);
        string pidStr = tagChunk.substr(lb + 1, rb - (lb + 1));
        try
        {
            pid = stoi(pidStr);
        }
        catch (...)
        {
            pid = -1;
        }
    }

    while (!ident.empty() && isspace((unsigned char)ident.back()))
        ident.pop_back();
    while (!ident.empty() && isspace((unsigned char)ident.front()))
        ident.erase(ident.begin());

    h.ident = ident;
    h.pid = pid;

    h.program = ident;

    return h;
}

Event SyslogCollector::parseLine(const string &hostname, const string &line)
{
    Event e;

    e.timestamp = extractIsoTs(line);
    if (e.timestamp.empty())
        e.timestamp = nowIso8601Local();

    e.hostname = hostname;
    e.source = "syslog";
    e.raw_log = normalize::sanitizeRaw(line);

    SyslogHeader h = parseSyslogLine(line);

    // базовые
    e.process = h.program;
    e.command.clear();

    // message -> command
    if (!h.message.empty())
        e.command = h.message;

    // классификация
    e.event_type = "syslog_raw";
    e.severity = "low";

    string progLower = e.process;
    toLowerInplace(progLower);

    if (!h.ident.empty())
    {
        string identLower = h.ident;
        toLowerInplace(identLower);

        string hostLower = hostname;
        toLowerInplace(hostLower);

        // если "host host: ..." или ident не похож на известный демон, считаем user_log
        if (identLower == hostLower || identLower == "logger" || identLower == "user" || identLower == "test")
        {
            e.process = "logger";
            e.user = h.ident; // tag
            e.event_type = "user_log";
            e.severity = "low";
        }
        else
        {
            // Упростим: если pid отсутствует И ident == hostname -> user_log
            if (h.pid < 0 && identLower == hostLower)
            {
                e.process = "logger";
                e.user = h.ident;
                e.event_type = "user_log";
            }
        }
    }

    // стандартные источники
    if (progLower == "cron")
    {
        e.event_type = "cron_event";
        e.severity = "low";
    }
    else if (progLower == "systemd")
    {
        e.event_type = "service_event";
        e.severity = "low";
    }
    else if (progLower == "kernel")
    {
        e.event_type = "kernel_event";
        e.severity = "medium";
    }

    // severity по содержимому
    if (containsInsensitive(e.raw_log, "denied") || containsInsensitive(e.raw_log, "success=no"))
    {
        e.severity = "high";
    }
    else if (containsInsensitive(e.raw_log, "failed") || containsInsensitive(e.raw_log, "error") ||
             containsInsensitive(e.raw_log, "warning"))
    {
        // чтобы не превращать всё в high, держим medium
        if (e.severity != "high")
            e.severity = "medium";
    }

    normalize::normalizeEvent(e);
    return e;
}

void SyslogCollector::poll(vector<Event> &out)
{
    vector<string> lines;
    if (!tail_.readNewLines(lines))
        return;

    for (const auto &line : lines)
    {
        if (line.empty())
            continue;
        out.push_back(parseLine(hostname_, line));
    }
}
