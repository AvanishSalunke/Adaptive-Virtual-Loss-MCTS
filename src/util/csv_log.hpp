// csv_log.hpp - appends result rows to a CSV file.
// Every row starts with the same run-info columns (time, commit, machine),
// followed by the columns the caller provides. The header is written once,
// when the file is created.
#pragma once
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "run_info.hpp"

// Formats a number with a fixed count of decimals, e.g. fmt(3.14159, 2) = "3.14".
inline std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

// Wraps a field in quotes if it contains a comma, quote or newline.
inline std::string csv_escape(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) {
        if (c == '"') q += '"';  // a quote inside quotes is written twice
        q += c;
    }
    return q + "\"";
}

inline void append_csv(const std::string& path, const RunInfo& info,
                       const std::vector<std::string>& header,
                       const std::vector<std::string>& row) {
    const bool is_new = !std::ifstream(path).good();
    std::ofstream out(path, std::ios::app);
    if (!out) {
        std::fprintf(stderr, "warning: cannot write %s (run from the project root)\n",
                     path.c_str());
        return;
    }
    auto write_line = [&out](const std::vector<std::string>& cols) {
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i) out << ',';
            out << csv_escape(cols[i]);
        }
        out << '\n';
    };

    std::vector<std::string> head = {"timestamp", "git_commit", "cpu_model",
                                     "logical_cpus", "ram_mb", "compiler"};
    head.insert(head.end(), header.begin(), header.end());
    std::vector<std::string> full = {info.timestamp, info.git_commit, info.cpu_model,
                                     std::to_string(info.logical_cpus),
                                     std::to_string(info.ram_mb), info.compiler};
    full.insert(full.end(), row.begin(), row.end());

    if (is_new) write_line(head);
    write_line(full);
}
