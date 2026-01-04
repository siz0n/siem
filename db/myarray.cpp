#include "myarray.h"

using namespace std;

void myarray::resize(size_t new_capacity)
{
    if (new_capacity <= capacity)
        return;
    string *new_data = new string[new_capacity];
    for (size_t i = 0; i < size; i++)
    {
        new_data[i] = data[i];
    }
    delete[] data;
    data = new_data;
    capacity = new_capacity;
}

myarray::myarray(size_t initial_capacity)
{
    if (initial_capacity == 0)
    {
        initial_capacity = 1;
    }
    capacity = initial_capacity;
    size = 0;
    data = new string[capacity];
}

myarray::~myarray()
{
    delete[] data;
}

void myarray::push(const std::string &value)
{
    if (size == capacity)
    {
        resize(capacity * 2);
    }
    data[size] = value;
    size++;
}
size_t myarray::getSize() const
{
    return size;
}
string &myarray::operator[](size_t index)
{
    return data[index];
}
const string &myarray::operator[](size_t index) const
{
    return data[index];
}