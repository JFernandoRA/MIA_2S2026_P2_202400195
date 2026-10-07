#pragma once
#include "parser.hpp"
#include "json.hpp"
#include <fstream>
#include <vector>
#include <ctime>
#include <sys/stat.h>

// Lista de reportes generados con rep; solo estos archivos se sirven al frontend
const std::string REPORT_REGISTRY = "reportes_generados.txt";

struct ReportEntry {
    std::string path, name, id;
    long long date;
};

inline std::vector<ReportEntry> loadReports() {
    std::vector<ReportEntry> out;
    std::ifstream f(REPORT_REGISTRY);
    std::string line;
    while (std::getline(f, line)) {
        std::vector<std::string> parts;
        size_t start = 0, sep;
        while ((sep = line.find('|', start)) != std::string::npos) { parts.push_back(line.substr(start, sep - start)); start = sep + 1; }
        parts.push_back(line.substr(start));
        if (parts.size() == 4) out.push_back({parts[0], parts[1], parts[2], std::atoll(parts[3].c_str())});
    }
    return out;
}

inline void saveReports(const std::vector<ReportEntry>& list) {
    std::ofstream f(REPORT_REGISTRY, std::ios::trunc);
    for (auto& r : list) f << r.path << "|" << r.name << "|" << r.id << "|" << r.date << "\n";
}

// Se llama tras un rep exitoso; si la ruta ya existía, se reemplaza por la versión nueva
inline void registerReport(const ParsedCommand& cmd) {
    if (cmd.name != "rep") return;
    std::vector<ReportEntry> list = loadReports();
    std::string path = getParam(cmd, "path");
    for (size_t i = 0; i < list.size(); i++) if (list[i].path == path) { list.erase(list.begin() + i); break; }
    list.push_back({path, toLower(getParam(cmd, "name")), getParam(cmd, "id"), (long long)time(nullptr)});
    saveReports(list);
}

inline bool isRegisteredReport(const std::string& path) {
    for (auto& r : loadReports()) if (r.path == path) return true;
    return false;
}

inline nlohmann::json reportsJson() {
    nlohmann::json arr = nlohmann::json::array();
    std::vector<ReportEntry> list = loadReports();
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        struct stat st;
        if (stat(it->path.c_str(), &st) != 0) continue;
        size_t dot = it->path.find_last_of('.'), slash = it->path.find_last_of('/');
        std::string ext = dot == std::string::npos ? "" : toLower(it->path.substr(dot + 1));
        arr.push_back({{"path", it->path}, {"file", slash == std::string::npos ? it->path : it->path.substr(slash + 1)},
                       {"name", it->name}, {"id", it->id}, {"ext", ext}, {"size", (long long)st.st_size},
                       {"timestamp", it->date}});
    }
    return arr;
}

inline std::string mimeFor(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : toLower(path.substr(dot + 1));
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "pdf") return "application/pdf";
    if (ext == "gif") return "image/gif";
    return "text/plain; charset=utf-8";
}
