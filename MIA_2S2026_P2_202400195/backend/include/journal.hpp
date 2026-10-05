#pragma once
#include "ext3_structs.hpp"
#include "fs_ops.hpp"
#include "parser.hpp"
#include "session.hpp"
#include <string>
#include <vector>
#include <set>
#include <ctime>

const long JOURNAL_EPOCH = 946684800; // 01/01/2000 00:00 UTC

inline float journalDateNow() {
    return (float)((time(nullptr) - JOURNAL_EPOCH) / 60);
}

inline time_t journalDateToTime(float d) {
    return JOURNAL_EPOCH + (time_t)d * 60;
}

inline std::string formatJournalDate(float d) {
    time_t t = journalDateToTime(d);
    char buf[32];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", localtime(&t));
    return buf;
}

inline bool isExt3(const FSContext& ctx) { return ctx.sb.s_filesystem_type == 3; }

inline long journalStart(const FSContext& ctx) { return ctx.partStart + (long)sizeof(Superblock); }

// En EXT3 hay una entrada de journal por cada inodo (n)
inline int journalCapacity(const FSContext& ctx) { return ctx.sb.s_inodes_count; }

inline long journalOffset(const FSContext& ctx, int i) { return journalStart(ctx) + (long)i * sizeof(Journal); }

// Las entradas se llenan en orden, así que basta una búsqueda binaria de la primera libre
inline int countJournalEntries(const FSContext& ctx) {
    int lo = 0, hi = journalCapacity(ctx);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        Journal j;
        readStruct(ctx.diskPath, journalOffset(ctx, mid), j);
        if (j.j_count != -1) lo = mid + 1; else hi = mid;
    }
    return lo;
}

inline bool appendJournal(const FSContext& ctx, const std::string& op, const std::string& path, const std::string& content) {
    if (!isExt3(ctx)) return false;
    int used = countJournalEntries(ctx);
    if (used >= journalCapacity(ctx)) return false;
    Journal j;
    j.j_count = used + 1;
    strncpy(j.j_content.i_operation, op.c_str(), sizeof(j.j_content.i_operation) - 1);
    strncpy(j.j_content.i_path, path.c_str(), sizeof(j.j_content.i_path) - 1);
    strncpy(j.j_content.i_content, content.c_str(), sizeof(j.j_content.i_content) - 1);
    j.j_content.i_date = journalDateNow();
    return writeStruct(ctx.diskPath, journalOffset(ctx, used), j);
}

inline std::vector<Journal> readJournal(const FSContext& ctx) {
    std::vector<Journal> entries;
    int used = countJournalEntries(ctx);
    if (used == 0) return entries;
    entries.resize(used);
    std::ifstream file(ctx.diskPath, std::ios::binary);
    file.seekg(journalStart(ctx));
    file.read(reinterpret_cast<char*>(entries.data()), (long)used * sizeof(Journal));
    return entries;
}

// Lee un campo char[N] que puede no terminar en '\0'
template<size_t N>
inline std::string fieldStr(const char (&f)[N]) { return std::string(f, strnlen(f, N)); }

// Registra en el journal un comando que ya se ejecutó con éxito (solo si la partición es EXT3)
inline void journalCommand(const ParsedCommand& cmd) {
    static const std::set<std::string> logged = {
        "mkdir", "mkfile", "mkgrp", "rmgrp", "mkusr", "rmusr", "chgrp",
        "remove", "rename", "copy", "move", "chown", "chmod", "edit"};
    if (!logged.count(cmd.name)) return;
    Session& sess = currentSession();
    if (!sess.active) return;
    FSContext ctx = resolveFS(sess.partitionId);
    if (!ctx.ok || !isExt3(ctx)) return;

    std::string path = hasParam(cmd, "path") ? getParam(cmd, "path") : "/users.txt";
    std::string content;
    if (cmd.name == "mkfile") {
        int idx = resolvePath(ctx, path);
        if (idx != -1) {
            Inode in;
            readStruct(ctx.diskPath, inodeOffset(ctx, idx), in);
            content = readFileContent(ctx, in);
        }
    } else {
        for (auto& kv : cmd.params) {
            if (kv.first == "path" || kv.first == "pass") continue;
            if (!content.empty()) content += " ";
            content += "-" + kv.first + (kv.second.empty() ? "" : "=" + kv.second);
        }
    }
    appendJournal(ctx, cmd.name, path, content);
}
