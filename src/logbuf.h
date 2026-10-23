// 简易缓冲日志：避免每请求都 fopen/fwrite/fclose（实测每次约 40ms）
// 用法见 logbuf.h
#pragma once
#include <string>
#include <mutex>
#include <vector>
#include <cstdio>
#include <ctime>

class LogBuf {
public:
    explicit LogBuf(const std::string &path, size_t flushLines = 200, int flushSecs = 1)
        : path_(path), maxLines_(flushLines), maxSecs_(flushSecs), last_(time(nullptr)) {}

    ~LogBuf() { flush(); }

    void add(const std::string &line) {
        std::lock_guard<std::mutex> lk(mux_);
        buf_.push_back(line);
        if (buf_.size() >= maxLines_ || time(nullptr) - last_ >= maxSecs_) {
            flushLocked();
        }
    }

    void flush() {
        std::lock_guard<std::mutex> lk(mux_);
        flushLocked();
    }

private:
    void flushLocked() {
        if (buf_.empty()) return;
        FILE *f = fopen(path_.c_str(), "ab");
        if (f) {
            for (size_t i = 0; i < buf_.size(); i++) {
                fwrite(buf_[i].data(), 1, buf_[i].size(), f);
            }
            fclose(f);
        }
        buf_.clear();
        last_ = time(nullptr);
    }

    std::string              path_;
    size_t                   maxLines_;
    int                      maxSecs_;
    time_t                   last_;
    std::mutex               mux_;
    std::vector<std::string> buf_;
};
