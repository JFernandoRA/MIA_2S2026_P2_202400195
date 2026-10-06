#pragma once
#include "parser.hpp"
#include "structs.hpp"
#include "disk_utils.hpp"
#include "mount_manager.hpp"
#include "session.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>

struct CmdResult {
    bool success;
    std::string message;
};

// Crea las carpetas padre de "path", como mkdir -p
inline void ensureParentDirs(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return;
    std::string dir = path.substr(0, pos);
    std::string current;
    std::stringstream ss(dir);
    std::string part;
    if (!dir.empty() && dir[0] == '/') current = "/";
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        current += part + "/";
        mkdir(current.c_str(), 0755);
    }
}

inline bool fileExists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// ---------------------- MKDISK ----------------------
inline CmdResult cmdMkdisk(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"size", "unit", "fit", "path"}, perr)) return {false, "MKDISK: " + perr};
    if (!hasParam(cmd, "size")) return {false, "MKDISK: falta el parámetro obligatorio -size"};
    if (!hasParam(cmd, "path")) return {false, "MKDISK: falta el parámetro obligatorio -path"};

    int sizeVal;
    try {
        sizeVal = std::stoi(getParam(cmd, "size"));
    } catch (...) {
        return {false, "MKDISK: -size debe ser un número"};
    }
    if (sizeVal <= 0) return {false, "MKDISK: -size debe ser positivo y mayor que cero"};

    std::string unit = toLower(getParam(cmd, "unit", "m"));
    if (unit != "k" && unit != "m") return {false, "MKDISK: -unit inválido, use K o M"};

    std::string fit = toLower(getParam(cmd, "fit", "ff"));
    char fitChar;
    if (fit == "bf") fitChar = 'B';
    else if (fit == "ff") fitChar = 'F';
    else if (fit == "wf") fitChar = 'W';
    else return {false, "MKDISK: -fit inválido, use BF, FF o WF"};

    std::string path = getParam(cmd, "path");

    long bytes = (unit == "k") ? (long)sizeVal * 1024L : (long)sizeVal * 1024L * 1024L;

    ensureParentDirs(path);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return {false, "MKDISK: no se pudo crear el archivo en " + path};

    char buffer[1024] = {0};
    long written = 0;
    while (written < bytes) {
        long toWrite = std::min((long)sizeof(buffer), bytes - written);
        file.write(buffer, toWrite);
        written += toWrite;
    }
    file.close();

    MBR mbr;
    mbr.mbr_tamano = (int)bytes;
    mbr.mbr_fecha_creacion = time(nullptr);
    mbr.mbr_dsk_signature = rand();
    mbr.dsk_fit = fitChar;

    if (!writeMBR(path, mbr)) return {false, "MKDISK: disco creado pero falló al escribir el MBR"};

    return {true, "MKDISK: disco creado correctamente en " + path + " (" + std::to_string(bytes) + " bytes)"};
}

// ---------------------- RMDISK ----------------------
inline CmdResult cmdRmdisk(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"path"}, perr)) return {false, "RMDISK: " + perr};
    if (!hasParam(cmd, "path")) return {false, "RMDISK: falta el parámetro obligatorio -path"};
    std::string path = getParam(cmd, "path");
    if (!fileExists(path)) return {false, "RMDISK: el archivo " + path + " no existe"};
    if (std::remove(path.c_str()) != 0) return {false, "RMDISK: no se pudo eliminar " + path};
    return {true, "RMDISK: disco " + path + " eliminado correctamente"};
}

// ---------------------- FDISK ----------------------

// true si alguna partición montada apunta a ese disco y nombre
inline bool isMountedByName(const std::string& path, const std::string& name) {
    for (auto& kv : mountState().mounted)
        if (kv.second.diskPath == path && kv.second.partitionName == name) return true;
    return false;
}

// Rellena con '\0' una región del disco (delete=full)
inline void zeroRegion(const std::string& path, long start, long size) {
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(start);
    std::vector<char> zeros(std::min(size, 1L << 20), 0);
    while (size > 0) {
        long chunk = std::min(size, (long)zeros.size());
        file.write(zeros.data(), chunk);
        size -= chunk;
    }
}

inline CmdResult fdiskDelete(const std::string& path, const std::string& name, const std::string& mode) {
    if (mode != "fast" && mode != "full") return {false, "FDISK: -delete solo acepta fast o full"};
    bool full = (mode == "full");
    if (isMountedByName(path, name)) return {false, "FDISK: la partición \"" + name + "\" está montada, desmóntela primero (unmount)"};

    MBR mbr;
    if (!readMBR(path, mbr)) return {false, "FDISK: no se pudo leer el MBR de " + path};

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_start == -1 || std::string(p.part_name) != name) continue;
        long start = p.part_start, size = p.part_s;
        bool wasExt = (p.part_type == 'E');
        p = Partition();
        if (!writeMBR(path, mbr)) return {false, "FDISK: falló al escribir el MBR"};
        if (full) zeroRegion(path, start, size);
        return {true, "FDISK: partición \"" + name + "\" eliminada (" + mode + ")" +
                      (wasExt ? ", junto con sus particiones lógicas" : "")};
    }

    // Si no es primaria/extendida, se busca entre las lógicas
    for (int i = 0; i < 4; i++) {
        Partition& ext = mbr.mbr_partitions[i];
        if (ext.part_start == -1 || ext.part_type != 'E') continue;
        long prev = -1, cursor = ext.part_start;
        while (cursor != -1) {
            EBR ebr;
            readEBR(path, cursor, ebr);
            if (ebr.part_start != -1 && std::string(ebr.part_name) == name) {
                long dataStart = ebr.part_start, dataSize = ebr.part_s;
                if (prev == -1) {
                    // el primer EBR se queda en su lugar (vacío) porque la extendida empieza con él
                    EBR empty;
                    empty.part_next = ebr.part_next;
                    writeEBR(path, cursor, empty);
                } else {
                    EBR prevEbr;
                    readEBR(path, prev, prevEbr);
                    prevEbr.part_next = ebr.part_next;
                    writeEBR(path, prev, prevEbr);
                    if (full) zeroRegion(path, cursor, sizeof(EBR));
                }
                if (full) zeroRegion(path, dataStart, dataSize);
                return {true, "FDISK: partición lógica \"" + name + "\" eliminada (" + mode + ")"};
            }
            prev = cursor;
            cursor = ebr.part_next;
        }
    }
    return {false, "FDISK: no existe una partición llamada \"" + name + "\" en " + path};
}

inline CmdResult fdiskAdd(const std::string& path, const std::string& name, long delta) {
    if (delta == 0) return {false, "FDISK: -add no puede ser 0"};
    MBR mbr;
    if (!readMBR(path, mbr)) return {false, "FDISK: no se pudo leer el MBR de " + path};
    std::string what = delta > 0 ? "agregaron " : "quitaron ";

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_start == -1 || std::string(p.part_name) != name) continue;
        long newSize = (long)p.part_s + delta;
        if (newSize <= 0) return {false, "FDISK: no se puede quitar ese espacio, la partición quedaría con tamaño negativo o cero"};
        if (delta > 0) {
            long limit = mbr.mbr_tamano;
            for (int j = 0; j < 4; j++) {
                const Partition& q = mbr.mbr_partitions[j];
                if (j != i && q.part_start != -1 && q.part_start > p.part_start) limit = std::min(limit, (long)q.part_start);
            }
            if (p.part_start + newSize > limit)
                return {false, "FDISK: no hay espacio libre suficiente después de la partición (disponible: " +
                               std::to_string(limit - p.part_start - p.part_s) + " bytes)"};
        } else if (p.part_type == 'E') {
            long usedEnd = p.part_start + (long)sizeof(EBR);
            long cursor = p.part_start;
            while (cursor != -1) {
                EBR ebr;
                readEBR(path, cursor, ebr);
                if (ebr.part_start != -1) usedEnd = std::max(usedEnd, (long)ebr.part_start + ebr.part_s);
                cursor = ebr.part_next;
            }
            if (p.part_start + newSize < usedEnd) return {false, "FDISK: no se puede reducir la extendida, cortaría sus particiones lógicas"};
        }
        p.part_s = (int)newSize;
        if (!writeMBR(path, mbr)) return {false, "FDISK: falló al escribir el MBR"};
        return {true, "FDISK: se " + what + std::to_string(std::labs(delta)) + " bytes a \"" + name +
                      "\" (nuevo tamaño: " + std::to_string(newSize) + " bytes)"};
    }

    for (int i = 0; i < 4; i++) {
        Partition& ext = mbr.mbr_partitions[i];
        if (ext.part_start == -1 || ext.part_type != 'E') continue;
        long cursor = ext.part_start;
        while (cursor != -1) {
            EBR ebr;
            readEBR(path, cursor, ebr);
            if (ebr.part_start != -1 && std::string(ebr.part_name) == name) {
                long newSize = (long)ebr.part_s + delta;
                if (newSize <= 0) return {false, "FDISK: no se puede quitar ese espacio, la partición quedaría con tamaño negativo o cero"};
                long limit = ebr.part_next != -1 ? ebr.part_next : (long)ext.part_start + ext.part_s;
                if (delta > 0 && ebr.part_start + newSize > limit)
                    return {false, "FDISK: no hay espacio libre suficiente después de la partición lógica"};
                ebr.part_s = (int)newSize;
                writeEBR(path, cursor, ebr);
                return {true, "FDISK: se " + what + std::to_string(std::labs(delta)) + " bytes a \"" + name +
                              "\" (nuevo tamaño: " + std::to_string(newSize) + " bytes)"};
            }
            cursor = ebr.part_next;
        }
    }
    return {false, "FDISK: no existe una partición llamada \"" + name + "\" en " + path};
}

inline CmdResult cmdFdisk(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"size", "unit", "path", "type", "fit", "name", "delete", "add"}, perr)) return {false, "FDISK: " + perr};
    if (!hasParam(cmd, "path")) return {false, "FDISK: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "name")) return {false, "FDISK: falta el parámetro obligatorio -name"};

    std::string path = getParam(cmd, "path");
    if (!fileExists(path)) return {false, "FDISK: el disco " + path + " no existe"};

    std::string name = getParam(cmd, "name");

    // -delete y -add tienen prioridad sobre la creación (se ignora -size)
    if (hasParam(cmd, "delete")) return fdiskDelete(path, name, toLower(getParam(cmd, "delete")));
    if (hasParam(cmd, "add")) {
        std::string u = toLower(getParam(cmd, "unit", "k"));
        long mult = u == "b" ? 1 : u == "k" ? 1024 : u == "m" ? 1024L * 1024L : 0;
        if (mult == 0) return {false, "FDISK: -unit inválido, use B, K o M"};
        long addVal;
        try { addVal = std::stol(getParam(cmd, "add")); }
        catch (...) { return {false, "FDISK: -add debe ser un número"}; }
        return fdiskAdd(path, name, addVal * mult);
    }

    std::string type = toLower(getParam(cmd, "type", "p"));
    char typeChar;
    if (type == "p") typeChar = 'P';
    else if (type == "e") typeChar = 'E';
    else if (type == "l") typeChar = 'L';
    else return {false, "FDISK: -type inválido, use P, E o L"};

    std::string fit = toLower(getParam(cmd, "fit", "wf"));
    char fitChar;
    if (fit == "bf") fitChar = 'B';
    else if (fit == "ff") fitChar = 'F';
    else if (fit == "wf") fitChar = 'W';
    else return {false, "FDISK: -fit inválido, use BF, FF o WF"};

    std::string unit = toLower(getParam(cmd, "unit", "k"));
    long unitMultiplier;
    if (unit == "b") unitMultiplier = 1;
    else if (unit == "k") unitMultiplier = 1024;
    else if (unit == "m") unitMultiplier = 1024L * 1024L;
    else return {false, "FDISK: -unit inválido, use B, K o M"};

    MBR mbr;
    if (!readMBR(path, mbr)) return {false, "FDISK: no se pudo leer el MBR de " + path};

    // Ubica la partición extendida del disco (si existe), para chequear nombres de
    // lógicas y, si type=L, para saber dónde encadenar la nueva lógica.
    int extIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 && mbr.mbr_partitions[i].part_type == 'E') extIndex = i;
    }

    // Validar nombre no repetido: entre particiones primarias/extendida...
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            return {false, "FDISK: ya existe una partición llamada \"" + name + "\" en este disco"};
        }
    }
    // ...y entre las lógicas ya creadas dentro de la extendida (si existe).
    if (extIndex != -1) {
        long cursor = mbr.mbr_partitions[extIndex].part_start;
        while (cursor != -1) {
            EBR ebr;
            readEBR(path, cursor, ebr);
            if (ebr.part_start != -1 && std::string(ebr.part_name) == name) {
                return {false, "FDISK: ya existe una partición lógica llamada \"" + name + "\" en este disco"};
            }
            cursor = ebr.part_next;
        }
    }

    // -size es obligatorio al crear (P, E o L)
    if (!hasParam(cmd, "size")) return {false, "FDISK: falta el parámetro obligatorio -size"};
    int sizeVal;
    try {
        sizeVal = std::stoi(getParam(cmd, "size"));
    } catch (...) {
        return {false, "FDISK: -size debe ser un número"};
    }
    if (sizeVal <= 0) return {false, "FDISK: -size debe ser positivo y mayor que cero"};
    int neededBytes = (int)(sizeVal * unitMultiplier);

    if (typeChar == 'L') {
        if (extIndex == -1) return {false, "FDISK: no existe una partición extendida en este disco"};
        Partition& ext = mbr.mbr_partitions[extIndex];

        long cursor = ext.part_start;
        EBR ebr;
        readEBR(path, cursor, ebr);
        while (ebr.part_next != -1) {
            cursor = ebr.part_next;
            readEBR(path, cursor, ebr);
        }

        if (ebr.part_start == -1) {
            // Primer EBR de la extendida, todavía vacío: se llena en el mismo lugar.
            long dataStart = cursor + (long)sizeof(EBR);
            if (dataStart + neededBytes > ext.part_start + (long)ext.part_s)
                return {false, "FDISK: no hay espacio suficiente en la partición extendida"};
            ebr.part_mount = '0';
            ebr.part_fit = fitChar;
            ebr.part_start = dataStart;
            ebr.part_s = neededBytes;
            ebr.part_next = -1;
            memset(ebr.part_name, 0, sizeof(ebr.part_name));
            strncpy(ebr.part_name, name.c_str(), sizeof(ebr.part_name) - 1);
            writeEBR(path, cursor, ebr);
        } else {
            // Ya hay al menos una lógica: se agrega un nuevo EBR justo después de sus datos.
            long newEbrOffset = ebr.part_start + ebr.part_s;
            long newDataStart = newEbrOffset + (long)sizeof(EBR);
            if (newDataStart + neededBytes > ext.part_start + (long)ext.part_s)
                return {false, "FDISK: no hay espacio suficiente en la partición extendida"};

            EBR newEbr;
            newEbr.part_mount = '0';
            newEbr.part_fit = fitChar;
            newEbr.part_start = newDataStart;
            newEbr.part_s = neededBytes;
            newEbr.part_next = -1;
            memset(newEbr.part_name, 0, sizeof(newEbr.part_name));
            strncpy(newEbr.part_name, name.c_str(), sizeof(newEbr.part_name) - 1);
            writeEBR(path, newEbrOffset, newEbr);

            ebr.part_next = newEbrOffset;
            writeEBR(path, cursor, ebr);
        }

        return {true, "FDISK: partición lógica \"" + name + "\" creada correctamente (" +
                      std::to_string(neededBytes) + " bytes)"};
    }

    // Contar particiones existentes (primarias + extendida) y validar restricciones
    int usedSlots = 0;
    bool hasExtended = false;
    int freeSlot = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1) {
            usedSlots++;
            if (mbr.mbr_partitions[i].part_type == 'E') hasExtended = true;
        } else if (freeSlot == -1) {
            freeSlot = i;
        }
    }
    if (usedSlots >= 4) return {false, "FDISK: el disco ya tiene el máximo de 4 particiones (primarias+extendida)"};
    if (typeChar == 'E' && hasExtended) return {false, "FDISK: ya existe una partición extendida en este disco"};
    if (freeSlot == -1) return {false, "FDISK: no hay slots de partición disponibles"};

    // Buscar espacio libre según el fit indicado
    auto gaps = findFreeGaps(mbr);
    int offset = chooseOffsetByFit(gaps, neededBytes, fitChar);
    if (offset == -1) return {false, "FDISK: no hay espacio suficiente en el disco para la partición solicitada"};

    Partition p;
    p.part_status = '0'; // aún no montada
    p.part_type = typeChar;
    p.part_fit = fitChar;
    p.part_start = offset;
    p.part_s = neededBytes;
    strncpy(p.part_name, name.c_str(), sizeof(p.part_name) - 1);
    p.part_correlative = -1;
    memset(p.part_id, 0, sizeof(p.part_id));

    mbr.mbr_partitions[freeSlot] = p;

    if (!writeMBR(path, mbr)) return {false, "FDISK: falló al escribir el MBR actualizado"};

    // Si es extendida, se escribe el primer EBR vacío
    if (typeChar == 'E') {
        EBR ebr;
        if (!writeEBR(path, offset, ebr)) {
            return {false, "FDISK: partición extendida creada pero falló al escribir el EBR inicial"};
        }
    }

    std::string tipoTexto = (typeChar == 'P') ? "primaria" : "extendida";
    return {true, "FDISK: partición " + tipoTexto + " \"" + name + "\" creada correctamente (" +
                  std::to_string(neededBytes) + " bytes, inicia en byte " + std::to_string(offset) + ")"};
}

// ---------------------- MOUNT ----------------------
inline CmdResult cmdMount(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"path", "name"}, perr)) return {false, "MOUNT: " + perr};
    if (!hasParam(cmd, "path")) return {false, "MOUNT: falta el parámetro obligatorio -path"};
    if (!hasParam(cmd, "name")) return {false, "MOUNT: falta el parámetro obligatorio -name"};

    std::string path = getParam(cmd, "path");
    std::string name = getParam(cmd, "name");

    if (!fileExists(path)) return {false, "MOUNT: el disco " + path + " no existe"};

    MBR mbr;
    if (!readMBR(path, mbr)) return {false, "MOUNT: no se pudo leer el MBR de " + path};

    int foundIndex = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            foundIndex = i;
            break;
        }
    }
    if (foundIndex == -1) return {false, "MOUNT: no existe una partición llamada \"" + name + "\" en " + path};

    Partition& p = mbr.mbr_partitions[foundIndex];
    if (p.part_type != 'P') {
        return {false, "MOUNT: solo se permite montar particiones primarias"};
    }

    MountState& st = mountState();
    for (auto& kv : st.mounted) {
        if (kv.second.diskPath == path && kv.second.partitionIndex == foundIndex) {
            return {false, "MOUNT: la partición \"" + name + "\" ya está montada (id " + kv.first + ")"};
        }
    }

    std::string id = generateMountId(path);

    p.part_status = '1';
    p.part_correlative = st.diskNextNumber[path] - 1;
    strncpy(p.part_id, id.c_str(), sizeof(p.part_id));

    if (!writeMBR(path, mbr)) return {false, "MOUNT: falló al actualizar el MBR"};

    MountedPartition mp;
    mp.diskPath = path;
    mp.partitionName = name;
    mp.partitionIndex = foundIndex;
    st.mounted[id] = mp;

    return {true, "MOUNT: partición \"" + name + "\" montada con id " + id};
}

// ---------------------- MOUNTED ----------------------
inline CmdResult cmdMounted(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {}, perr)) return {false, "MOUNTED: " + perr};
    MountState& st = mountState();
    if (st.mounted.empty()) return {true, "MOUNTED: no hay particiones montadas"};

    std::ostringstream oss;
    oss << "MOUNTED: ";
    bool first = true;
    for (auto& kv : st.mounted) {
        if (!first) oss << ", ";
        oss << kv.first;
        first = false;
    }
    return {true, oss.str()};
}

// ---------------------- UNMOUNT ----------------------
inline CmdResult cmdUnmount(const ParsedCommand& cmd) {
    std::string perr;
    if (!validateParams(cmd, {"id"}, perr)) return {false, "UNMOUNT: " + perr};
    if (!hasParam(cmd, "id")) return {false, "UNMOUNT: falta el parámetro obligatorio -id"};
    std::string id = getParam(cmd, "id");

    MountState& st = mountState();
    auto it = st.mounted.find(id);
    if (it == st.mounted.end()) return {false, "UNMOUNT: no existe una partición montada con id " + id};

    MBR mbr;
    if (readMBR(it->second.diskPath, mbr)) {
        Partition& p = mbr.mbr_partitions[it->second.partitionIndex];
        p.part_status = '0';
        p.part_correlative = 0;
        memset(p.part_id, 0, sizeof(p.part_id));
        writeMBR(it->second.diskPath, mbr);
    }
    std::string name = it->second.partitionName;
    st.mounted.erase(it);
    Session& sess = currentSession();
    bool closed = sess.active && sess.partitionId == id;
    if (closed) sess = Session();
    return {true, "UNMOUNT: partición \"" + name + "\" (id " + id + ") desmontada" +
                  (closed ? " y se cerró la sesión activa en ella" : "")};
}
