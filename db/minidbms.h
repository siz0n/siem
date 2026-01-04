#pragma once

#include <string>
#include <iosfwd>
#include "custom_hashmap.h"
#include "document.h"
#include "utills.h"

class MiniDBMS
{
private:
    std::string db_name;      // название файла
    std::string db_folder;    // название папки
    CustomHashMap data_store; // memory память
    long long next_id;        // счетчик для айди

    std::string generate_id();
    std::string get_collection_path() const;

    bool is_integer_string(const std::string &s);
    bool like_match(const std::string &value, const std::string &pattern);
    bool match_query_value(const std::string &doc_value_raw, const std::string &query_value_obj);

    bool handle_or_query(const Document *doc, const std::string &query_json);
    bool match_and_query(const Document *doc, const std::string &query_json);
    bool handle_and_query(const Document *doc, const std::string &query_json);
    bool match_document(const Document *doc, const std::string &query_json);

    void handle_find(const std::string &query_json);
    void handle_delete(const std::string &query_json);

public:
    MiniDBMS(const std::string &db_name, const std::string &db_folder = "mydb");
    ~MiniDBMS();

    void loadFromDisk();
    void saveToDisk();

    void insertQuery(const std::string &query_json);
    void findQueryToStream(const std::string &query_json, std::ostream &out);
    std::size_t deleteQuery(const std::string &query_json);
    void findQueryToJsonArray(const std::string& query_json, std::string& out_array_json, std::size_t& out_count);

    void run(const std::string &command, const std::string &query_json);
};