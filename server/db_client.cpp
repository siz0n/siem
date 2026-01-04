#include <iostream>
#include <string>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <sys/time.h> // для struct timeval


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../db/utills.h"


static std::string toLower(const std::string& s) // приведение строки к нижнему регистру
{
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return res;
}


static std::string escapeJsonString(const std::string& s) // безопасная вставка строки в JSON
{
    std::string result;
    result.reserve(s.size() + 16);

    for (char c : s)
    {
        switch (c)
        {
        case '\\':
            result += "\\\\";
            break;
        case '\"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result.push_back(c);
            break;
        }
    }

    return result;
}

// отправка всей строки
bool writeAll(int sock, const std::string& data)
{
    const char* buf = data.c_str();
    std::size_t total = data.size();
    std::size_t sent = 0;

    while (sent < total)
    {
        ssize_t n = ::send(sock, buf + sent, total - sent, 0);

        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                std::cerr << "[Client] send timeout\n";
            }
            else
            {
                std::perror("[Client] send error");
            }
            return false;
        }

        if (n == 0)
        {
            std::cerr << "[Client] send returned 0\n";
            return false;
        }

        sent += static_cast<std::size_t>(n);
    }
    return true;
}


// чтение одной строки до '\n'
bool readLine(int sock, std::string& out)
{
    out.clear();
    char ch = 0;

    while (true)
    {
        ssize_t n = ::recv(sock, &ch, 1, 0);

        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                std::cerr << "[Client] recv timeout\n";
            }
            else
            {
                std::perror("[Client] recv error");
            }
            return false;
        }

        if (n == 0)
        {
            // сервер закрыл соединение
            return false;
        }

        if (ch == '\n')
        {
            break;
        }

        out.push_back(ch);
    }

    return true;
}


static bool buildJsonRequestFromCommand(const std::string& line, const std::string& database, std::string& outJson) // построение JSON-запроса из команды пользователя
{
    std::string trimmed = trim(line); // убираем пробелы
    if (trimmed.empty())
    {
        return false;
    }

    
    std::size_t spacePos = trimmed.find(' '); // ищем пробел
    std::string cmd = (spacePos == std::string::npos)  ? trimmed : trimmed.substr(0, spacePos); // команда
    std::string rest = (spacePos == std::string::npos ? std::string() : trim(trimmed.substr(spacePos + 1))); // остальная часть
    std::string op = toLower(cmd); // приводим к индексу

    if (op != "insert" && op != "find" && op != "delete")
    {
        std::cerr << "Unknown command: " << cmd
                  << " (use INSERT, FIND, DELETE)\n";
        return false;
    }

    // Для find/delete, если условия нет - считаем "{}"
    std::string queryJson = "{}";
    if (op == "find" || op == "delete")
    {
        if (!rest.empty())
        {
            queryJson = rest; // присваем напрямую
        }
    }


    //  rest должен быть либо (один документ), либо (массив).
    std::string dataJson = "[]";
    if (op == "insert")
    {
        if (rest.empty())
        {
            std::cerr << "INSERT требует JSON-документ после команды.\n";
            return false;
        }

        if (!rest.empty() && rest.front() == '[')
        {
            // уже массив
            dataJson = rest;
        }
        else
        {
            // один объект, оборачиваем в массив
            dataJson = "[" + rest + "]";
        }

        queryJson = "{}"; 
    }

    // Собираем JSON-запрос:
    // }
    std::string json;
    json.reserve(256 + dataJson.size() + queryJson.size()); // выделяем память

    json += "{";
    json += "\"database\":\"";
    json += escapeJsonString(database);
    json += "\",";

    json += "\"operation\":\"";
    json += escapeJsonString(op);
    json += "\",";

    json += "\"data\":";
    json += dataJson;
    json += ",";

    json += "\"query\":";
    json += queryJson;

    json += "}";
    json += "\n"; // сервер ждёт строку, заканчивающуюся \n

    outJson = json;
    return true;
}

int main(int argc, char* argv[])
{
    std::string host;
    int port;
    std::string database = "mydb";
    std::string onceCommand;
    bool onceMode = false; // режим одного запроса

 
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--host" && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
        }
        else if (arg == "--database" && i + 1 < argc)
        {
            database = argv[++i];
        }
        else if (arg == "--once" && i + 1 < argc)
        {
            onceMode = true;
            onceCommand = argv[++i];
        }
        else
        {
            std::cerr << "Неизвестный аргумент: " << arg << "\n";
            return 1;
        }
    }

    // создаём сокет
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }
    
    struct timeval tv;
    tv.tv_sec = 120;
    tv.tv_usec = 0;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
    }

    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_SNDTIMEO");
    }
    // настраиваем адрес сервера
    sockaddr_in addr{};
    addr.sin_family = AF_INET; //
    addr.sin_port = htons(static_cast<uint16_t>(port));

    // допускаем и IP, и "localhost"
    if (host == "localhost")
    {
        host = "127.0.0.1";
    }

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) // преобразуем строку в адрес
    {
        std::cerr << "Invalid host/IP address: " << host << "\n";
        close(sock);
        return 1;
    }

    // подключаемся
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        perror("connect");
        close(sock);
        return 1;
    }

    std::cout << "Connected to " << host << ":" << port
              << " (database: " << database << ")\n";

    // РЕЖИМ ОДНОГО ЗАПРОСА
    if (onceMode)
    {
        std::string reqJson; // запрос в JSON
        if (!buildJsonRequestFromCommand(onceCommand, database, reqJson)) 
        {
            std::cerr << "Failed to build request from --once command.\n";
            close(sock);
            return 1;
        }

        if (!writeAll(sock, reqJson)) // отправляем запрос
        {
            std::cerr << "Send error\n";
            close(sock);
            return 1;
        }

        std::string respLine;
        if (!readLine(sock, respLine)) // читаем ответ
        {
            std::cerr << "Disconnected from server\n";
            close(sock);
            return 1;
        }

        std::cout << respLine << "\n"; // печатаем ответ
        close(sock);
        return 0;
    }

    // ИНТЕРАКТИВНЫЙ РЕЖИМ
    while (true)
    {
        std::cout << "[" << database << "] > ";
        std::string line;
        if (!std::getline(std::cin, line))
        {
            break; 
        }

        std::string trimmed = trim(line); // убираем пробелы
        std::string lowered = toLower(trimmed); // приводим к нижнему регистру

        if (lowered == "exit" || lowered == "quit")
        {
            break;
        }

        if (trimmed.empty())
        {
            continue;
        }

        std::string reqJson;
        if (!buildJsonRequestFromCommand(line, database, reqJson))
        {
            continue;
        }

        if (!writeAll(sock, reqJson))
        {
            std::cerr << "Send error\n";
            break;
        }

        std::string respLine;
        if (!readLine(sock, respLine))
        {
            std::cerr << "Disconnected from server\n";
            close(sock);
            return 1;
        }

        std::cout << respLine << "\n";
    }

    close(sock);
    return 0;
}
