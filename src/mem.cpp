#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include <boost/cstdint.hpp>

#include <atmi.h>
#include "typeinfo.hpp"

int tperrno;

typedef struct {
    size_t size;
    char type[TYPELEN];
    char subtype[SUBTYPELEN];
    boost::uint8_t typeidx;
    char data[];
} meminfo_t;

static auto carraytypeinfo = typeinfo("CARRAY", "*", 512);
static auto stringtypeinfo = typeinfo("STRING", "*", 512);
static std::vector<typeinfo *> g_typeinfo = {
    &carraytypeinfo,
    &stringtypeinfo
};

void register_typeinfo(typeinfo *ti) {
    g_typeinfo.push_back(ti);
}

char *tpalloc(FUXCONST char *type, FUXCONST char *subtype, long size) {
    if (type == nullptr || size < 0) {
        //tperrno = TPEINVAL;
        return nullptr;
    }

    auto found = false;

    boost::uint8_t typeidx = 0;
    for (auto ti : g_typeinfo) {
        if (strcmp(ti->type, type) == 0
                && (subtype == nullptr || strcmp(ti->subtype, subtype) == 0)) {
            found = true;

            if (size == 0) {
                size = ti->defsize;
            }
            break;
        }
        typeidx++;
    }

    if (!found) {
        //tperrno = TPNOENT;
        return nullptr;
    }

    auto mem = reinterpret_cast<meminfo_t *>(malloc(offsetof(meminfo_t, data) + size));
    mem->size = size;
    mem->typeidx = typeidx;
    std::copy_n(g_typeinfo[typeidx]->type, sizeof(mem->type), mem->type);
    std::copy_n(g_typeinfo[typeidx]->subtype, sizeof(mem->subtype), mem->subtype);

    g_typeinfo[typeidx]->initbuf(mem->data, mem->size);
    return mem->data;
}

char *tprealloc(char *ptr, long size)  {
    auto mem = reinterpret_cast<meminfo_t *>(ptr - offsetof(meminfo_t, data));
    mem = reinterpret_cast<meminfo_t *>(realloc(mem, size));
    mem->size = size;
    g_typeinfo[mem->typeidx]->reinitbuf(mem->data, mem->size);
    return mem->data;
}

void tpfree(char *ptr) {
    if (ptr != nullptr) {
        auto mem = reinterpret_cast<meminfo_t *>(ptr - offsetof(meminfo_t, data));
        g_typeinfo[mem->typeidx]->uninitbuf(mem->data, mem->size);
        free(mem);
    }
}

long tptypes(char *ptr, char *type, char *subtype) {
    auto mem = reinterpret_cast<meminfo_t *>(ptr - offsetof(meminfo_t, data));
    std::copy_n(mem->type, sizeof(mem->type), type);
    std::copy_n(mem->subtype, sizeof(mem->subtype), subtype);
}
