
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct ServerConfig
{
    std::string host = "127.0.0.1";
    int port = 5000;
};

struct AgentSection
{
    std::string id = "agent-unknown";
    std::string database = "mydb";
};

struct SourceConfig
{
    bool enabled = false;
    std::string path;
};

struct SendConfig
{
    int interval_sec = 2;
    int batch_size = 2;
    int ram_ring_capacity = 2;
};

struct FilterConfig
{
    std::string min_severity = "low";
    bool drop_empty_raw = true;

    int rate_limit_window_sec = 0;
    bool dedupe_bash_history = false;

    std::vector<std::string> keep_if_raw_contains;
    std::vector<std::string> drop_process_contains;
    std::vector<std::string> drop_event_types;
    std::vector<std::string> drop_raw_contains;
};

struct LoggingConfig
{
    bool enabled = true;
    std::string level = "info";
    std::string path;
};

struct AgentConfig
{
    ServerConfig server;
    AgentSection agent;
    std::string data_dir = "./data";

    std::unordered_map<std::string, SourceConfig> sources;

    SendConfig send;
    FilterConfig filter;
    LoggingConfig logging;
};

bool loadConfigFile(const std::string &path, AgentConfig &cfg);
