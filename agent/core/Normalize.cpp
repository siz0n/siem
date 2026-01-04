#include "Normalize.h"

#include <ctime>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <stdint.h>
#include <cstdio>

#include "Event.h"

using namespace std;

namespace normalize
{

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }

    static int toInt2(const string &s, size_t pos)
    {
        if (pos + 1 >= s.size())
            return -1;
        if (!isDigit(s[pos]) || !isDigit(s[pos + 1]))
            return -1;
        return (s[pos] - '0') * 10 + (s[pos + 1] - '0');
    }

    static int toInt4(const string &s, size_t pos)
    {
        if (pos + 3 >= s.size())
            return -1;
        for (int i = 0; i < 4; ++i)
            if (!isDigit(s[pos + i]))
                return -1;
        return (s[pos] - '0') * 1000 + (s[pos + 1] - '0') * 100 + (s[pos + 2] - '0') * 10 + (s[pos + 3] - '0');
    }

    static string formatUtcZ(const tm &utc, int millis)
    {
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &utc);

        if (millis < 0)
            millis = 0;
        if (millis > 999)
            millis = 999;

        char ms[8];
        snprintf(ms, sizeof(ms), ".%03dZ", millis);

        string out(buf);
        out += ms;
        return out;
    }

    static bool parseBaseDateTime(const string &ts, tm &outTm, size_t &posAfter)
    {

        if (ts.size() < 19)
            return false;
        if (ts[4] != '-' || ts[7] != '-' || (ts[10] != 'T' && ts[10] != ' ') || ts[13] != ':' || ts[16] != ':')
            return false;

        int Y = toInt4(ts, 0);
        int M = toInt2(ts, 5);
        int D = toInt2(ts, 8);
        int h = toInt2(ts, 11);
        int m = toInt2(ts, 14);
        int s = toInt2(ts, 17);

        if (Y < 0 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 60)
            return false;

        tm tm{};
        tm.tm_year = Y - 1900;
        tm.tm_mon = M - 1;
        tm.tm_mday = D;
        tm.tm_hour = h;
        tm.tm_min = m;
        tm.tm_sec = s;

        outTm = tm;
        posAfter = 19;
        return true;
    }

    static int parseMillis(const string &ts, size_t &pos)
    {

        if (pos < ts.size() && ts[pos] == '.')
        {
            ++pos;
            int ms = 0;
            int digits = 0;

            while (pos < ts.size() && isDigit(ts[pos]) && digits < 6)
            { // до микросекунд
                ms = ms * 10 + (ts[pos] - '0');
                ++pos;
                ++digits;
            }

            if (digits == 0)
                return 0;
            if (digits == 1)
                return ms * 100;
            if (digits == 2)
                return ms * 10;
            if (digits >= 3)
            {

                while (digits > 3)
                {
                    ms /= 10;
                    --digits;
                }
                return ms;
            }
        }
        return 0;
    }

    static bool parseTz(const string &ts, size_t pos, int &tzOffsetSeconds, bool &isUtcZ, bool &hasTz)
    {

        hasTz = false;
        isUtcZ = false;
        tzOffsetSeconds = 0;

        if (pos >= ts.size())
            return true;
        if (ts[pos] == 'Z')
        {
            hasTz = true;
            isUtcZ = true;
            tzOffsetSeconds = 0;
            return true;
        }

        if (ts[pos] == '+' || ts[pos] == '-')
        {
            hasTz = true;
            int sign = (ts[pos] == '-') ? -1 : 1;
            if (pos + 5 >= ts.size())
                return false; //
            int hh = toInt2(ts, pos + 1);
            int mm = toInt2(ts, pos + 4);
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59)
                return false;
            if (ts[pos + 3] != ':')
                return false;
            tzOffsetSeconds = sign * (hh * 3600 + mm * 60);
            return true;
        }

        return true;
    }

    string toIsoUtcZ(const string &inTs)
    {
        string ts = inTs;
        // trim spaces
        while (!ts.empty() && isspace((unsigned char)ts.front()))
            ts.erase(ts.begin());
        while (!ts.empty() && isspace((unsigned char)ts.back()))
            ts.pop_back();

        tm tm{};
        size_t pos = 0;
        if (!parseBaseDateTime(ts, tm, pos))
        {
            return inTs;
        }

        int millis = parseMillis(ts, pos);

        bool isZ = false;
        bool hasTz = false;
        int tzOff = 0;
        if (!parseTz(ts, pos, tzOff, isZ, hasTz))
        {
            return inTs;
        }

        time_t epoch = 0;

        if (hasTz)
        {

            time_t baseUtc = timegm(&tm);
            epoch = baseUtc - tzOff;
        }
        else
        {
            epoch = mktime(&tm);
        }

        std::tm outUtc{};
        gmtime_r(&epoch, &outUtc);
        return formatUtcZ(outUtc, millis);
    }

    string sanitizeRaw(const string &s) // удалить управляющие символы и кавычки
    {
        string out;
        out.reserve(s.size());

        for (unsigned char uc : s)
        {
            char c = (char)uc;

            if (uc < 0x20 || uc == 0x7F)
            {
                out.push_back(' ');
                continue;
            }

            if (c == '"' || c == '\\')
            {
                out.push_back(' ');
                continue;
            }

            out.push_back(c);
        }

        while (!out.empty() && isspace((unsigned char)out.back()))
            out.pop_back();
        return out;
    }

    void normalizeEvent(Event &e)
    {
        if (!e.timestamp.empty())
            e.timestamp = toIsoUtcZ(e.timestamp);

        if (!e.raw_log.empty())
            e.raw_log = sanitizeRaw(e.raw_log);

        if (e.source.empty())
            e.source = "unknown";
        if (e.event_type.empty())
            e.event_type = "raw";
        if (e.severity.empty())
            e.severity = "low";
    }

}
