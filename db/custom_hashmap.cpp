#include "custom_hashmap.h"
#include "utills.h"

using namespace std;

ListNode::ListNode(const string &k, Document *v)
    : key(k), value(v), next(nullptr) {}

CustomList::CustomList() : head(nullptr) {}
CustomList::~CustomList()
{
    ListNode *current = head;
    while (current)
    {
        ListNode *next = current->next;
        delete current;
        current = next;
    }
}
ListNode *CustomList::find(const ::string &key) const
{
    ListNode *current = head;
    while (current)
    {
        if (current->key == key)
        {
            return current;
        }
        current = current->next;
    }
    return nullptr;
}
Document *CustomList::remove(const ::string &key)
{
    ListNode *current = head;
    ListNode *prev = nullptr;

    while (current)
    {
        if (current->key == key)
        {
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                head = current->next;
            }

            Document *removed_value = current->value;
            current->value = nullptr;
            delete current;
            return removed_value;
        }
        prev = current;
        current = current->next;
    }
    return nullptr;
}

CustomHashMap::CustomHashMap(size_t initial_capacity)
{
    if (initial_capacity == 0)
    {
        initial_capacity = DEFAULT_CAPACITY;
    }
    capacity = initial_capacity;
    size = 0;
    buckets = new CustomList[capacity];
}

CustomHashMap::~CustomHashMap()
{
    for (size_t i = 0; i < capacity; i++)
    {
        ListNode *current = buckets[i].head;

        while (current)
        {
            delete current->value;
            current = current->next;
        }
    }
    delete[] buckets;
}

size_t CustomHashMap::_hash(const ::string &key) const
{ // вычисление индекса
    size_t hash_value = 0;
    unsigned int prime = 31;
    for (unsigned char c : key)
    {
        hash_value = hash_value * prime + c;
    }
    return hash_value % capacity;
}

void CustomHashMap::resize_rehash()
{
    // сохраняем старые значения
    size_t old_capacity = capacity;
    CustomList *old_buckets = buckets;

    capacity *= 2;
    size = 0;
    buckets = new CustomList[capacity];

    for (size_t i = 0; i < old_capacity; ++i)
    {
        ListNode *current = old_buckets[i].head;
        while (current)
        {
            string key = current->key;
            Document *value = current->value;

            current->value = nullptr;

            // кладём в новую таблицу
            put(key, value, false);

            current = current->next;
        }
    }
    delete[] old_buckets; // удаление старыъ
}

void CustomHashMap::put(const ::string &key, Document *value, bool delete_on_update)
{
    string cleaned_key = trim(key);
    if ((float)size / capacity >= LOAD_FACTOR)
    {
        resize_rehash();
    }

    size_t index = _hash(cleaned_key);
    ListNode *node = buckets[index].find(cleaned_key); // поиск узла в бакете

    if (node)
    {
        if (delete_on_update)
        {
            delete node->value;
        }
        node->value = value;
        return;
    }
    else
    {
        ListNode *new_node = new ListNode(cleaned_key, value);
        new_node->next = buckets[index].head;
        buckets[index].head = new_node;
        size++;
    }
}

Document *CustomHashMap::get(const ::string &key) const
{
    string cleaned_key = trim(key);
    size_t index = _hash(cleaned_key);
    ListNode *node = buckets[index].find(cleaned_key);
    if (node)
    {
        return node->value;
    }
    else
    {
        return nullptr;
    }
}

Document *CustomHashMap::remove(const ::string &key)
{
    string cleaned_key = trim(key);
    size_t index = _hash(cleaned_key);
    Document *removed = buckets[index].remove(cleaned_key);
    if (removed != nullptr)
        size--;
    return removed;
}

size_t CustomHashMap::getSize() const
{
    return size;
}

size_t CustomHashMap::getCapacity() const
{
    return capacity;
}

ListNode *CustomHashMap::getBucketHead(size_t index) const
{
    if (index < capacity)
    {
        return buckets[index].head;
    }
    return nullptr;
}
