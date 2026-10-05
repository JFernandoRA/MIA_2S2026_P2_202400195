#pragma once
#include "parser.hpp"
#include "commands.hpp"
#include "journal.hpp"
#include "json.hpp"
#include <sstream>
#include <iomanip>

// Devuelve el journal de una partición como JSON (lo consume el frontend)
inline nlohmann::json journalToJson(const std::string& id, std::string& err) {
    nlohmann::json arr = nlohmann::json::array();
    FSContext ctx = resolveFS(id);
    if (!ctx.ok) { err = ctx.error; return arr; }
    if (!isExt3(ctx)) { err = "la partición " + id + " no está formateada en EXT3"; return arr; }
    for (auto& j : readJournal(ctx)) {
        arr.push_back({
            {"count", j.j_count},
            {"operation", fieldStr(j.j_content.i_operation)},
            {"path", fieldStr(j.j_content.i_path)},
            {"content", fieldStr(j.j_content.i_content)},
            {"date", formatJournalDate(j.j_content.i_date)},
            {"timestamp", (long long)journalDateToTime(j.j_content.i_date)}});
    }
    return arr;
}

// ---------------------- JOURNALING ----------------------
inline CmdResult cmdJournaling(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"id"}, perr)) return {false, "JOURNALING: " + perr};
    if (!hasParam(cmd, "id")) return {false, "JOURNALING: falta el parámetro obligatorio -id"};
    std::string err;
    nlohmann::json entries = journalToJson(getParam(cmd, "id"), err);
    if (!err.empty()) return {false, "JOURNALING: " + err};

    std::ostringstream out;
    out << "JOURNALING: " << entries.size() << " transacciones en " << getParam(cmd, "id") << "\n";
    out << std::left << std::setw(5) << "#" << std::setw(11) << "Operación" << " "
        << std::setw(32) << "Path" << std::setw(18) << "Fecha" << "Contenido";
    for (auto& e : entries) {
        std::string content = e["content"].get<std::string>();
        for (auto& c : content) if (c == '\n') c = ' ';
        out << "\n" << std::setw(5) << e["count"].get<int>() << std::setw(10) << e["operation"].get<std::string>()
            << " " << std::setw(32) << e["path"].get<std::string>() << std::setw(18) << e["date"].get<std::string>()
            << (content.empty() ? "-" : content);
    }
    return {true, out.str()};
}
