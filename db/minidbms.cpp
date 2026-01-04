#include <iostream>
#include <fstream>
#include <sstream>

#include "minidbms.h"
#include "document.h"

using namespace std;

MiniDBMS::MiniDBMS(const string &db_name, const string &db_folder)
    : db_name(db_name), db_folder(db_folder), data_store(), next_id(1) {}
MiniDBMS::~MiniDBMS() {} // у хэша есть свой тут не нужен



string MiniDBMS::generate_id()
{
    string id = to_string(next_id);
    next_id++;
    return (id);
}

string MiniDBMS::get_collection_path() const
{
    return (db_folder + "/" + db_name + ".json");
}

void MiniDBMS::loadFromDisk()
{
    string path = get_collection_path();
    ifstream file(path);
    if (!file.is_open())
    {
        // файла нет — начинаем с пустой базы
        cout << " Файл коллекции не найден. Новая база." << endl;
        next_id = 1;
        return;
    }

    // читаем весь файл в одну строку
    string all;
    {
        string line;
        while (getline(file, line))
        {
            all += line;
        }
    }
    file.close();

    string s = trim(all);
    if (s.empty())
    {
        next_id = 1;
        return;
    }

    if (s.front() != '[' || s.back() != ']')
    {
        cerr << "Некорректный формат файла (ожидался JSON-массив)." << endl;
        next_id = 1;
        return;
    }

    // содержимое между [ и ]
    string content = s.substr(1, s.length() - 2);
    size_t pos = 0;
    long long max_id = 0;

    while (pos < content.size())
    {
        // пропускаем пробелы, табы, переводы строк, запятые
        while (pos < content.size() &&
        (content[pos] == ' ' ||
        content[pos] == '\t' ||
        content[pos] == '\n' ||
        content[pos] == '\r' ||
        content[pos] == ',')) {
        ++pos;
        }
        if (pos >= content.size())
            break;

        if (content[pos] != '{')
        {
            cerr << "Ожидался '{' при разборе массива документов." << endl;
            break;
        }

        // ищем конец объекта по балансу скобок
        size_t start_obj = pos;
        int bracket_count = 0;
        bool found_end = false;

        while (pos < content.size())
        {
            if (content[pos] == '{')
                bracket_count++;
            if (content[pos] == '}')
            {
                bracket_count--;
                if (bracket_count == 0)
                {
                    found_end = true;
                    ++pos; // включаем '}' и двигаем pos на следующий символ
                    break;
                }
            }
            ++pos;
        }

        if (!found_end)
        {
            cerr << "ERROR: Не смогли найти конец JSON-объекта в массиве." << endl;
            break;
        }

        // вырезаем одну строку
        string obj_str = content.substr(start_obj, pos - start_obj);
        obj_str = trim(obj_str);
        if (obj_str.empty())
            continue;

        Document *doc = Document::deserialize(obj_str);
        if (doc)
        {
            data_store.put(doc->_id, doc);
            try
            {
                long long current_id = stoll(doc->_id);
                if (current_id > max_id)
                {
                    max_id = current_id;
                }
            }
            catch (const exception &e)
            {
                cerr << "WARNING: Не удалось преобразовать _id '" << doc->_id
                     << "' в число: " << e.what() << endl;
            }
        }
    }

    next_id = max_id + 1;
    cout << "INFO: Загрузка завершена. Документов: "
         << data_store.getSize()
         << ". next_id = " << next_id << endl;
}

void MiniDBMS::saveToDisk() 
{
    string path = get_collection_path();
    ofstream file(path); // открываем для перезаписи
    if (!file.is_open())
    {
        cerr << "Ошибка открытия файла \n";
        return;
    }

    file << "[\n";

    bool first = true;

    // проход по все бакетам
    for (size_t i = 0; i < data_store.getCapacity(); i++)
    {
        ListNode *current = data_store.getBucketHead(i);
        while (current)
        {
            Document *doc = current->value;
            if (!doc)
            {
                current = current->next;
                continue;
            }

            if (!first)
            {
                file << ",\n";
            }
            first = false;

            file << doc->serialize();

            current = current->next;
        }
    }

    file << "\n]\n";

    file.close();
}

// проверка есть ли в строке цифры или + -
bool MiniDBMS::is_integer_string(const string &s)
{
    string t = trim(s);
    if (t.empty())
        return false;
    size_t i = 0;
    if (t[0] == '+' || t[0] == '-')
    {
        if (t.size() == 1)
            return false;
        i = 1;
    }
    for (; i < t.size(); i++)
    {
        if (t[i] < '0' || t[i] > '9')
            return false;
    }
    return true;
}

static bool like_match_impl(const string &value, const string &pattern, size_t i, size_t j)
{ // patern - шаблон поиска
    if (j == pattern.size())
    {
        return i == value.size();
    }
    char pc = pattern[j]; // текуший символ
    if (pc == '%')        // любой
    {
        return like_match_impl(value, pattern, i, j + 1) ||
               (i < value.size() && like_match_impl(value, pattern, i + 1, j));
    }
    if (pc == '_') //  один символ
    {
        return (i < value.size() &&
                like_match_impl(value, pattern, i + 1, j + 1));
    }
    // Обычный символ – должен совпасть по значению
    return (i < value.size() &&
            value[i] == pc &&
            like_match_impl(value, pattern, i + 1, j + 1));
}

bool MiniDBMS::like_match(const string &value, const string &pattern)
{
    return like_match_impl(value, pattern, 0, 0);
}

// сравниваем одно поле с одним из условий
bool MiniDBMS::match_query_value(const string &doc_value_raw, const string &query_value_obj)
{
    string doc_value = trim(doc_value_raw);       // строковое значение
    string trimmed_query = trim(query_value_obj); // строка с условием

    // постой случай
    if (trimmed_query.empty())
        return false;
    if (trimmed_query.front() != '{')
    { // неявное равеносто (нету)
        if (trimmed_query.length() >= 2 && trimmed_query.front() == '"' && trimmed_query.back() == '"')
        {
            trimmed_query = trimmed_query.substr(1, trimmed_query.length() - 2); // вырезаем середину
        }
        trimmed_query = trim(trimmed_query);
        // Если оба числа — сравниваем как int, иначе как строки
        if (is_integer_string(doc_value) && is_integer_string(trimmed_query))
        {
            try
            {
                int lhs = stoi(doc_value);
                int rhs = stoi(trimmed_query);
                return lhs == rhs;
            }
            catch (...)
            {
                return false;
            }
        }

        return doc_value == trimmed_query;
    }
    // сложный случай
    auto extract_operator_value = [&](const string &op_key) -> string // лямда функция
    {
        string op_search = "\"" + op_key + "\":";
        size_t pos = trimmed_query.find(op_search); // поиск оператора
        if (pos == string::npos)
            return "";

        size_t start_search = pos + op_search.length();
        size_t start_val = trimmed_query.find_first_not_of(" \t\n\r", start_search);
        if (start_val == string::npos)
            return "";

        if (trimmed_query[start_val] == '"')
        {
            // строка
            size_t start_content = start_val + 1;
            size_t end_content = trimmed_query.find('"', start_content);
            if (end_content == string::npos)
                return "";
            return trim(trimmed_query.substr(start_content, end_content - start_content));
        }
        else
        {
            // число или сырой литерал
            size_t end_val = trimmed_query.find_first_of(",}", start_val);
            if (end_val == string::npos)
                return "";
            return trim(trimmed_query.substr(start_val, end_val - start_val));
        }
    };

    bool has_any_operator = false;
    bool result = true;

    // --- $eq (явное равенство) ---
    string eq_val_str = extract_operator_value("$eq");
    if (!eq_val_str.empty())
    {
        has_any_operator = true;
        if (is_integer_string(doc_value) && is_integer_string(eq_val_str))
        {
            try
            {
                int lhs = stoi(doc_value);
                int rhs = stoi(eq_val_str);
                result = result && (lhs == rhs);
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            result = result && (doc_value == eq_val_str);
        }
    }

    // --- $gt ---
    string gt_val_str = extract_operator_value("$gt");
    if (!gt_val_str.empty())
    {
        has_any_operator = true;
        if (is_integer_string(doc_value) && is_integer_string(gt_val_str))
        {
            try
            {
                int lhs = stoi(doc_value);
                int rhs = stoi(gt_val_str);
                result = result && (lhs > rhs);
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            result = result && (doc_value > gt_val_str);
        }
    }

    // --- $lt ---
    string lt_val_str = extract_operator_value("$lt");
    if (!lt_val_str.empty())
    {
        has_any_operator = true;
        if (is_integer_string(doc_value) && is_integer_string(lt_val_str))
        {
            try
            {
                int lhs = stoi(doc_value);
                int rhs = stoi(lt_val_str);
                result = result && (lhs < rhs);
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            result = result && (doc_value < lt_val_str);
        }
    }

    // --- $like ---
    string like_val_str = extract_operator_value("$like");
    if (!like_val_str.empty())
    {
        has_any_operator = true;
        result = result && like_match(doc_value, like_val_str);
    }

    // --- $in ---
    string in_search = "\"$in\":";
    size_t in_pos = trimmed_query.find(in_search);
    if (in_pos != string::npos)
    {
        has_any_operator = true;

        size_t array_start = trimmed_query.find('[', in_pos + in_search.length());
        if (array_start == string::npos)
            return false;
        size_t array_end = trimmed_query.find(']', array_start);
        if (array_end == string::npos)
            return false;

        string array_content =
            trimmed_query.substr(array_start + 1, array_end - array_start - 1);

        stringstream ss(array_content);
        string item;
        bool in_result = false;

        while (getline(ss, item, ','))
        {
            string trimmed_item = trim(item);

            if (trimmed_item.length() >= 2 &&
                trimmed_item.front() == '"' &&
                trimmed_item.back() == '"')
            {
                trimmed_item =
                    trimmed_item.substr(1, trimmed_item.length() - 2);
                trimmed_item = trim(trimmed_item);
            }
            else
            {
                trimmed_item = trim(trimmed_item);
            }

            if (is_integer_string(doc_value) && is_integer_string(trimmed_item))
            {
                try
                {
                    int lhs = stoi(doc_value);
                    int rhs = stoi(trimmed_item);
                    if (lhs == rhs)
                    {
                        in_result = true;
                        break;
                    }
                }
                catch (...)
                {
                    // игнорируем и считаем неравными
                }
            }
            else if (doc_value == trimmed_item)
            {
                in_result = true;
                break;
            }
        }

        result = result && in_result;
    }

    if (!has_any_operator)
    {
        // В объекте нет ни одного из известных операторов –

        return false;
    }

    return result;
}

bool MiniDBMS::match_and_query(const Document *doc, const string &query_json)
{
    string query = trim(query_json);
    if (query.empty() || query == "{}")
        return true;

    // ожидаем объект вида {...}
    if (query.front() != '{' || query.back() != '}')
        return false;

    // убираем внешние скобки
    string content = query.substr(1, query.length() - 2);
    size_t current_pos = 0;

    while (current_pos < content.length())
    {
        // пропускаем пробелы, табы и запятые
        while (current_pos < content.size() &&
        (content[current_pos] == ' ' ||
        content[current_pos] == '\t' ||
        content[current_pos] == '\n' ||
        content[current_pos] == '\r' ||
        content[current_pos] == ',')) {
        ++current_pos;
        }
        if (current_pos >= content.length())
            break;

        // парсим имя поля value(city)
        size_t start_key = content.find('"', current_pos);
        if (start_key == string::npos)
            break;

        size_t end_key = content.find('"', start_key + 1);
        if (end_key == string::npos)
            return false; // кривой JSON "name":

        string field_name = content.substr(start_key + 1, end_key - start_key - 1);

        // ищем начало этого значения

        size_t start_val_search = content.find(':', end_key);
        if (start_val_search == string::npos)
            return false;

        size_t val_start_char = content.find_first_not_of(" \t", start_val_search + 1);
        if (val_start_char == string::npos)
            return false;

        // определяем конец значения
        size_t end_val = string::npos;
        char first_char = content[val_start_char];

        if (first_char == '{' || first_char == '[')
        {
            // объект или массив
            char open_char = first_char;
            char close_char = (open_char == '{') ? '}' : ']';

            int bracket_count = 0;
            end_val = val_start_char;
            bool found_end = false;

            while (end_val < content.length())
            {
                if (content[end_val] == open_char)
                    bracket_count++;
                if (content[end_val] == close_char)
                {
                    bracket_count--;
                    if (bracket_count == 0)
                    {
                        found_end = true;
                        break;
                    }
                }
                end_val++;
            }

            if (!found_end)
                return false;
        }
        else if (first_char == '"')
        {
            // строка
            size_t end_quote = content.find('"', val_start_char + 1);
            if (end_quote == string::npos)
                return false;
            end_val = end_quote;
        }
        else
        {
            // число или литерал
            size_t separator_pos = content.find_first_of(",}", val_start_char);
            size_t boundary = (separator_pos == string::npos)
                                  ? content.length()
                                  : separator_pos;

            end_val = boundary - 1;
            while (end_val > val_start_char &&
                   (content[end_val] == ' ' || content[end_val] == '\t'))
            {
                end_val--;
            }
        }

        if (end_val == string::npos || end_val < val_start_char)
            return false;

        // само значение условия
        size_t length = end_val - val_start_char + 1;
        string condition_value = content.substr(val_start_char, length);

        // ---- достаём значение поля из документа
        string doc_value_raw;
        if (field_name == "_id")
        {
            doc_value_raw = doc->_id;
        }
        else
        {
            if (!doc->getField(field_name, doc_value_raw))
            {
                // поля нет - документ не удовлетворяет AND
                return false;
            }
        }

        // ---- проверяем одно условие ----
        if (!match_query_value(doc_value_raw, condition_value))
        {
            return false;
        }

        // двигаемся дальше
        current_pos = end_val + 1;
    }

    return true;
}

// обработка $or: {"$or": [ { ... }, { ... }, ... ]}
bool MiniDBMS::handle_or_query(const Document *doc, const string &query_json)
{
    string search_key = "\"$or\":";
    size_t pos = query_json.find(search_key);
    if (pos == string::npos)
        return false;

    // Находим границы массива условий
    size_t array_start = query_json.find('[', pos + search_key.length());
    if (array_start == string::npos)
        return false;

    size_t array_end = query_json.find_last_of(']');
    if (array_end == string::npos || array_end < array_start)
        return false;

    string array_content = query_json.substr(array_start + 1, array_end - array_start - 1); // убираем скобки

    size_t current_pos = 0;
    bool has_any_condition = false;

    while (current_pos < array_content.length())
    {
        // ищем начало объекта-условия
        size_t start_cond = array_content.find('{', current_pos);
        if (start_cond == string::npos)
            break;

        size_t end_cond = start_cond;
        int bracket_count = 0;
        bool found_end = false;

        // ищем конец объекта с учётом вложенных { }
        while (end_cond < array_content.length())
        {
            if (array_content[end_cond] == '{')
                bracket_count++;
            if (array_content[end_cond] == '}')
            {
                bracket_count--;
                if (bracket_count == 0)
                {
                    found_end = true;
                    break;
                }
            }
            end_cond++;
        }

        if (!found_end)
            return false;

        string sub_query =
            array_content.substr(start_cond, end_cond - start_cond + 1);
        has_any_condition = true;

        // каждое подусловие — это полноценный подзапрос (там может быть и $and, и $or)
        if (match_document(doc, trim(sub_query)))
        {
            // для $or достаточно одного совпадения
            return true;
        }

        current_pos = end_cond + 1;
    }

    if (!has_any_condition)
        return false;

    // ни одно условие не совпало
    return false;
}

// обработка $and: {"$and": [ { ... }, { ... }, ... ]}
bool MiniDBMS::handle_and_query(const Document *doc, const string &query_json)
{
    string search_key = "\"$and\":";
    size_t pos = query_json.find(search_key);
    if (pos == string::npos)
        return false;

    // Находим границы массива условий
    size_t array_start = query_json.find('[', pos + search_key.length());
    if (array_start == string::npos)
        return false;

    size_t array_end = query_json.find_last_of(']');
    if (array_end == string::npos || array_end < array_start)
        return false;

    string array_content = query_json.substr(array_start + 1, array_end - array_start - 1); // чистим скобки

    size_t current_pos = 0;
    bool has_any_condition = false;

    while (current_pos < array_content.length())
    {
        size_t start_cond = array_content.find('{', current_pos);
        if (start_cond == string::npos)
            break;

        size_t end_cond = start_cond;
        int bracket_count = 0;
        bool found_end = false;

        while (end_cond < array_content.length())
        {
            if (array_content[end_cond] == '{')
                bracket_count++;
            if (array_content[end_cond] == '}')
            {
                bracket_count--;
                if (bracket_count == 0)
                {
                    found_end = true;
                    break;
                }
            }
            end_cond++;
        }

        if (!found_end)
            return false;

        string sub_query = array_content.substr(start_cond, end_cond - start_cond + 1);
        has_any_condition = true;

        // КАЖДОЕ подусловие было true
        if (!match_document(doc, trim(sub_query)))
        {
            return false;
        }

        current_pos = end_cond + 1;
    }

    // Если не было считаем что документ не подходит
    if (!has_any_condition)
        return false;

    return true;
}

// решает как вывести
bool MiniDBMS::match_document(const Document *doc, const string &query_json)
{
    string query = trim(query_json);
    if (query.empty() || query == "{}")
        return true;

    // если это объект смотрим на первый ключ
    if (!query.empty() && query.front() == '{')
    {
        size_t first_quote = query.find('"');
        if (first_quote != string::npos)
        {
            size_t second_quote = query.find('"', first_quote + 1);
            if (second_quote != string::npos)
            {
                string first_key = query.substr(first_quote + 1, second_quote - first_quote - 1);

                if (first_key == "$or")
                {
                    return handle_or_query(doc, query);
                }
                if (first_key == "$and")
                {
                    return handle_and_query(doc, query);
                }
            }
        }
    }

    // по умолчанию — неявный AND
    return match_and_query(doc, query);
}

// вставка нового документа
void MiniDBMS::insertQuery(const string &query_json)
{
    string new_id = generate_id();

    string trimmed = trim(query_json);
    if (trimmed.empty())
    {
        cerr << "ERROR: пустая вставка" << endl;
        return;
    }
    if (trimmed.front() != '{')
    {
        cerr << "ERROR: не правильнный ввод " << trimmed << endl;
        return;
    }

    // убираем первую '{' и подставляем "_id" первым полем
    string without_first_brace = trimmed.substr(1);
    string full_json = "{\"_id\":\"" + new_id + "\"," + without_first_brace;

    Document *new_doc = Document::deserialize(full_json);
    if (!new_doc)
    {
        cerr << "ERROR: проблема с файлом." << endl;
        return;
    }

    data_store.put(new_doc->_id, new_doc);
    cout << "SUCCESS: Document inserted. ID: " << new_id << endl;
}

void MiniDBMS::findQueryToStream(const string &query_json, ostream &out) // вывод в поток
{
    size_t found_count = 0;
    out << "Результаты поиска:\n";

    // проход по все бакетам
    for (size_t i = 0; i < data_store.getCapacity(); ++i)
    {
        ListNode *current = data_store.getBucketHead(i);
        while (current)
        {
            Document *doc = current->value;
            if (match_document(doc, query_json))
            {
                out << doc->serialize() << "\n";
                found_count++;
            }
            current = current->next;
        }
    }

    out << "Найдено документов: " << found_count << "\n";
}   

void MiniDBMS::findQueryToJsonArray(const string& query_json, string& out_array_json, size_t& out_count) // вывод в JSON-массив
{
    std::string q = trim(query_json);
    if (q.empty())
    {
        q = "{}";
    }

    out_array_json.clear();
    out_array_json.push_back('[');

    bool first = true;
    out_count = 0U;

    const std::size_t cap = data_store.getCapacity();
    for (std::size_t i = 0; i < cap; ++i)
    {
        ListNode* node = data_store.getBucketHead(i); // проход по цепочке
        while (node != nullptr)
        {
            Document* doc = node->value;
            if (doc != nullptr && match_document(doc, q))
            {
                if (!first)
                {
                    out_array_json.push_back(',');
                }
                out_array_json += doc->serialize();
                first = false;
                ++out_count;
            }
            node = node->next;
        }
    }

    out_array_json.push_back(']');
}

// поиск документов по условию
void MiniDBMS::handle_find(const string &query_json)
{
    findQueryToStream(query_json, cout);
}

size_t MiniDBMS::deleteQuery(const std::string &query_json){
    size_t deleted_count = 0;

    myarray ids_to_delete;

    // сначала собираем id всех подходящих документов
    for (size_t i = 0; i < data_store.getCapacity(); ++i)
    {
        ListNode *current = data_store.getBucketHead(i);
        while (current)
        {
            Document *doc = current->value;
            if (match_document(doc, query_json))
            {
                ids_to_delete.push(current->key); // ключ = _id
            }
            current = current->next;
        }
    }

    // потом удаляем их по одному
    for (size_t i = 0; i < ids_to_delete.getSize(); ++i)
    {
        string id = ids_to_delete[i];
        Document *removed_doc = data_store.remove(id);
        if (removed_doc)
        {
            delete removed_doc;
            deleted_count++;
        }
    }

    return deleted_count;
}
// удаление документов по условию
void MiniDBMS::handle_delete(const string &query_json)
{
    cout <<"INFO:Начало удаления документов...\n" <<query_json <<endl;
    size_t deleted_count = deleteQuery(query_json);  
    cout << "Документ удален:" << deleted_count << endl; 
}









