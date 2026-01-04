#pragma once
#include <string>

class TcpClient
{
public:
    TcpClient(std::string host, int port, int timeoutSec = 5);

    bool connect();
    void close();
    bool isConnected() const;

    bool sendAll(const std::string &data);

    bool readLine(std::string &out);

private:
    std::string host_;
    int port_ = 0;
    int timeoutSec_ = 5;
    int sock_ = -1;
};
