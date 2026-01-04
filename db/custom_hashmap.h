#include <string>
#include "document.h"

struct ListNode
{
    std::string key;
    Document *value;
    ListNode *next;

    ListNode(const std::string &k, Document *v);
};
class CustomList
{
public:
    ListNode *head;

    CustomList();
    ~CustomList();

    ListNode *find(const std::string &key) const;
    Document *remove(const std::string &key);
};

class CustomHashMap
{
private:
    CustomList *buckets; // цепочки
    size_t capacity;
    size_t size;

    static const size_t DEFAULT_CAPACITY = 16;
    static constexpr float LOAD_FACTOR = 0.75f;

    size_t _hash(const std::string &key) const;
    void resize_rehash();

public:
    CustomHashMap(size_t initial_capacity = DEFAULT_CAPACITY);
    ~CustomHashMap();

    void put(const std::string &key, Document *value, bool delete_on_update = true);
    Document *get(const std::string &key) const;
    Document *remove(const std::string &key);

    size_t getSize() const;
    size_t getCapacity() const;

    ListNode *getBucketHead(size_t index) const;
};
