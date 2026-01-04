#include "document.h"
#include <iostream>

using namespace std;

Document::Document(string id) : _id(id)
{ // _id = id
}

void Document::addField(const string &key, const string &value) // добавление файла
{
    for (size_t i = 0; i < keys.getSize(); i++)
    {
        if (keys[i] == key)
        {
            values[i] = value;
            return;
        }
    }
    keys.push(key);
    values.push(value);
}
bool Document::getField(const string &key, string &out) const // ищем значение по ключу
{                                                             // проверка на наличие ключа
    for (size_t i = 0; i < keys.getSize(); i++)
    {
        if (keys[i] == key)
        {
            out = values[i];
            return true;
        }
    }
    return false;
}

string Document::serialize() const // создание json
{
    string json = "{";
    json += "\"_id\":\"" + _id + "\"";

    for (size_t i = 0; i < keys.getSize(); i++)
    { // проверка ключ ли id
        if (keys[i] == "_id")
            continue;
        json += ",\"" + keys[i] + "\":\"" + values[i] + "\"";
    }
    json += "}";
    return json;
}

Document *Document::deserialize(const std::string &json_line) // мини парсер
{
    string s = trim(json_line); // очищаем строку
    if (s.size() < 2 || s.front() != '{' || s.back() != '}')
    {

        return nullptr;
    }

    Document *doc = new Document();
    size_t i = 1;
    while (i < s.size() - 1)
    {
        // делаем пропуск лишних символов(пробел и т д)
        while (i < s.size() - 1 && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == '\n' || s[i] == '\r'))
            ++i;

        if (i >= s.size() - 1)
            break;
        if (s[i] != '"')
        {
            cerr << "Ошибка корректности файла\n";
            delete doc;
            return nullptr;
        }
        // ищем ключ
        size_t key_start = i + 1;
        size_t key_end = s.find('"', key_start);
        if (key_end == string::npos)
        {
            cerr << "Ошибка корректности файла\n";
            delete doc;
            return nullptr;
        }

        string key = s.substr(key_start, key_end - key_start);
        i = key_end + 1;

        while (i < s.size() - 1 && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == '\n' || s[i] == '\r'))
            ++i;

        if (i >= s.size() - 1 || s[i] != ':')
        {
            cerr << "Ошибка корректности файла\n";
            delete doc;
            return nullptr;
        }
        i++;

        while (i < s.size() - 1 && (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;

        // поиск значений
        string value;
        if (s[i] == '"')
        {
            size_t val_start = i + 1;
            size_t val_end = s.find('"', val_start);
            if (val_end == string::npos)
            {
                cerr << "Ошибка корректности файла\n";
                delete doc;
                return nullptr;
            }

            value = s.substr(val_start, val_end - val_start);
            i = val_end + 1;
        }

        // случай для чисел (256, 31})
        else
        {
            size_t val_start = i;
            size_t val_end = s.find_first_of(",}", val_start);
            if (val_end == string::npos)
            {
                cerr << "Ошибка корректности файла\n";
                delete doc;
                return nullptr;
            }
            string raw = s.substr(val_start, val_end - val_start);
            value = trim(raw);
            i = val_end;
        }

        if (key == "_id")
        {
            if (doc->_id.empty())
            {
                doc->_id = value;
            }
        }
        else
        {
            doc->addField(key, value);
        }
    }

    if (doc->_id.empty())
    {
        delete doc;
        return nullptr;
    }
    return doc;
}