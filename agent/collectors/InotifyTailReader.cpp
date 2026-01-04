#include "InotifyTailReader.h"

#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

using namespace std;

static inline void trim(string &s)
{
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();

    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i)
        s.erase(0, i);
}

static bool parseKeyValue(const string &line, string &k, string &v) // парсинг пробела
{
    auto eq = line.find('=');
    if (eq == string::npos)
        return false;
    k = line.substr(0, eq);
    v = line.substr(eq + 1);
    trim(k);
    trim(v);
    return !k.empty();
}

InotifyTailReader::InotifyTailReader(string path,
                                     string key,
                                     string stateFile,
                                     TruncatePolicy tp)
    : path_(move(path)),
      key_(move(key)),
      stateFile_(move(stateFile)),
      truncatePolicy_(tp)
{
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ >= 0)
    {
        watchFd_ = inotify_add_watch(
            inotifyFd_,
            path_.c_str(),
            IN_MODIFY | IN_MOVE_SELF | IN_DELETE_SELF | IN_ATTRIB); // добавили переместили удалили изменили
    }

    // 1) сначала загрузим state (если есть)
    if (!key_.empty() && !stateFile_.empty())
        loadState();

    // 2) потом откроем файл и выставим позицию согласно state
    openFile(false);

    // если файл открылся и offset задан
    if (fileFd_ >= 0 && offset_ > 0)
    {
        // если offset больше файла — обработаем как truncate
        off_t endPos = lseek(fileFd_, 0, SEEK_END);
        if (endPos >= 0 && (unsigned long long)endPos < offset_)
        {
            if (truncatePolicy_ == TruncatePolicy::SeekToEnd)
                offset_ = (unsigned long long)endPos;
            else
                offset_ = 0;
        }
        lseek(fileFd_, (off_t)offset_, SEEK_SET);
    }

    // обязательно обновить inode_
    if (fileFd_ >= 0)
    {
        struct stat st{};
        if (fstat(fileFd_, &st) == 0)
            inode_ = (unsigned long long)st.st_ino;
    }

    if (!key_.empty() && !stateFile_.empty())
        saveState();
}

InotifyTailReader::~InotifyTailReader()
{
    if (!key_.empty() && !stateFile_.empty())
        saveState();

    closeFile();

    if (watchFd_ >= 0 && inotifyFd_ >= 0)
        inotify_rm_watch(inotifyFd_, watchFd_);
    if (inotifyFd_ >= 0)
        close(inotifyFd_);
}

void InotifyTailReader::startFromEnd()
{
    closeFile();
    offset_ = 0;
    inode_ = 0;

    openFile(true);

    if (!key_.empty() && !stateFile_.empty())
        saveState();
}

bool InotifyTailReader::openFile(bool seekEnd)
{
    fileFd_ = ::open(path_.c_str(), O_RDONLY);
    if (fileFd_ < 0)
        return false;

    struct stat st{};
    if (fstat(fileFd_, &st) == 0)
    {
        inode_ = (unsigned long long)st.st_ino;

        off_t pos = 0;
        if (seekEnd)
            pos = lseek(fileFd_, 0, SEEK_END);
        else
            pos = lseek(fileFd_, 0, SEEK_SET);

        if (pos < 0)
            pos = 0;

        // если мы открыли с конца, то offset_ должен стать end
        if (seekEnd)
            offset_ = (unsigned long long)pos;
    }

    return true;
}

void InotifyTailReader::closeFile()
{
    if (fileFd_ >= 0)
    {
        close(fileFd_);
        fileFd_ = -1;
    }
}

void InotifyTailReader::loadState()
{
    ifstream in(stateFile_);
    if (!in.is_open())
        return;

    string wantPathKey = stateKeyPath();
    string wantInodeKey = stateKeyInode();
    string wantOffsetKey = stateKeyOffset();

    string line;
    string savedPath;
    unsigned long long savedInode = 0;
    unsigned long long savedOffset = 0;

    while (getline(in, line))
    {
        trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        string k, v;
        if (!parseKeyValue(line, k, v))
            continue;

        if (k == wantPathKey)
            savedPath = v;
        else if (k == wantInodeKey)
        {
            try
            {
                savedInode = stoull(v);
            }
            catch (...)
            {
            }
        }
        else if (k == wantOffsetKey)
        {
            try
            {
                savedOffset = stoull(v);
            }
            catch (...)
            {
            }
        }
    }

    // применяем то, что нашли
    if (!savedPath.empty() && savedPath != path_)
    {

        return;
    }

    if (savedOffset > 0)
        offset_ = savedOffset;
}

void InotifyTailReader::saveState() const
{
    if (key_.empty() || stateFile_.empty())
        return;

    ifstream in(stateFile_);
    vector<pair<string, string>> kv;

    if (in.is_open())
    {
        string line;
        while (getline(in, line))
        {
            trim(line);
            if (line.empty() || line[0] == '#')
                continue;

            string k, v;
            if (!parseKeyValue(line, k, v))
                continue;

            // не сохраняем старые значения наших ключей
            if (k == stateKeyPath() || k == stateKeyInode() || k == stateKeyOffset())
                continue;

            kv.push_back({k, v});
        }
    }

    kv.push_back({stateKeyPath(), path_});
    kv.push_back({stateKeyInode(), to_string(inode_)});
    kv.push_back({stateKeyOffset(), to_string(offset_)});

    filesystem::create_directories(filesystem::path(stateFile_).parent_path());

    string tmp = stateFile_ + ".tmp";
    {
        ofstream out(tmp, ios::trunc);
        if (!out.is_open())
            return;
        for (const auto &it : kv)
            out << it.first << "=" << it.second << "\n";
    }

    error_code ec;
    filesystem::rename(tmp, stateFile_, ec);
    if (ec)
    {
        // fallback
        remove(stateFile_.c_str());
        filesystem::rename(tmp, stateFile_, ec);
        if (ec)
            remove(tmp.c_str());
    }
}

bool InotifyTailReader::refreshIfRotatedOrRecreated()
{
    struct stat st{};
    if (stat(path_.c_str(), &st) != 0)
    {
        // файла нет - ждём появления
        closeFile();
        inode_ = 0;
        offset_ = 0;
        return false;
    }

    unsigned long long curInode = (unsigned long long)st.st_ino;

    // тот же inode -> возможен truncate
    if (inode_ != 0 && curInode == inode_)
    {
        if (fileFd_ >= 0)
        {
            off_t endPos = lseek(fileFd_, 0, SEEK_END);
            if (endPos >= 0 && (unsigned long long)endPos < offset_)
            {
                // truncate
                if (truncatePolicy_ == TruncatePolicy::SeekToEnd)
                    offset_ = (unsigned long long)endPos;
                else
                    offset_ = 0;
            }
            lseek(fileFd_, (off_t)offset_, SEEK_SET);
        }

        return true;
    }

    // inode поменялся -> rotate/recreate
    closeFile();
    inode_ = 0;
    offset_ = 0;

    bool seekEnd = (truncatePolicy_ == TruncatePolicy::SeekToEnd);

    if (!openFile(seekEnd))
        return false;

    // обновим inode
    if (fileFd_ >= 0)
    {
        struct stat st2{};
        if (fstat(fileFd_, &st2) == 0)
            inode_ = (unsigned long long)st2.st_ino;
    }

    saveState();
    return true;
}

bool InotifyTailReader::readAvailable(vector<string> &outLines)
{
    outLines.clear();

    if (fileFd_ < 0)
    {
        if (!refreshIfRotatedOrRecreated())
            return false;
        if (fileFd_ < 0)
            return false;
    }

    // позиционируемся на offset_
    if (lseek(fileFd_, (off_t)offset_, SEEK_SET) < 0)
    {
        offset_ = 0;
        lseek(fileFd_, 0, SEEK_SET);
    }

    string buf;
    buf.reserve(8192);

    char tmp[4096];
    while (true)
    {
        ssize_t n = ::read(fileFd_, tmp, sizeof(tmp));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            break;
        buf.append(tmp, tmp + n);
    }

    off_t cur = lseek(fileFd_, 0, SEEK_CUR);
    if (cur < 0)
        cur = 0;
    offset_ = (unsigned long long)cur;

    size_t start = 0;
    while (true)
    {
        size_t nl = buf.find('\n', start);
        if (nl == string::npos)
            break;

        string line = buf.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        outLines.push_back(move(line));
        start = nl + 1;
    }

    if (!key_.empty() && !stateFile_.empty())
        saveState();

    return !outLines.empty();
}

bool InotifyTailReader::readNewLines(vector<string> &outLines, int timeoutMs)
{
    outLines.clear();

    refreshIfRotatedOrRecreated();
    if (readAvailable(outLines) && !outLines.empty())
        return true;

    if (inotifyFd_ < 0)
        return false;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(inotifyFd_, &rfds);

    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    int rc = select(inotifyFd_ + 1, &rfds, nullptr, nullptr, &tv);
    if (rc <= 0)
        return false;

    // читаем события (важен факт)
    char evbuf[4096];
    ssize_t n = read(inotifyFd_, evbuf, sizeof(evbuf));
    (void)n;

    refreshIfRotatedOrRecreated();
    return readAvailable(outLines);
}
