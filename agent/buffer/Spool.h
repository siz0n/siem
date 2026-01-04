#pragma once
#include <string>
#include <cstddef>

class TcpClient;

namespace spool
{

    void init(const std::string &spoolDir, std::size_t ringCapacity = 256);

    bool enqueue(const std::string &payloadLine);

    bool flushSome(TcpClient &client, int maxItems = 50);

    bool hasData();

    bool clearAll();

    std::string queuePath();
    std::string inflightPath();

}
