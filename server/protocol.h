#pragma once

#include <string>
#include <cstddef> 

struct Request
{ 
    std::string database; // имя базы данных
    std::string operation; // "insert", "find", "delete"
    std::string data_json; // данные для вставки (только для insert)
    std::string query_json; // уловия
};

struct Response
{
    std::string status; // success / error
    std::string message;
    std::size_t count = 0; // количество найденных/удаленных документов

    std::string data; // найденные данные в формате JSON (для find)
};
