#include "utils.h"
#include <unistd.h>

std::string getHostName()
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
    return "unknown";
}
