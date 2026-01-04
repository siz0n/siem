#include "request_handler.h"

#include <string>
#include <vector>
#include <stdexcept>

#include "../db/utills.h" // trim()

using namespace std;


static bool splitJsonArrayObjects(const std::string& arrJson,
                                  std::vector<std::string>& out)
{
    out.clear();

    std::string s = trim(arrJson);
    if (s.size() < 2 || s.front() != '[' || s.back() != ']')
        return false;

    std::string content = s.substr(1, s.size() - 2);
    size_t pos = 0;

    while (pos < content.size())
    {
        // пропускаем мусор
        while (pos < content.size() &&
               (content[pos]==' ' || content[pos]=='\t' ||
                content[pos]=='\n'|| content[pos]=='\r' ||
                content[pos]==',')) {
            ++pos;
        }
        if (pos >= content.size())
            break;

        if (content[pos] != '{')
            return false;

        size_t start = pos;
        int depth = 0;
        bool inStr = false;

        for (; pos < content.size(); ++pos)
        {
            char c = content[pos];

            if (c == '"' && (pos == 0 || content[pos - 1] != '\\'))
                inStr = !inStr;

            if (inStr)
                continue;

            if (c == '{') depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                {
                    ++pos; // включаем '}'
                    break;
                }
            }
        }

        std::string obj = trim(content.substr(start, pos - start));
        if (!obj.empty())
            out.push_back(obj);
    }

    return true;
}


Response processRequest(const Request& req, MiniDBMS& db)
{
    Response resp;
    resp.status = "error";
    resp.message = "Unknown error";
    resp.count = 0;
    resp.data = "[]";

    try
    {
     
        if (req.operation == "insert")
        {
            std::string data = trim(req.data_json);

            if (data.empty())
            {
                resp.message = "INSERT requires 'data'";
                return resp;
            }


            if (data.front() == '[')
            {
                std::vector<std::string> objects;
                if (!splitJsonArrayObjects(data, objects))
                {
                    resp.message = "Invalid JSON array format";
                    return resp;
                }

                for (const auto& obj : objects)
                {
                    db.insertQuery(obj);
                    resp.count++;
                }

                db.saveToDisk();

                resp.status = "success";
                resp.message = "Batch inserted: " + std::to_string(resp.count);
                return resp;
            }

        
            if (data.front() == '{')
            {
                db.insertQuery(data);
                db.saveToDisk();

                resp.status = "success";
                resp.count = 1;
                resp.message = "Inserted 1 document";
                return resp;
            }

            resp.message = "INSERT expects object {} or array []";
            return resp;
        }

       
        if (req.operation == "find")
        {
            std::string query = trim(req.query_json);
            if (query.empty())
                query = "{}";

            std::string json_array;
            size_t count = 0;

            db.findQueryToJsonArray(query, json_array, count);

            resp.status = "success";
            resp.message = "Fetched " + std::to_string(count);
            resp.data = json_array;
            resp.count = count;
            return resp;
        }

        if (req.operation == "delete")
        {
            std::string query = trim(req.query_json);
            if (query.empty())
                query = "{}";

            size_t removed = db.deleteQuery(query);
            db.saveToDisk();

            resp.status = "success";
            resp.message = "Deleted " + std::to_string(removed);
            resp.count = removed;
            resp.data = "[]";
            return resp;
        }

        resp.message = "Unknown operation: " + req.operation;
        return resp;
    }
    catch (const std::exception& e)
    {
        resp.status = "error";
        resp.message = e.what();
        resp.count = 0;
        resp.data = "[]";
        return resp;
    }
}
