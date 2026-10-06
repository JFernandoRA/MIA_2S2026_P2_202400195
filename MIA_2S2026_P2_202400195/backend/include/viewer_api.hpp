#pragma once
#include "commands.hpp"
#include "account_commands.hpp"
#include "journal.hpp"
#include "json.hpp"
#include "rep_utils.hpp"
#include <fstream>
#include <set>

using nlohmann::json;

const std::string DISK_REGISTRY = "discos_registrados.txt"; // discos creados con mkdisk, persiste entre reinicios

inline std::set<std::string> loadDiskRegistry() {
    std::set<std::string> disks;
    std::ifstream f(DISK_REGISTRY);
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) disks.insert(line);
    return disks;
}

inline void saveDiskRegistry(const std::set<std::string>& disks) {
    std::ofstream f(DISK_REGISTRY, std::ios::trunc);
    for (auto& d : disks) f << d << "\n";
}

// Se llama tras un mkdisk/rmdisk exitoso
inline void updateDiskRegistry(const ParsedCommand& cmd) {
    if (cmd.name != "mkdisk" && cmd.name != "rmdisk") return;
    std::set<std::string> disks = loadDiskRegistry();
    if (cmd.name == "mkdisk") disks.insert(getParam(cmd, "path"));
    else disks.erase(getParam(cmd, "path"));
    saveDiskRegistry(disks);
}

inline std::string fitName(char f) {
    return f == 'B' ? "Best Fit" : f == 'F' ? "First Fit" : "Worst Fit";
}

inline std::string mountedIdFor(const std::string& disk, const std::string& name) {
    for (auto& kv : mountState().mounted)
        if (kv.second.diskPath == disk && kv.second.partitionName == name) return kv.first;
    return "";
}

// Lee el superbloque al inicio de la partición para saber si está formateada
inline std::string fsTypeAt(const std::string& disk, long start) {
    Superblock sb;
    if (!readStruct(disk, start, sb) || sb.s_magic != 0xEF53) return "sin formato";
    return sb.s_filesystem_type == 3 ? "EXT3" : "EXT2";
}

inline json partitionJson(const std::string& disk, const std::string& name, char type, char fit, long start, long size) {
    std::string id = mountedIdFor(disk, name);
    return {{"name", name}, {"type", type == 'P' ? "Primaria" : type == 'E' ? "Extendida" : "Lógica"},
            {"fit", fitName(fit)}, {"start", start}, {"size", size}, {"id", id},
            {"status", id.empty() ? "Desmontada" : "Montada"},
            {"fs", type == 'E' ? "-" : fsTypeAt(disk, start)}};
}

inline json diskJson(const std::string& path) {
    MBR mbr;
    readMBR(path, mbr);
    json parts = json::array();
    int mountedCount = 0;
    for (auto& p : mbr.mbr_partitions) {
        if (p.part_start == -1) continue;
        parts.push_back(partitionJson(path, fieldStr(p.part_name), p.part_type, p.part_fit, p.part_start, p.part_s));
        if (p.part_type != 'E') continue;
        long cursor = p.part_start;
        while (cursor != -1) {
            EBR ebr;
            readEBR(path, cursor, ebr);
            if (ebr.part_start != -1)
                parts.push_back(partitionJson(path, fieldStr(ebr.part_name), 'L', ebr.part_fit, ebr.part_start, ebr.part_s));
            cursor = ebr.part_next;
        }
    }
    for (auto& p : parts) if (p["status"] == "Montada") mountedCount++;
    size_t slash = path.find_last_of('/');
    return {{"path", path}, {"name", slash == std::string::npos ? path : path.substr(slash + 1)},
            {"size", mbr.mbr_tamano}, {"fit", fitName(mbr.dsk_fit)}, {"date", formatTime(mbr.mbr_fecha_creacion)},
            {"signature", mbr.mbr_dsk_signature}, {"mounted", mountedCount}, {"partitions", parts}};
}

// Discos registrados más los que tengan particiones montadas, solo si el archivo aún existe
inline json disksJson() {
    std::set<std::string> disks = loadDiskRegistry();
    for (auto& kv : mountState().mounted) disks.insert(kv.second.diskPath);
    json arr = json::array();
    for (auto& d : disks) if (fileExists(d)) arr.push_back(diskJson(d));
    return arr;
}

inline std::string permString(const Inode& in) {
    static const char* rwx[] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    std::string s = in.i_type == '0' ? "d" : "-";
    for (int i = 0; i < 3; i++) {
        int d = in.i_perm[i] - '0';
        s += (d >= 0 && d <= 7) ? rwx[d] : "???";
    }
    return s;
}

// Lista el contenido de una carpeta para el visualizador (solo lectura)
inline json listFolderJson(const std::string& id, const std::string& path, std::string& err) {
    json arr = json::array();
    FSContext ctx = resolveFS(id);
    if (!ctx.ok) { err = ctx.error; return arr; }
    int idx = resolvePath(ctx, path);
    if (idx == -1) { err = "la ruta " + path + " no existe"; return arr; }
    Inode folder;
    readStruct(ctx.diskPath, inodeOffset(ctx, idx), folder);
    if (folder.i_type != '0') { err = path + " no es una carpeta"; return arr; }

    std::vector<UserRecord> records;
    int uIdx; Inode uIn;
    loadUsersFile(ctx, records, uIdx, uIn);
    auto ownerName = [&](int uid) { for (auto& r : records) if (r.type == 'U' && r.id == uid) return r.user; return std::to_string(uid); };
    auto groupName = [&](int gid) { for (auto& r : records) if (r.type == 'G' && r.id == gid) return r.group; return std::to_string(gid); };

    for (int i = 0; i < 12; i++) {
        if (folder.i_block[i] == -1) continue;
        FolderBlock fb;
        readStruct(ctx.diskPath, blockOffset(ctx, folder.i_block[i]), fb);
        for (auto& e : fb.b_content) {
            std::string name = fieldStr(e.b_name);
            if (e.b_inodo == -1 || name == "." || name == "..") continue;
            Inode in;
            readStruct(ctx.diskPath, inodeOffset(ctx, e.b_inodo), in);
            arr.push_back({{"name", name}, {"type", in.i_type == '0' ? "carpeta" : "archivo"},
                           {"perm", std::string(in.i_perm, 3)}, {"permStr", permString(in)},
                           {"owner", ownerName(in.i_uid)}, {"group", groupName(in.i_gid)},
                           {"size", in.i_s}, {"inode", e.b_inodo},
                           {"created", formatTime(in.i_ctime)}, {"modified", formatTime(in.i_mtime)}});
        }
    }
    return arr;
}

inline std::string readFileJson(const std::string& id, const std::string& path, std::string& err) {
    FSContext ctx = resolveFS(id);
    if (!ctx.ok) { err = ctx.error; return ""; }
    int idx = resolvePath(ctx, path);
    if (idx == -1) { err = "el archivo " + path + " no existe"; return ""; }
    Inode in;
    readStruct(ctx.diskPath, inodeOffset(ctx, idx), in);
    if (in.i_type != '1') { err = path + " es una carpeta"; return ""; }
    Session& s = currentSession();
    if (!hasPermission(in, s.uid, s.gid, s.user == "root", 4)) { err = "no tiene permiso de lectura sobre " + path; return ""; }
    return readFileContent(ctx, in);
}

// Bitmaps como cadenas de 0/1, para la vista de LOSS
inline json bitmapsJson(const std::string& id, std::string& err) {
    FSContext ctx = resolveFS(id);
    if (!ctx.ok) { err = ctx.error; return json::object(); }
    auto readBm = [&](long start, long count) {
        std::string raw(count, '\0'), out(count, '0');
        std::ifstream f(ctx.diskPath, std::ios::binary);
        f.seekg(start);
        f.read(&raw[0], count);
        for (long i = 0; i < count; i++) if (raw[i] == '1') out[i] = '1';
        return out;
    };
    return {{"fs", ctx.sb.s_filesystem_type == 3 ? "EXT3" : "EXT2"},
            {"inodes", readBm(ctx.sb.s_bm_inode_start, ctx.sb.s_inodes_count)},
            {"blocks", readBm(ctx.sb.s_bm_block_start, ctx.sb.s_blocks_count)}};
}

inline json sessionJson() {
    Session& s = currentSession();
    return {{"active", s.active}, {"user", s.user}, {"group", s.groupName}, {"id", s.partitionId}};
}
