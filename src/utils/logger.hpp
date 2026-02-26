#ifndef LOGGER_HPP
#define LOGGER_HPP
#include "timeex.hpp"

#include <string>
#include <stdint.h>
#include <stdarg.h>
#include <cstdio> // std::snprintf()
#include <stdexcept>
#include <assert.h>
#include <stdio.h>
#include <sstream>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

namespace cpp_streamer
{

#define LOGGER_BUFFER_SIZE (2*1024*1024)

enum LOGGER_LEVEL {
    LOGGER_DEBUG_LEVEL,
    LOGGER_INFO_LEVEL,
    LOGGER_WARN_LEVEL,
    LOGGER_ERROR_LEVEL
};


class Logger
{
public:
    Logger(const std::string filename = "", 
        enum LOGGER_LEVEL level = LOGGER_INFO_LEVEL,
        bool async = false):filename_(filename)
    , level_(level)
    {
        async_ = async;
        buffer_ = new char[buffer_len_];
        if (async_) {
            running_ = true;
            log_thread_ = std::make_unique<std::thread>(&Logger::LogThread, this);
        }
    }
    ~Logger()
    {
        if (async_) {
            running_ = false;
            log_cv_.notify_one();
            log_thread_->join();
        }
        delete[] buffer_;
        buffer_ = nullptr;
    }

public:
    void SetFilename(const std::string& filename) {
        filename_ = filename;
    }
    void SetLevel(enum LOGGER_LEVEL level) {
        level_ = level;
    }

    enum LOGGER_LEVEL GetLevel() {
        return level_;
    }
    void AllocBuffer(size_t len) {
        if (buffer_) {
            delete[] buffer_;
        }
        buffer_ = new char[len];
        buffer_len_ = len;
    }
    bool IsAsync() const {
        return async_;
    }
    char* GetBuffer() {
        return buffer_;
    }
    size_t BufferSize() {
        return buffer_len_;
    }
    void Logf(const char* level, const char* buffer) {
        std::stringstream ss;


        ss << "[" << level << "]" << "[" << get_now_str() << "]"
           << buffer << "\r\n";
        if (async_) {
            InsertLog(ss.str());
            return;
        }
        
        if (filename_.empty()) {
            std::cout << ss.str();
        } else {
            FILE* fp;
#ifdef _WIN64
            errno_t err = fopen_s(&fp, filename_.c_str(), "ab+");
            if (err == 0 && fp != nullptr) {
                fwrite(ss.str().c_str(), ss.str().length(), 1, fp);
                fclose(fp);
            }
#else
            fp = fopen(filename_.c_str(), "ab+");
            if (fp != nullptr) {
                fwrite(ss.str().c_str(), ss.str().length(), 1, fp);
                fclose(fp);
            }
#endif
        }
    }

private:
    void LogThread() {
        std::vector<std::string> logs;
        while (running_)
        {
            logs.clear();
            bool r = GetLogs(logs);
            if (!r) {
                continue;
            }
            FILE* fp = nullptr;
            #ifdef _WIN64
            errno_t err = fopen_s(&fp, filename_.c_str(), "ab+");
            if (err != 0 || fp == nullptr) {
                continue;
            }
            #else
            fp = fopen(filename_.c_str(), "ab+");
            if (fp == nullptr) {
                continue;
            }
            #endif
            for (const auto& log : logs) {
                if (filename_.empty()) {
                    std::cout << log;
                } else {
                    fwrite(log.c_str(), log.length(), 1, fp);
                }
            }
            fclose(fp);
        }
    }
    void InsertLog(const std::string& log) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_queue_.push(log);
        log_cv_.notify_one();
    }
    bool GetLogs(std::vector<std::string>& logs) {
        std::unique_lock<std::mutex> lock(log_mutex_);
        // use log_cv_ to wait for log_queue_ not empty, and running_ is true
        log_cv_.wait(lock, [this] { return !log_queue_.empty() || !running_; });
        if (!running_) {
            return false;
        }

        // pop all log iterm from log_queue_
        while (!log_queue_.empty()) {
            logs.push_back(log_queue_.front());
            log_queue_.pop();
        }

        return true;
    }
private:
    std::string filename_;
    enum LOGGER_LEVEL level_;
    char* buffer_ = nullptr;
    size_t buffer_len_ = LOGGER_BUFFER_SIZE;

private:
    bool async_ = false;
    bool running_ = false;
    std::unique_ptr<std::thread> log_thread_;
    std::mutex log_mutex_;
    std::condition_variable log_cv_;
    std::queue<std::string> log_queue_;
};

inline void LogError(Logger* logger, const char* data) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_INFO_LEVEL) {
        return;
    }
    logger->Logf("W", data);
}

inline void LogErrorf(Logger* logger, const char* fmt, ...) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_ERROR_LEVEL) {
        return;
    }
    char* buffer = nullptr;
    size_t bsize = 0;

    if (logger->IsAsync()) {
        buffer = new char[10*1024];
        bsize = 10*1024;
    } else {
        buffer = logger->GetBuffer();
        bsize = logger->BufferSize();
    }
    va_list ap;
 
    va_start(ap, fmt);
    int ret_len = vsnprintf(buffer, bsize, fmt, ap);
    buffer[ret_len] = 0;
    va_end(ap);

    logger->Logf("E", buffer);
    if (logger->IsAsync()) {
        delete[] buffer;
    }
}

inline void LogWarn(Logger* logger, const char* data) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_INFO_LEVEL) {
        return;
    }
    logger->Logf("W", data);
}

inline void LogWarnf(Logger* logger, const char* fmt, ...) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_WARN_LEVEL) {
        return;
    }
    char* buffer = nullptr;
    size_t bsize = 0;

    if (logger->IsAsync()) {
        buffer = new char[10*1024];
        bsize = 10*1024;
    } else {
        buffer = logger->GetBuffer();
        bsize = logger->BufferSize();
    }
    va_list ap;
 
    va_start(ap, fmt);
    vsnprintf(buffer, bsize, fmt, ap);
    va_end(ap);

    logger->Logf("W", buffer);
    if (logger->IsAsync()) {
        delete[] buffer;
    }
}

inline void LogInfo(Logger* logger, const char* data) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_INFO_LEVEL) {
        return;
    }
    logger->Logf("I", data);
}

inline void LogInfof(Logger* logger, const char* fmt, ...) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_INFO_LEVEL) {
        return;
    }
    char* buffer = nullptr;
    size_t bsize = 0;

    if (logger->IsAsync()) {
        buffer = new char[10*1024];
        bsize = 10*1024;
    } else {
        buffer = logger->GetBuffer();
        bsize = logger->BufferSize();
    }
    va_list ap;
 
    va_start(ap, fmt);
    vsnprintf(buffer, bsize, fmt, ap);
    //int ret_len = vsnprintf(buffer, bsize, fmt, ap);
    //buffer[ret_len] = 0;
    va_end(ap);

    //std::cout << "loginfo size:" << bsize << ", str len:" << ret_len << "\r\n\r\n";
    logger->Logf("I", buffer);
    if (logger->IsAsync()) {
        delete[] buffer;
    }
}

inline void LogDebug(Logger* logger, const char* data) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_DEBUG_LEVEL) {
        return;
    }
    logger->Logf("D", data);
}

inline void LogDebugf(Logger* logger, const char* fmt, ...) {
    if (logger == nullptr || logger->GetLevel() > LOGGER_DEBUG_LEVEL) {
        return;
    }
    char* buffer = nullptr;
    size_t bsize = 0;

    if (logger->IsAsync()) {
        buffer = new char[10*1024];
        bsize = 10*1024;
    } else {
        buffer = logger->GetBuffer();
        bsize = logger->BufferSize();
    }
    va_list ap;
 
    va_start(ap, fmt);
    int ret_len = vsnprintf(buffer, bsize, fmt, ap);
    buffer[ret_len] = 0;
    va_end(ap);

    logger->Logf("D", buffer);
    if (logger->IsAsync()) {
        delete[] buffer;
    }
}

inline void LogInfoData(Logger* logger, const uint8_t* data, size_t len, const char* dscr) {
    if (!logger || logger->GetLevel() > LOGGER_INFO_LEVEL) {
        return;
    }
    const size_t print_buffer_size = 500 * 1024;
    char* print_data = new char[print_buffer_size];

    if (print_data == nullptr) {
        return;
    }
    size_t print_len = 0;
    const int MAX_LINES = 500;
    int line = 0;
    int index = 0;
    print_len += snprintf(print_data, print_buffer_size, "%s:", dscr);
    for (index = 0; index < (int)len; index++) {
        if ((index%16) == 0) {
            print_len += snprintf(print_data + print_len, print_buffer_size - print_len, "\r\n");
            if (++line > MAX_LINES) {
                break;
            }
        }
        print_len += snprintf(print_data + print_len, print_buffer_size - print_len,
            " %02x", *(static_cast<const uint8_t*>(data + index)));
    }


    logger->Logf("I", print_data);

    delete[] print_data;
}

class CppStreamException : public std::exception
{
public:
    explicit CppStreamException(const char* description)
    {
        desc_ = description;
    }

    virtual const char* what() const noexcept { return desc_.c_str(); } 

private:
    std::string desc_;
};

#define CSM_THROW_ERROR(desc, ...) \
    do \
    { \
        char exp_buffer[1024]; \
        std::snprintf(exp_buffer, sizeof(exp_buffer), desc, ##__VA_ARGS__); \
        throw CppStreamException(exp_buffer); \
    } while (false)

}
#endif //LOGGER_HPP