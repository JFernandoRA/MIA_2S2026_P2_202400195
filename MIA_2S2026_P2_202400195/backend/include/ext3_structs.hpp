#pragma once
#include <cstring>

#pragma pack(push, 1)

struct Information {
    char  i_operation[10]; // mkdir, mkfile, remove, rename, copy, move, chown...
    char  i_path[32];      // ruta afectada (se trunca a 31 chars)
    char  i_content[64];   // contenido o parámetros extra, vacío si no aplica
    float i_date;          // minutos desde 01/01/2000 UTC (un float no alcanza para time_t)

    Information() {
        memset(i_operation, 0, sizeof(i_operation));
        memset(i_path, 0, sizeof(i_path));
        memset(i_content, 0, sizeof(i_content));
        i_date = 0;
    }
};

struct Journal {
    int j_count; // correlativo de la entrada, -1 = libre
    Information j_content;

    Journal() { j_count = -1; }
};

#pragma pack(pop)
