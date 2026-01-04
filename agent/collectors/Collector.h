#pragma once
#include <vector>
#include "../core/Event.h"

class Collector {
public:
    virtual ~Collector() = default;

    // добавляет новые события в out
    virtual void poll(std::vector<Event>& out) = 0;
};
