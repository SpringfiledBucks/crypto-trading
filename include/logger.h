// 简单线程安全日志器，写入 stderr（被 systemd 收集到 journal）
#pragma once
#include <string>
#include <mutex>
#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>
#include <optional>

class Logger {
public:
    enum Level { INFO, WARN, ERROR };
    static void init_file(const std::string &path) {
        static std::mutex m;
        std::lock_guard<std::mutex> lk(m);
        file_path = path;
        try {
            file_stream.emplace(path, std::ios::app);
        } catch(...) { file_stream.reset(); }
    }

    static void log(Level lvl, const std::string &msg) {
        static std::mutex m;
        std::lock_guard<std::mutex> lk(m);
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%F %T", std::localtime(&t));
        const char *lvl_s = (lvl==INFO?"INFO":(lvl==WARN?"WARN":"ERROR"));
        std::string line = std::string("[") + buf + "] [" + lvl_s + "] " + msg + "\n";
        // always write to stderr so systemd/journal captures it
        std::cerr << line;
        // if file logging configured, append there as well
        if(file_stream && file_stream->is_open()) {
            (*file_stream) << line;
            file_stream->flush();
        }
    }
    static void info(const std::string &m) { log(INFO, m); }
    static void warn(const std::string &m) { log(WARN, m); }
    static void error(const std::string &m) { log(ERROR, m); }
private:
    static inline std::optional<std::ofstream> file_stream{};
    static inline std::string file_path{};
};
