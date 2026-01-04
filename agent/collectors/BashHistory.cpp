#include "BashHistory.h"

#include <ctime>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <utility>

#include "../core/Normalize.h"

using namespace std;

BashHistoryCollector::BashHistoryCollector(string path, string hostname, string stateFile)
    : hostname_(move(hostname)),
      tail_(expandTilde(path), "bash_history", stateFile,
            InotifyTailReader::TruncatePolicy::SeekToEnd)

{
}

void BashHistoryCollector::startFromEnd()
{
    tail_.startFromEnd();
}

string BashHistoryCollector::nowIso8601Local() // когда нашел
{
    time_t t = time(nullptr);
    tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return string(buf);
}

string BashHistoryCollector::expandTilde(const string &path)
{
    if (path.empty() || path[0] != '~')
        return path;

    string base;

    const char *sudoUser = getenv("SUDO_USER");
    if (sudoUser && *sudoUser)
    {
        passwd *pw = getpwnam(sudoUser);
        if (pw && pw->pw_dir && *pw->pw_dir)
            base = pw->pw_dir;
    }

    if (base.empty())
    {
        const char *home = getenv("HOME");
        if (home && *home)
            base = home;
    }

    if (base.empty())
        return path;

    if (path == "~")
        return base;

    if (path.rfind("~/", 0) == 0)
        return base + path.substr(1);

    return path;
}

void BashHistoryCollector::poll(vector<Event> &out)
{
    vector<string> lines;
    if (!tail_.readNewLines(lines))
        return;

    for (auto &cmd : lines)
    {
        if (cmd.empty())
            continue;

        Event e;
        e.timestamp = nowIso8601Local();
        e.hostname = hostname_;
        e.source = "bash_history";
        e.event_type = "command_history";
        e.severity = "low";
        e.process = "bash";
        e.command = cmd;
        e.raw_log = normalize::sanitizeRaw(cmd);

        normalize::normalizeEvent(e);
        out.push_back(move(e));
    }
}
