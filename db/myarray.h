#pragma once

#include <string>

class myarray
{
private:
    std::string *data;
    size_t capacity;
    size_t size; // занятоe

    void resize(size_t new_capacity);

public:
    myarray(size_t initial_capacity = 10);
    ~myarray();
    void push(const std::string &value);
    size_t getSize() const;
    std::string &operator[](size_t index);
    const std::string &operator[](size_t index) const;
};