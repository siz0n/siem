#pragma once
#include <string>
#include <vector>
#include <cstdint>

class InotifyTailReader
{
public:
    enum class TruncatePolicy
    {
        ResetToZero,
        SeekToEnd
    };

    InotifyTailReader(std::string path,
                      std::string key = {},
                      std::string stateFile = {},
                      TruncatePolicy tp = TruncatePolicy::ResetToZero);

    ~InotifyTailReader();

    // принудительно начать с конца (игнорируя state)
    void startFromEnd();

    // читать новые строки (inotify + tail)
    bool readNewLines(std::vector<std::string> &outLines, int timeoutMs = 250);

    // debug
    std::string path() const { return path_; }
    unsigned long long inode() const { return inode_; }
    unsigned long long offset() const { return offset_; }

private:
    bool openFile(bool seekEnd);
    void closeFile();

    bool refreshIfRotatedOrRecreated();
    bool readAvailable(std::vector<std::string> &outLines);

    // state (key=value)
    void loadState();
    void saveState() const;

    // helpers for state
    std::string stateKeyPath() const { return key_ + ".path"; }
    std::string stateKeyInode() const { return key_ + ".inode"; }
    std::string stateKeyOffset() const { return key_ + ".offset"; }

private:
    std::string path_;
    std::string key_;
    std::string stateFile_;
    TruncatePolicy truncatePolicy_ = TruncatePolicy::ResetToZero;

    int inotifyFd_ = -1;
    int watchFd_ = -1;

    int fileFd_ = -1;
    unsigned long long inode_ = 0;
    unsigned long long offset_ = 0;
};
