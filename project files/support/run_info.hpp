// run_info.hpp - collects "where and when was this measured" facts, so every
// result row can be traced back to a machine and a code version.
// Linux only (reads /proc and calls git through the shell).
#pragma once
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

struct RunInfo {
    std::string timestamp;
    std::string git_commit;
    std::string cpu_model;
    std::string compiler;
    int logical_cpus = 0;
    long ram_mb = 0;
};

// Runs a shell command and returns the first line it prints ("" on failure).
inline std::string first_line_of_command(const char* cmd) {
    std::string out;
    if (FILE* p = popen(cmd, "r")) {
        char buf[256];
        if (std::fgets(buf, sizeof buf, p)) out = buf;
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

inline std::string read_cpu_model() {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("model name", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos && colon + 2 <= line.size())
                return line.substr(colon + 2);
        }
    }
    return "unknown";
}

inline long read_ram_mb() {
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line)) {
        long kb = 0;
        if (std::sscanf(line.c_str(), "MemTotal: %ld kB", &kb) == 1) return kb / 1024;
    }
    return 0;
}

// Short commit hash. A "-dirty" suffix means there are uncommitted changes
// outside results/, so the numbers may not match that commit exactly.
inline std::string read_git_commit() {
    std::string hash = first_line_of_command("git rev-parse --short HEAD 2>/dev/null");
    if (hash.empty()) return "unknown";
    if (!first_line_of_command("git status --porcelain -- . ':!results' 2>/dev/null").empty())
        hash += "-dirty";
    return hash;
}

inline RunInfo collect_run_info() {
    RunInfo info;
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
    info.timestamp = buf;
    info.git_commit = read_git_commit();
    info.cpu_model = read_cpu_model();
    info.compiler = std::string("g++ ") + __VERSION__;
    info.logical_cpus = static_cast<int>(std::thread::hardware_concurrency());
    info.ram_mb = read_ram_mb();
    return info;
}
