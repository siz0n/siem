#include "TcpClient.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#include <cstdio>
#include <iostream>

using namespace std;

TcpClient::TcpClient(string host, int port, int timeoutSec)
    : host_(move(host)), port_(port), timeoutSec_(timeoutSec) {}

bool TcpClient::connect()
{
    close();

    sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0)
        return false;

    timeval tv{};
    tv.tv_sec = timeoutSec_;
    tv.tv_usec = 0;
    ::setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0)
    {
        close();
        return false;
    }

    if (::connect(sock_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        close();
        return false;
    }

    return true;
}

void TcpClient::close()
{
    if (sock_ >= 0)
    {
        ::close(sock_);
        sock_ = -1;
    }
}

bool TcpClient::isConnected() const { return sock_ >= 0; }

bool TcpClient::sendAll(const string &data)
{
    if (sock_ < 0)
        return false;

    const char *buf = data.c_str();
    size_t total = data.size();
    size_t sent = 0;

    while (sent < total)
    {
        ssize_t n = ::send(sock_, buf + sent, total - sent, 0);
        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                cerr << "[Agent] send timeout\n";
            else
                perror("[Agent] send error");
            return false;
        }
        if (n == 0)
            return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool TcpClient::readLine(string &out)
{
    if (sock_ < 0)
        return false;

    out.clear();
    char ch = 0;

    while (true)
    {
        ssize_t n = ::recv(sock_, &ch, 1, 0);
        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                cerr << "[Agent] recv timeout\n";
            else
                perror("[Agent] recv error");
            return false;
        }
        if (n == 0)
            return false;
        if (ch == '\n')
            break;
        out.push_back(ch);
    }

    return true;
}
