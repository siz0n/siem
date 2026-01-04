
#include "Spool.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>

#include "../network/TcpClient.h"
#include "RingBuffer.h"

using namespace std;

namespace spool
{

    static string g_dir;
    static string g_queue;
    static string g_inflight;

    static RingBuffer<string> *g_ring = nullptr;

    static void ensureDir(const string &dir)
    {
        error_code ec;
        filesystem::create_directories(dir, ec);
    }

    static string normalizeLine(string s) // \т\т
    {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        return s;
    }

    static bool fileHasData(const string &path)
    {
        ifstream in(path, ios::binary);
        if (!in.is_open())
            return false;
        return in.peek() != ifstream::traits_type::eof();
    }

    static bool responseIsSuccess(const string &resp)
    {
        // минимальный надёжный чек: {"status":"success"...}
        return (resp.find("\"status\"") != string::npos) &&
               (resp.find("success") != string::npos);
    }

    static bool appendQueue(const string &line)
    {
        if (g_queue.empty())
            return false;
        ofstream out(g_queue, ios::app | ios::binary);
        if (!out.is_open())
            return false;
        out << line << "\n";
        return true;
    }

    static bool prependQueueHead(const string &line)
    {
        if (g_queue.empty())
            return false;

        string tmp = g_queue + ".tmp";
        {
            ofstream out(tmp, ios::binary | ios::trunc);
            if (!out.is_open())
                return false;

            if (!line.empty())
                out << line << "\n";

            ifstream q(g_queue, ios::binary);
            if (q.is_open())
                out << q.rdbuf();
        }

        error_code ec;
        filesystem::rename(tmp, g_queue, ec);
        if (ec)
        {
            remove(tmp.c_str());
            return false;
        }
        return true;
    }

    void init(const string &spoolDir, size_t ringCapacity)
    {
        g_dir = spoolDir;
        ensureDir(g_dir);

        if (!g_dir.empty() && g_dir.back() == '/')
            g_dir.pop_back();

        g_queue = g_dir + "/queue.log";
        g_inflight = g_dir + "/inflight.log";

                {
            ofstream out(g_queue, ios::app);
        }
        {
            ofstream out(g_inflight, ios::app);
        }

        if (ringCapacity == 0)
            ringCapacity = 1;
        delete g_ring;
        g_ring = new RingBuffer<string>(ringCapacity);
    }

    string queuePath() { return g_queue; }
    string inflightPath() { return g_inflight; }

    bool enqueue(const string &payloadLine)
    {
        if (g_queue.empty() || g_inflight.empty())
            return false;

        string line = normalizeLine(payloadLine);
        if (line.empty())
            return true;

        // 1) сначала RAM ring
        if (g_ring && g_ring->push(move(line)))
        {
            return true;
        }

        // 2) если ring полный — на диск
        return appendQueue(line);
    }

    bool hasData()
    {
        bool ringHas = false;
        if (g_ring)
            ringHas = !g_ring->empty();

        return ringHas || fileHasData(g_inflight) || fileHasData(g_queue);
    }

    bool clearAll()
    {
        if (g_ring)
        {
            // очищаем ring через pop
            string tmp;
            while (g_ring->pop(tmp))
            {
            }
        }

        bool ok1 = true, ok2 = true;
        if (!g_queue.empty())
            ok1 = (ofstream(g_queue, ios::trunc).good());
        if (!g_inflight.empty())
            ok2 = (ofstream(g_inflight, ios::trunc).good());
        return ok1 && ok2;
    }

    static bool sendLineWaitSuccess(TcpClient &client, const string &line)
    {
        string payload = normalizeLine(line);
        if (payload.empty())
            return true;

        string wire = payload;
        wire.push_back('\n');

        if (!client.sendAll(wire))
            return false;

        string resp;
        if (!client.readLine(resp))
            return false;

        return responseIsSuccess(resp);
    }

    static bool sendInflightOnce(TcpClient &client)
    {
        if (!fileHasData(g_inflight))
            return false;

        ifstream in(g_inflight, ios::binary);
        if (!in.is_open())
            return false;

        string line;
        if (!getline(in, line))
            return false;
        line = normalizeLine(line);
        if (line.empty())
            return false;

        if (!sendLineWaitSuccess(client, line))
            return false;

        ofstream clr(g_inflight, ios::trunc);
        return true;
    }

    static bool moveQueueHeadToInflight()
    {
        if (!fileHasData(g_queue))
            return false;

        ifstream in(g_queue, ios::binary);
        if (!in.is_open())
            return false;

        string first;
        if (!getline(in, first))
            return false;
        first = normalizeLine(first);

        {
            ofstream out(g_inflight, ios::binary | ios::trunc);
            if (!out.is_open())
                return false;
            out << first << "\n";
        }

        string tmp = g_queue + ".tmp";
        {
            ofstream out(tmp, ios::binary | ios::trunc);
            if (!out.is_open())
                return false;

            string rest;
            while (getline(in, rest))
            {
                out << normalizeLine(rest) << "\n";
            }
        }

        in.close();

        error_code ec;
        filesystem::rename(tmp, g_queue, ec);
        if (ec)
        {
            remove(tmp.c_str());
            return false;
        }

        return true;
    }

    static bool rollbackInflightToQueueHead()
    {
        if (!fileHasData(g_inflight))
            return true;

        ifstream infl(g_inflight, ios::binary);
        if (!infl.is_open())
            return false;

        string line;
        getline(infl, line);
        infl.close();

        line = normalizeLine(line);
        if (line.empty())
        {
            ofstream clr(g_inflight, ios::trunc);
            return true;
        }

        if (!prependQueueHead(line))
            return false;

        ofstream clr(g_inflight, ios::trunc);
        return true;
    }

    static bool flushFromRingOnce(TcpClient &client)
    {
        if (!g_ring || g_ring->empty())
            return false;

        string line;
        if (!g_ring->pop(line))
            return false;

        if (sendLineWaitSuccess(client, line))
        {
            return true;
        }

        prependQueueHead(line);
        return false;
    }

    bool flushSome(TcpClient &client, int maxItems)
    {
        if (maxItems <= 0)
            maxItems = 1;
        if (!client.isConnected())
            return false;

        bool flushedAny = false;

        for (int i = 0; i < maxItems; ++i)
        {

            if (fileHasData(g_inflight))
            {
                if (sendInflightOnce(client))
                {
                    flushedAny = true;
                    continue;
                }

                break;
            }

            if (g_ring && !g_ring->empty())
            {
                if (flushFromRingOnce(client))
                {
                    flushedAny = true;
                    continue;
                }

                break;
            }

            if (!fileHasData(g_queue))
                break;

            if (!moveQueueHeadToInflight())
                break;

            if (sendInflightOnce(client))
            {
                flushedAny = true;
                continue;
            }

            rollbackInflightToQueueHead();
            break;
        }

        return flushedAny;
    }

}
