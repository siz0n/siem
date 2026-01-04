
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "core/Config.h"
#include "core/Filter.h"
#include "core/Logger.h"
#include "network/TcpClient.h"

#include "buffer/Spool.h"
#include "buffer/RingBuffer.h"

#include "collectors/Auditd.h"
#include "collectors/Syslog.h"
#include "collectors/AuthLog.h"
#include "collectors/BashHistory.h"

using namespace std;

//  signals
static volatile sig_atomic_t g_stop = 0;
static void onSignal(int) { g_stop = 1; }

//  проверка источников
static bool sourceEnabled(const AgentConfig &cfg, const string &name)
{
    auto it = cfg.sources.find(name);
    return it != cfg.sources.end() && it->second.enabled;
}

static string sourcePath(const AgentConfig &cfg, const string &name, const string &fallback)
{
    auto it = cfg.sources.find(name);
    if (it == cfg.sources.end())
        return fallback;
    if (it->second.path.empty())
        return fallback;
    return it->second.path;
}

static string getHostNameFallback() // получить hostname системы
{
    char buf[256];
    if (::gethostname(buf, sizeof(buf)) == 0)
    {
        buf[sizeof(buf) - 1] = '\0';
        return string(buf);
    }
    return "unknown-host";
}

static bool fileExists(const string &path)
{
    error_code ec;
    return filesystem::exists(path, ec);
}

static void ensureDir(const string &dir)
{
    if (dir.empty())
        return;
    error_code ec;
    filesystem::create_directories(dir, ec);
}

static string expandHome(const string &path)
{
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/')
    {
        const char *home = getenv("HOME");
        if (home && *home)
            return string(home) + path.substr(1);
    }
    return path;
}

static string jsonEscape(const string &s)
{
    string out;
    out.reserve(s.size() + 16);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '\"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                const char *hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xF];
                out += hex[c & 0xF];
            }
            else
                out.push_back((char)c);
        }
    }
    return out;
}

// Один JSON = один insert с массивом документов (batch)
static string buildInsertBatchPayload(const string &database,
                                      const string &agentId,
                                      const vector<Event> &batch,
                                      size_t from,
                                      size_t count)
{
    string j;
    j.reserve(1024 + count * 512);

    j += "{";
    j += "\"database\":\"" + jsonEscape(database) + "\",";
    j += "\"operation\":\"insert\",";
    j += "\"data\":[";

    bool first = true;
    for (size_t i = 0; i < count; ++i)
    {
        const Event &e = batch[from + i];
        if (!first)
            j += ",";
        first = false;

        j += "{";
        j += "\"agent_id\":\"" + jsonEscape(agentId) + "\",";
        j += "\"timestamp\":\"" + jsonEscape(e.timestamp) + "\",";
        j += "\"hostname\":\"" + jsonEscape(e.hostname) + "\",";
        j += "\"source\":\"" + jsonEscape(e.source) + "\",";
        j += "\"event_type\":\"" + jsonEscape(e.event_type) + "\",";
        j += "\"severity\":\"" + jsonEscape(e.severity) + "\",";
        j += "\"raw_log\":\"" + jsonEscape(e.raw_log) + "\"";

        if (!e.user.empty())
            j += ",\"user\":\"" + jsonEscape(e.user) + "\"";
        if (!e.process.empty())
            j += ",\"process\":\"" + jsonEscape(e.process) + "\"";
        if (!e.command.empty())
            j += ",\"command\":\"" + jsonEscape(e.command) + "\"";
        if (!e.ip.empty())
            j += ",\"ip\":\"" + jsonEscape(e.ip) + "\"";

        j += "}";
    }

    j += "],";
    j += "\"query\":{}";
    j += "}";

    return j;
}

// Пуш payload в RAM, если RAM переполнен  в spool
static void pushPayload(RingBuffer<string> &ramQueue, const string &payload)
{
    string p = payload;
    if (!ramQueue.push(move(p)))
    {
        string oldest;
        if (ramQueue.pop(oldest))
            spool::enqueue(oldest);

        string p2 = payload;
        if (!ramQueue.push(move(p2)))
            spool::enqueue(payload);
    }
}

static void usage(const char *argv0)
{
    cerr << "Usage: " << argv0 << " [--config agent.conf] [--from-begin]\n";
}

int main(int argc, char **argv)
{
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    string configPath = "agent.conf";
    bool fromBegin = false;

    for (int i = 1; i < argc; ++i)
    {
        string a = argv[i];
        if (a == "--config" && i + 1 < argc)
            configPath = argv[++i];
        else if (a == "--from-begin")
            fromBegin = true;
        else
        {
            cerr << "Unknown arg: " << a << "\n";
            usage(argv[0]);
            return 1;
        }
    }

    AgentConfig cfg;
    if (!loadConfigFile(configPath, cfg))
    {
        cerr << "[Agent] Failed to load config: " << configPath << "\n";
        return 1;
    }

    // paths from data_dir
    const string dataDir = cfg.data_dir.empty() ? "./data" : cfg.data_dir;
    const string spoolDir = dataDir + "/spool";
    const string stateDir = dataDir + "/state";
    const string stateFile = stateDir + "/reader.state";

    ensureDir(dataDir);
    ensureDir(spoolDir);
    ensureDir(stateDir);

    //  LOG FILE

    string logPath = cfg.logging.path.empty() ? (dataDir + "/siem_agent.log") : cfg.logging.path;
    {
        filesystem::path p(logPath);
        if (p.has_parent_path())
            ensureDir(p.parent_path().string());
    }

    if (cfg.logging.enabled)
    {
        if (!logger::init(logPath, cfg.logging.level))
        {
            cerr << "[Agent] Failed to open log file: " << logPath << "\n";
        }
    }

    logger::info(string("[Agent] Config loaded: ") + configPath);
    logger::info(string("[Agent] dataDir=") + dataDir);
    logger::info(string("[Agent] logPath=") + logPath);

    // spool init
    spool::init(spoolDir);
    logger::info(string("[Agent] Spool queue=") + spool::queuePath() + " inflight=" + spool::inflightPath());

    const bool hadState = fileExists(stateFile);
    logger::info(string("[Agent] State file: ") + stateFile + " exists=" + (hadState ? "yes" : "no"));

    const string hostname = getHostNameFallback();
    logger::info(string("[Agent] hostname=") + hostname);

    // sources paths from cfg
    const string auditPath = sourcePath(cfg, "auditd", "/var/log/audit/audit.log");
    const string syslogPath = sourcePath(cfg, "syslog", "/var/log/syslog");
    const string authPath = sourcePath(cfg, "auth", "/var/log/auth.log");
    const string bashPath = expandHome(sourcePath(cfg, "bash_history", "~/.bash_history"));

    logger::info(string("[Agent] paths: auditd=") + auditPath);
    logger::info(string("[Agent] paths: syslog=") + syslogPath);
    logger::info(string("[Agent] paths: auth=") + authPath);
    logger::info(string("[Agent] paths: bash_history=") + bashPath);

    //  collectors
    AuditdCollector audit(auditPath, hostname, stateFile);
    SyslogCollector sys(syslogPath, hostname, stateFile);
    AuthLogCollector auth(authPath, hostname, stateFile);
    BashHistoryCollector bash(bashPath, hostname, stateFile);

    // первый запуск (state нет) -> jump to end, чтобы не засосать старые логи
    if (fromBegin)
    {
        logger::warn("[Agent] fromBegin=yes (do not startFromEnd)");
    }
    else if (!hadState)
    {
        logger::info("[Agent] First run (no state): startFromEnd for enabled sources");
        if (sourceEnabled(cfg, "auditd"))
            audit.startFromEnd();
        if (sourceEnabled(cfg, "syslog"))
            sys.startFromEnd();
        if (sourceEnabled(cfg, "auth"))
            auth.startFromEnd();
        if (sourceEnabled(cfg, "bash_history"))
            bash.startFromEnd();
    }
    else
    {
        logger::info("[Agent] State exists: continue from saved offsets");
    }

    //  filter
    EventFilter filter(cfg.filter);

    //  RAM ring buffer (payloads, not events)
    const int ringCap = (cfg.send.ram_ring_capacity <= 0) ? 256 : cfg.send.ram_ring_capacity;
    RingBuffer<string> ramQueue((size_t)ringCap);

    TcpClient client(cfg.server.host, cfg.server.port, 5);

    const string agentId = cfg.agent.id;
    const string database = cfg.agent.database;

    const int batchSize = (cfg.send.batch_size <= 0) ? 1 : cfg.send.batch_size;
    const int sendIntervalSec = (cfg.send.interval_sec <= 0) ? 1 : cfg.send.interval_sec;

    logger::info(string("[Agent] server=") + cfg.server.host + ":" + to_string(cfg.server.port));
    logger::info(string("[Agent] agent.id=") + agentId + " database=" + database);
    logger::info(string("[Agent] send.interval_sec=") + to_string(sendIntervalSec) + " send.batch_size=" + to_string(batchSize) + " ram_ring_capacity=" + to_string(ringCap));

    vector<Event> pending;
    pending.reserve((size_t)batchSize * 2);

    auto lastBatchFlush = chrono::steady_clock::now();

    while (!g_stop)
    {
        vector<Event> events;

        if (sourceEnabled(cfg, "auditd"))
            audit.poll(events);
        if (sourceEnabled(cfg, "syslog"))
            sys.poll(events);
        if (sourceEnabled(cfg, "auth"))
            auth.poll(events);
        if (sourceEnabled(cfg, "bash_history"))
            bash.poll(events);

        for (const auto &e : events)
        {
            if (!filter.allow(e))
                continue;
            pending.push_back(e);
        }

        while ((int)pending.size() >= batchSize)
        {
            string payload = buildInsertBatchPayload(database, agentId, pending, 0, (size_t)batchSize);
            pushPayload(ramQueue, payload);
            pending.erase(pending.begin(), pending.begin() + batchSize);
        }

        auto now = chrono::steady_clock::now();
        auto sec = chrono::duration_cast<chrono::seconds>(now - lastBatchFlush).count();
        if (sec >= sendIntervalSec)
        {

            if (!pending.empty())
                logger::debug(string("[Agent] send_interval: flushing tail events=") + to_string(pending.size()));

            while (!pending.empty())
            {
                size_t cnt = pending.size();
                if ((int)cnt > batchSize)
                    cnt = (size_t)batchSize;

                string payload = buildInsertBatchPayload(database, agentId, pending, 0, cnt);
                pushPayload(ramQueue, payload);

                pending.erase(pending.begin(), pending.begin() + (long)cnt);
            }

            lastBatchFlush = now;
        }

        //  connect
        if (!client.isConnected())
        {
            client.connect();
            if (client.isConnected())
                logger::info("[Agent] connected");
        }

        if (client.isConnected())
            spool::flushSome(client, 800);

        if (client.isConnected())
        {
            const int maxPayloadsPerTick = 20;
            for (int sent = 0; sent < maxPayloadsPerTick; ++sent)
            {
                string payload;
                if (!ramQueue.pop(payload))
                    break;

                string wire = payload;
                wire.push_back('\n');

                bool ok = false;
                if (client.sendAll(wire))
                {
                    string resp;
                    if (client.readLine(resp))
                        ok = (resp.find("success") != string::npos);
                }

                if (!ok)
                {
                    logger::warn("[Agent] send failed -> enqueue to spool and reconnect later");
                    spool::enqueue(payload);
                    client.close();
                    break;
                }
            }
        }

        this_thread::sleep_for(chrono::milliseconds(200));
    }

    logger::info("[Agent] stopping...");

    while (!pending.empty())
    {
        size_t cnt = pending.size();
        if ((int)cnt > batchSize)
            cnt = (size_t)batchSize;

        string payload = buildInsertBatchPayload(database, agentId, pending, 0, cnt);
        spool::enqueue(payload);
        pending.erase(pending.begin(), pending.begin() + (long)cnt);
    }

    // RAM -> spool
    {
        string x;
        while (ramQueue.pop(x))
            spool::enqueue(x);
    }

    logger::shutdown();
    return 0;
}
