#include <cstring>

static const int TYPELEN = 8;
static const int SUBTYPELEN = 16;

class typeinfo {
    public:
        char type[TYPELEN];
        char subtype[SUBTYPELEN];
        size_t defsize;

        typeinfo(const char *type_, const char *subtype_, size_t defsize_) {
            memset(type, 0, sizeof(type));
            memset(subtype, 0, sizeof(subtype));
            strncpy(type, type_, sizeof(type));
            strncpy(subtype, subtype_, sizeof(subtype));
            defsize = defsize_;
        }
        virtual ~typeinfo() {}
        virtual void initbuf(char *buf, long size) {}
        virtual void reinitbuf(char *buf, long size) {}
        virtual void uninitbuf(char *buf, long size) {}
};

void register_typeinfo(typeinfo *ti);
