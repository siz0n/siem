#include "../db/minidbms.h"
#include "protocol.h"
#include "request_handler.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h> // для struct timeval
#include <atomic>



#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace std;

struct DbEntry
{
    string name;   // имя базы
    MiniDBMS* db;       // указатель на объект базы
    mutex mtx;     // мьютекс НА КОНКРЕТНУЮ БД
    DbEntry* next;      // односвязный список
};

static DbEntry* g_dbList = nullptr;
static mutex g_dbListMutex;
// счетчик активных клиентов
static std::atomic<int> g_activeClients{0};
// максимально допустимое количество одновременно обслуживаемых клиентов
static constexpr int MAX_CLIENTS = 16;



// чтение строки из сокета
static bool readLine(int sock, std::string& out)
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
                std::cerr << "[Server] recv timeout\n";
            }
            else
            {
                std::perror("[Server] recv error");
            }
            return false;
        }

        if (n == 0)
        {
            // Клиент закрыл соединение
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


// отправка всей строки
static bool writeAll(int sock, const std::string& data)
{
    const char* buf = data.c_str();
    size_t total = data.size();
    size_t sent = 0;

    while (sent < total)
    {
        ssize_t n = ::send(sock, buf + sent, total - sent, 0);

        if (n < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                std::cerr << "[Server] send timeout\n";
            }
            else
            {
                std::perror("[Server] send error");
            }
            return false;
        }

        if (n == 0)
        {
            // очень редкий случай, но на всякий:
            std::cerr << "[Server] send returned 0\n";
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}


// вытащить строковое поле: "key":"value" для работы с клиентом
static bool extractJsonStringField(const string& json, const string& key, string& out)
{
    string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern); // ищем ключ
    if (pos == string::npos)
    {
        return false;
    }

    size_t colon = json.find(':', pos + pattern.size()); // ищем двоеточие
    if (colon == string::npos)
    {
        return false;
    }

    size_t first_quote = json.find('"', colon + 1); // ищем первую кавычку значения
    if (first_quote == string::npos)
    {
        return false;
    }

    size_t second_quote = json.find('"', first_quote + 1); // ищем вторую кавычку значения
    if (second_quote == string::npos)
    {
        return false;
    }

    out = json.substr(first_quote + 1, second_quote - first_quote - 1);// вынимаем значение между кавычками
    return true;
}

// поиск строки для бд
static bool extractJsonValueField(const string& json, const string& key, string& out) 
{
    string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == string::npos)
    {
        return false;
    }

    size_t colon = json.find(':', pos + pattern.size());
    if (colon == string::npos)
    {
        return false;
    }

    size_t start = json.find_first_not_of(" \t\n\r", colon + 1); // ищем начало значения
    if (start == string::npos)
    {
        return false;
    }

    char c = json[start]; // первый символ значения

    if (c == '{') 
    {
        int count = 0;
        size_t i = start;
        bool found_end = false;

        while (i < json.size())
        {
            if (json[i] == '{')
            {
                ++count;
            }
            else if (json[i] == '}')
            {
                --count;
                if (count == 0)
                {
                    ++i; 
                    found_end = true;
                    break;
                }
            }
            ++i;
        }

        if (!found_end)
        {
            return false;
        }

        out = json.substr(start, i - start); // вынимаем объект
        return true;
    }

    if (c == '[') 
    {
        int count = 0;
        size_t i = start;
        bool found_end = false;

        while (i < json.size())
        {
            if (json[i] == '[')
            {
                ++count;
            }
            else if (json[i] == ']')
            {
                --count;
                if (count == 0)
                {
                    ++i; // включаем ']'
                    found_end = true;
                    break;
                }
            }
            ++i;
        }

        if (!found_end)
        {
            return false;
        }

        out = json.substr(start, i - start);
        return true;
    }

    return false;
}

// разбор JSON-строки запроса в Request
static bool parseJsonRequest(const string& line, Request& req)
{
    req = Request{};

    if (!extractJsonStringField(line, "database", req.database))
    {
        return false;
    }
    if (!extractJsonStringField(line, "operation", req.operation))
    {
        return false;
    }

    string data_value;
    if (extractJsonValueField(line, "data", data_value))
    {
        req.data_json = data_value;
    }

    string query_value;
    if (extractJsonValueField(line, "query", query_value))
    {
        req.query_json = query_value;
    }

    return true;
}

// подготовка JSON-строки из Response
static string escapeJsonString(const string& s)
{
    string result;
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

// сериализация Response в JSON
static string serializeResponseToJson(const Response& resp)
{
    string json;
    json.reserve(128 + resp.data.size());

    json += "{";

    json += "\"status\":\"";
    json += escapeJsonString(resp.status);
    json += "\",";

    json += "\"message\":\"";
    json += escapeJsonString(resp.message);
    json += "\",";

    json += "\"count\":";
    json += to_string(resp.count);
    json += ",";

    // data — уже валидный JSON (обычно массив []), поэтому без кавычек
    json += "\"data\":";
    if (resp.data.empty())
    {
        json += "[]";
    }
    else
    {
        json += resp.data;
    }

    json += "}";
    json += "\n";

    return json;
}


static DbEntry* getOrCreateDbEntry(const string& dbName) // получение или создание записи базы
{
    lock_guard<mutex> lock(g_dbListMutex);

    // ищем уже существующую запись
    DbEntry* current = g_dbList;
    while (current != nullptr)
    {
        if (current->name == dbName)
        {
            return current;
        }
        current = current->next;
    }

    // не нашли - создаём новую базу
    MiniDBMS* db = new MiniDBMS(dbName);
    db->loadFromDisk();

    DbEntry* entry = new DbEntry;
    entry->name = dbName;
    entry->db = db;
    entry->next = g_dbList; // вставляем в начало списка

    g_dbList = entry;

    return entry;
}


// обработка клиента
static void handleClient(int clientSock)
{
    string line; // буфер для чтения строк

    while (readLine(clientSock, line)) // читаем запросы построчно
    {
        if (line.empty())
        {
            continue;
        }

        Request req;
        if (!parseJsonRequest(line, req))
        {
            // Некорректный JSON-запрос 
            Response resp;
            resp.status  = "error";
            resp.message = "Invalid request JSON format";
            resp.count   = 0;
            resp.data    = "[]";

            string out = serializeResponseToJson(resp);
            (void)writeAll(clientSock, out);
            continue;
        }

        // Получаем (или создаём) запись для нужной базы
        DbEntry* entry = getOrCreateDbEntry(req.database);

        Response resp;
        {
            // Блокируем КОНКРЕТНУЮ БД на время операции
            lock_guard<mutex> dbLock(entry->mtx);
            resp = processRequest(req, *entry->db);
        }

        // Сериализуем ответ в JSON и отправляем
        string out = serializeResponseToJson(resp);
        if (!writeAll(clientSock, out))
        {
            // Ошибка отправки - выходим из цикла и закрываем сокет
            break;
        }
    }

    ::close(clientSock);
}


int main(int argc, char* argv[])
{
    if (argc < 3) // порт и имя бд
    {
        cerr << "Usage: " << argv[0]
                  << " <port> <default_db_name>\n";
        return 1;
    }

    int port = stoi(argv[1]);
    string defaultDbName = argv[2];

    // заранее подгружаем дефолтную БД
    {
        DbEntry* entry = getOrCreateDbEntry(defaultDbName);
        (void)entry; // чтобы не было предупреждения
    }

    int listenSock = ::socket(AF_INET, SOCK_STREAM, 0); // создаём слушающий сокет tcp
    if (listenSock < 0) // ошибка
    {
        perror("socket"); // вывод ошибки
        return 1;
    }

    sockaddr_in addr{}; // заполняем адрес
    addr.sin_family = AF_INET; // IPv4
    addr.sin_addr.s_addr = INADDR_ANY; // слушаем на всех интерфейсах
    addr.sin_port = htons(static_cast<uint16_t>(port)); // порт

    if (::bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) // привязываем сокет к адресу
    {
        perror("bind");
        ::close(listenSock);
        return 1;
    }

    if (::listen(listenSock, 16) < 0) // начинаем слушать сокет макс 16
    {
        perror("listen");
        ::close(listenSock);
        return 1;
    }

    cout << "Server listening on port " << port << endl;

    while (true) // ждем клиента 
{
    sockaddr_in clientAddr{};
    socklen_t clientLen = sizeof(clientAddr);

    int clientSock = ::accept(
        listenSock,
        reinterpret_cast<sockaddr*>(&clientAddr),
        &clientLen
    );

    if (clientSock < 0) // ошибка accept
    {
        perror("accept");
        continue;
    }

    // проверяем, не превышен ли лимит активных клиентов
    if (g_activeClients.load() >= MAX_CLIENTS)
    {
        std::cerr << "[Server] Too many clients (" << g_activeClients.load()
                  << "), refusing new connection\n";
        ::close(clientSock);
        continue;
    }

    // увеличиваем счетчик активных клиентов
    g_activeClients++;

    // настраиваем таймауты для избежания зависаний recv/send
    struct timeval tv;
    tv.tv_sec = 120;  // 5 секунд
    tv.tv_usec = 0;

    if (::setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_RCVTIMEO");
    }
    if (::setsockopt(clientSock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
    {
        perror("setsockopt SO_SNDTIMEO");
    }

    // запускаем поток, который по завершении уменьшит счетчик активных клиентов
    std::thread t(
        [](int sock)
        {
            handleClient(sock);
            g_activeClients--;
        },
        clientSock);

    t.detach();
}



    close(listenSock);// закрываем слушающий сокет
    return 0;
}
