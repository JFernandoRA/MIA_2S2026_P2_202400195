#pragma once
#include "parser.hpp"
#include "commands.hpp"
#include "fs_ops.hpp"
#include "session.hpp"
#include "account_commands.hpp"
#include "journal.hpp"
#include <functional>
#include <sstream>

// ---------------------- utilidades compartidas ----------------------

inline void releaseInode(FSContext& ctx, int idx) {
    writeByte(ctx.diskPath, ctx.sb.s_bm_inode_start + idx, '0');
    ctx.sb.s_free_inodes_count++;
}

inline void releaseBlock(FSContext& ctx, int idx) {
    writeByte(ctx.diskPath, ctx.sb.s_bm_block_start + idx, '0');
    ctx.sb.s_free_blocks_count++;
}

inline Inode loadInode(const FSContext& ctx, int idx) {
    Inode in;
    readStruct(ctx.diskPath, inodeOffset(ctx, idx), in);
    return in;
}

inline void storeInode(const FSContext& ctx, int idx, const Inode& in) {
    writeStruct(ctx.diskPath, inodeOffset(ctx, idx), in);
}

inline void saveSuperblock(const FSContext& ctx) { writeStruct(ctx.diskPath, ctx.partStart, ctx.sb); }

// Hijos de una carpeta (sin . ni ..), como pares nombre -> inodo
inline std::vector<std::pair<std::string, int>> folderChildren(const FSContext& ctx, const Inode& folder) {
    std::vector<std::pair<std::string, int>> out;
    for (int i = 0; i < 12; i++) {
        if (folder.i_block[i] == -1) continue;
        FolderBlock fb;
        readStruct(ctx.diskPath, blockOffset(ctx, folder.i_block[i]), fb);
        for (auto& e : fb.b_content) {
            std::string name(e.b_name, strnlen(e.b_name, sizeof(e.b_name)));
            if (e.b_inodo == -1 || name == "." || name == "..") continue;
            out.push_back({name, e.b_inodo});
        }
    }
    return out;
}

// Cambia la entrada "name" de una carpeta: nuevo nombre, nuevo inodo, o la borra si newInode == -1
inline bool editFolderEntry(const FSContext& ctx, int folderIdx, const std::string& name,
                            const std::string& newName, int newInode) {
    Inode folder = loadInode(ctx, folderIdx);
    for (int i = 0; i < 12; i++) {
        if (folder.i_block[i] == -1) continue;
        FolderBlock fb;
        readStruct(ctx.diskPath, blockOffset(ctx, folder.i_block[i]), fb);
        for (auto& e : fb.b_content) {
            if (e.b_inodo == -1 || std::string(e.b_name, strnlen(e.b_name, sizeof(e.b_name))) != name) continue;
            memset(e.b_name, 0, sizeof(e.b_name));
            if (newInode != -1) strncpy(e.b_name, newName.c_str(), sizeof(e.b_name) - 1);
            e.b_inodo = newInode;
            writeStruct(ctx.diskPath, blockOffset(ctx, folder.i_block[i]), fb);
            folder.i_mtime = time(nullptr);
            storeInode(ctx, folderIdx, folder);
            return true;
        }
    }
    return false;
}

inline std::string joinPath(const std::string& parent, const std::string& name) {
    return (parent == "/" ? "" : parent) + "/" + name;
}

// Valida sesión y partición; si algo falla deja el mensaje en err
inline bool requireSession(const std::string& cmdName, FSContext& ctx, std::string& err) {
    Session& sess = currentSession();
    if (!sess.active) { err = cmdName + ": no hay una sesión activa, debe iniciar sesión"; return false; }
    ctx = resolveFS(sess.partitionId);
    if (!ctx.ok) { err = cmdName + ": " + ctx.error; return false; }
    return true;
}

inline bool canDo(const Inode& in, int bit) {
    Session& s = currentSession();
    return hasPermission(in, s.uid, s.gid, s.user == "root", bit);
}

inline bool validName(const std::string& name) {
    return !name.empty() && name.size() <= 11 && name.find('/') == std::string::npos && name != "." && name != "..";
}

// ---------------------- REMOVE ----------------------

// Borra recursivamente lo que se pueda; devuelve false si algo quedó por falta de permisos
inline bool removeRec(FSContext& ctx, int idx, const std::string& path, int& removed, std::vector<std::string>& denied) {
    Inode in = loadInode(ctx, idx);
    if (!canDo(in, 2)) { denied.push_back(path); return false; }
    if (in.i_type == '0') {
        bool all = true;
        for (auto& child : folderChildren(ctx, in)) {
            if (removeRec(ctx, child.second, joinPath(path, child.first), removed, denied))
                editFolderEntry(ctx, idx, child.first, "", -1);
            else all = false;
        }
        if (!all) return false;
    }
    for (int i = 0; i < 12; i++) if (in.i_block[i] != -1) releaseBlock(ctx, in.i_block[i]);
    releaseInode(ctx, idx);
    removed++;
    return true;
}

inline CmdResult cmdRemove(const ParsedCommand& cmd) {
    std::string err;
    if (!validateParams(cmd, {"path"}, err)) return {false, "REMOVE: " + err};
    if (!hasParam(cmd, "path")) return {false, "REMOVE: falta el parámetro obligatorio -path"};
    FSContext ctx;
    if (!requireSession("REMOVE", ctx, err)) return {false, err};

    std::string path = getParam(cmd, "path");
    if (path == "/" || path == "/users.txt") return {false, "REMOVE: no se puede eliminar " + path};
    int idx = resolvePath(ctx, path);
    if (idx == -1) return {false, "REMOVE: la ruta " + path + " no existe"};

    std::string parentPath, name;
    splitPath(path, parentPath, name);
    int parentIdx = resolvePath(ctx, parentPath);

    int removed = 0;
    std::vector<std::string> denied;
    bool all = removeRec(ctx, idx, path, removed, denied);
    if (all) editFolderEntry(ctx, parentIdx, name, "", -1);
    saveSuperblock(ctx);

    if (all) return {true, "REMOVE: \"" + path + "\" eliminado (" + std::to_string(removed) + " elementos)"};
    if (removed > 0) journalCommand(cmd);
    std::string list;
    for (auto& d : denied) list += (list.empty() ? "" : ", ") + d;
    return {false, "REMOVE: sin permiso de escritura sobre " + list + "; se eliminaron " + std::to_string(removed) +
                   " elementos y se conservaron sus carpetas padre"};
}

// ---------------------- RENAME ----------------------

inline CmdResult cmdRename(const ParsedCommand& cmd) {
    std::string err;
    if (!validateParams(cmd, {"path", "name"}, err)) return {false, "RENAME: " + err};
    if (!hasParam(cmd, "path")) return {false, "RENAME: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "name")) return {false, "RENAME: falta el parámetro obligatorio -name"};
    FSContext ctx;
    if (!requireSession("RENAME", ctx, err)) return {false, err};

    std::string path = getParam(cmd, "path"), newName = getParam(cmd, "name");
    if (path == "/") return {false, "RENAME: no se puede renombrar la raíz"};
    if (!validName(newName)) return {false, "RENAME: el nombre \"" + newName + "\" no es válido (máximo 11 caracteres, sin /)"};
    int idx = resolvePath(ctx, path);
    if (idx == -1) return {false, "RENAME: la ruta " + path + " no existe"};
    if (!canDo(loadInode(ctx, idx), 2)) return {false, "RENAME: no tiene permiso de escritura sobre " + path};

    std::string parentPath, name;
    splitPath(path, parentPath, name);
    int parentIdx = resolvePath(ctx, parentPath);
    if (findInFolder(ctx, parentIdx, newName) != -1) return {false, "RENAME: ya existe \"" + newName + "\" en " + parentPath};

    editFolderEntry(ctx, parentIdx, name, newName, idx);
    return {true, "RENAME: \"" + path + "\" ahora se llama \"" + newName + "\""};
}

// ---------------------- COPY ----------------------

// Copia src dentro de la carpeta destParent con el nombre dado; omite lo que no se pueda leer
inline bool copyRec(FSContext& ctx, int srcIdx, int destParent, const std::string& name, const std::string& path,
                    int& copied, std::vector<std::string>& skipped, std::string& err) {
    Inode src = loadInode(ctx, srcIdx);
    if (!canDo(src, 4)) { skipped.push_back(path); return true; }
    Session& sess = currentSession();

    if (src.i_type == '1') {
        std::string content = readFileContent(ctx, src);
        int ni = allocateInode(ctx);
        if (ni == -1) { err = "no hay inodos libres"; return false; }
        ctx.sb.s_free_inodes_count--;
        Inode copy;
        copy.i_uid = sess.uid; copy.i_gid = sess.gid;
        copy.i_atime = copy.i_ctime = copy.i_mtime = time(nullptr);
        copy.i_type = '1';
        memcpy(copy.i_perm, src.i_perm, 3);
        if (!writeFileContent(ctx, ni, copy, content)) { err = "no hay bloques libres"; return false; }
        Inode parent = loadInode(ctx, destParent);
        if (!addFolderEntry(ctx, destParent, parent, name, ni)) { err = "la carpeta destino está llena"; return false; }
        copied++;
        return true;
    }

    int ni = createFolderUnder(ctx, destParent, name, sess.uid, sess.gid);
    if (ni == -1) { err = "no hay espacio para crear la carpeta " + name; return false; }
    Inode folder = loadInode(ctx, ni);
    memcpy(folder.i_perm, src.i_perm, 3);
    storeInode(ctx, ni, folder);
    copied++;
    for (auto& child : folderChildren(ctx, src))
        if (!copyRec(ctx, child.second, ni, child.first, joinPath(path, child.first), copied, skipped, err)) return false;
    return true;
}

// Resuelve -path y -destino, comunes a COPY y MOVE
inline bool resolveSrcDest(const ParsedCommand& cmd, const std::string& cmdName, FSContext& ctx,
                           int& srcIdx, int& destIdx, std::string& name, std::string& err) {
    if (!validateParams(cmd, {"path", "destino"}, err)) { err = cmdName + ": " + err; return false; }
    if (!hasParam(cmd, "path")) { err = cmdName + ": falta el parámetro obligatorio -path"; return false; }
    if (!hasParam(cmd, "destino")) { err = cmdName + ": falta el parámetro obligatorio -destino"; return false; }
    if (!requireSession(cmdName, ctx, err)) return false;

    std::string path = getParam(cmd, "path"), dest = getParam(cmd, "destino");
    if (path == "/") { err = cmdName + ": no se puede usar la raíz como origen"; return false; }
    srcIdx = resolvePath(ctx, path);
    if (srcIdx == -1) { err = cmdName + ": la ruta " + path + " no existe"; return false; }
    destIdx = resolvePath(ctx, dest);
    if (destIdx == -1) { err = cmdName + ": la carpeta destino " + dest + " no existe"; return false; }
    Inode destIn = loadInode(ctx, destIdx);
    if (destIn.i_type != '0') { err = cmdName + ": el destino " + dest + " no es una carpeta"; return false; }
    if (!canDo(destIn, 2)) { err = cmdName + ": no tiene permiso de escritura sobre " + dest; return false; }
    std::string d = dest == "/" ? "/" : dest + "/";
    if (dest == path || d.rfind(path + "/", 0) == 0) { err = cmdName + ": no se puede colocar una carpeta dentro de sí misma"; return false; }

    std::string parentPath;
    splitPath(path, parentPath, name);
    if (findInFolder(ctx, destIdx, name) != -1) { err = cmdName + ": ya existe \"" + name + "\" en " + dest; return false; }
    return true;
}

inline CmdResult cmdCopy(const ParsedCommand& cmd) {
    FSContext ctx;
    int srcIdx, destIdx;
    std::string name, err;
    if (!resolveSrcDest(cmd, "COPY", ctx, srcIdx, destIdx, name, err)) return {false, err};
    std::string path = getParam(cmd, "path"), dest = getParam(cmd, "destino");
    if (!canDo(loadInode(ctx, srcIdx), 4)) return {false, "COPY: no tiene permiso de lectura sobre " + path};

    int copied = 0;
    std::vector<std::string> skipped;
    bool ok = copyRec(ctx, srcIdx, destIdx, name, path, copied, skipped, err);
    saveSuperblock(ctx);
    if (!ok) return {false, "COPY: " + err + " (se copiaron " + std::to_string(copied) + " elementos)"};

    std::string msg = "COPY: \"" + path + "\" copiado a \"" + dest + "\" (" + std::to_string(copied) + " elementos)";
    if (!skipped.empty()) {
        msg += "; sin permiso de lectura, no se copió:";
        for (auto& s : skipped) msg += " " + s;
    }
    return {true, msg};
}

// ---------------------- MOVE ----------------------

inline CmdResult cmdMove(const ParsedCommand& cmd) {
    FSContext ctx;
    int srcIdx, destIdx;
    std::string name, err;
    if (!resolveSrcDest(cmd, "MOVE", ctx, srcIdx, destIdx, name, err)) return {false, err};
    std::string path = getParam(cmd, "path"), dest = getParam(cmd, "destino");
    Inode src = loadInode(ctx, srcIdx);
    if (!canDo(src, 2)) return {false, "MOVE: no tiene permiso de escritura sobre " + path};

    // Misma partición: solo se cambian las referencias, sin copiar bloques
    Inode destIn = loadInode(ctx, destIdx);
    if (!addFolderEntry(ctx, destIdx, destIn, name, srcIdx)) return {false, "MOVE: la carpeta destino está llena"};
    std::string parentPath, dummy;
    splitPath(path, parentPath, dummy);
    editFolderEntry(ctx, resolvePath(ctx, parentPath), name, "", -1);
    if (src.i_type == '0') editFolderEntry(ctx, srcIdx, "..", "..", destIdx);
    return {true, "MOVE: \"" + path + "\" movido a \"" + dest + "\""};
}

// ---------------------- FIND ----------------------

// ? = un carácter, * = uno o más caracteres
inline bool wildcardMatch(const char* p, const char* s) {
    if (*p == '\0') return *s == '\0';
    if (*p == '*') {
        if (*s == '\0') return false;
        for (const char* t = s + 1; ; t++) {
            if (wildcardMatch(p + 1, t)) return true;
            if (*t == '\0') return false;
        }
    }
    if (*s == '\0') return false;
    if (*p == '?' || *p == *s) return wildcardMatch(p + 1, s + 1);
    return false;
}

// Agrega al árbol las ramas que contienen coincidencias; devuelve cuántas hubo
inline int findRec(const FSContext& ctx, int idx, const std::string& pattern, int depth, std::vector<std::string>& lines) {
    Inode folder = loadInode(ctx, idx);
    int total = 0;
    for (auto& child : folderChildren(ctx, folder)) {
        Inode in = loadInode(ctx, child.second);
        if (!canDo(in, 4)) continue;
        std::string line = std::string(depth * 2, ' ') + "|_ " + child.first + (in.i_type == '0' ? "/" : "");
        size_t mark = lines.size();
        lines.push_back(line);
        bool self = wildcardMatch(pattern.c_str(), child.first.c_str());
        int inside = in.i_type == '0' ? findRec(ctx, child.second, pattern, depth + 1, lines) : 0;
        if (!self && inside == 0) lines.resize(mark);
        total += inside + (self ? 1 : 0);
    }
    return total;
}

inline CmdResult cmdFind(const ParsedCommand& cmd) {
    std::string err;
    if (!validateParams(cmd, {"path", "name"}, err)) return {false, "FIND: " + err};
    if (!hasParam(cmd, "path")) return {false, "FIND: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "name")) return {false, "FIND: falta el parámetro obligatorio -name"};
    FSContext ctx;
    if (!requireSession("FIND", ctx, err)) return {false, err};

    std::string path = getParam(cmd, "path"), pattern = getParam(cmd, "name");
    int idx = resolvePath(ctx, path);
    if (idx == -1) return {false, "FIND: la ruta " + path + " no existe"};
    Inode start = loadInode(ctx, idx);
    if (start.i_type != '0') return {false, "FIND: " + path + " no es una carpeta"};
    if (!canDo(start, 4)) return {false, "FIND: no tiene permiso de lectura sobre " + path};

    std::vector<std::string> lines;
    int found = findRec(ctx, idx, pattern, 1, lines);
    std::string out = "FIND: " + std::to_string(found) + " coincidencias de \"" + pattern + "\" en " + path + "\n" + path;
    for (auto& l : lines) out += "\n" + l;
    return {true, out};
}

// ---------------------- CHOWN / CHMOD ----------------------

// Aplica fn al inodo (y a su contenido si recursive); solo root o el dueño pueden cambiarlo
inline void applyOwned(FSContext& ctx, int idx, const std::string& path, bool recursive,
                       const std::function<void(Inode&)>& fn, int& changed, std::vector<std::string>& denied) {
    Session& sess = currentSession();
    Inode in = loadInode(ctx, idx);
    if (sess.user == "root" || in.i_uid == sess.uid) {
        fn(in);
        storeInode(ctx, idx, in);
        changed++;
    } else {
        denied.push_back(path);
    }
    if (recursive && in.i_type == '0')
        for (auto& child : folderChildren(ctx, in))
            applyOwned(ctx, child.second, joinPath(path, child.first), recursive, fn, changed, denied);
}

inline CmdResult finishOwned(const std::string& cmdName, const std::string& what, int changed, const std::vector<std::string>& denied) {
    if (changed == 0) return {false, cmdName + ": no es propietario de " + denied.front() + " (solo root o el dueño pueden cambiarlo)"};
    std::string msg = cmdName + ": " + what + " (" + std::to_string(changed) + " elementos)";
    if (!denied.empty()) msg += "; se omitieron " + std::to_string(denied.size()) + " elementos de otros propietarios";
    return {true, msg};
}

inline CmdResult cmdChown(const ParsedCommand& cmd) {
    std::string err;
    if (!validateParams(cmd, {"path", "r", "usuario"}, err)) return {false, "CHOWN: " + err};
    if (!hasParam(cmd, "path")) return {false, "CHOWN: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "usuario")) return {false, "CHOWN: falta el parámetro obligatorio -usuario"};
    if (hasParam(cmd, "r") && !getParam(cmd, "r").empty()) return {false, "CHOWN: -r no debe recibir ningún valor"};
    FSContext ctx;
    if (!requireSession("CHOWN", ctx, err)) return {false, err};

    std::string path = getParam(cmd, "path"), user = getParam(cmd, "usuario");
    int idx = resolvePath(ctx, path);
    if (idx == -1) return {false, "CHOWN: la ruta " + path + " no existe"};

    std::vector<UserRecord> records;
    int uIdx; Inode uIn;
    loadUsersFile(ctx, records, uIdx, uIn);
    UserRecord* target = findActiveUser(records, user);
    if (!target) return {false, "CHOWN: el usuario \"" + user + "\" no existe"};

    int newUid = target->id, changed = 0;
    std::vector<std::string> denied;
    applyOwned(ctx, idx, path, hasParam(cmd, "r"), [newUid](Inode& in) { in.i_uid = newUid; }, changed, denied);
    return finishOwned("CHOWN", "propietario de \"" + path + "\" cambiado a " + user, changed, denied);
}

inline CmdResult cmdChmod(const ParsedCommand& cmd) {
    std::string err;
    if (!validateParams(cmd, {"path", "r", "ugo"}, err)) return {false, "CHMOD: " + err};
    if (!hasParam(cmd, "path")) return {false, "CHMOD: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "ugo")) return {false, "CHMOD: falta el parámetro obligatorio -ugo"};
    if (hasParam(cmd, "r") && !getParam(cmd, "r").empty()) return {false, "CHMOD: -r no debe recibir ningún valor"};
    std::string ugo = getParam(cmd, "ugo");
    if (ugo.size() != 3 || ugo.find_first_not_of("01234567") != std::string::npos)
        return {false, "CHMOD: -ugo debe tener 3 dígitos entre 0 y 7 (ej. 764)"};
    FSContext ctx;
    if (!requireSession("CHMOD", ctx, err)) return {false, err};

    std::string path = getParam(cmd, "path");
    int idx = resolvePath(ctx, path);
    if (idx == -1) return {false, "CHMOD: la ruta " + path + " no existe"};

    int changed = 0;
    std::vector<std::string> denied;
    applyOwned(ctx, idx, path, hasParam(cmd, "r"), [ugo](Inode& in) { memcpy(in.i_perm, ugo.c_str(), 3); }, changed, denied);
    return finishOwned("CHMOD", "permisos de \"" + path + "\" cambiados a " + ugo, changed, denied);
}
