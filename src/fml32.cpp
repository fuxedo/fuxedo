#include <fstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <map>

#include <boost/cstdint.hpp>
#include <boost/thread/tss.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>

#include "fml32.h"
#include "typeinfo.hpp"

static std::map<FLDID32, std::string> g_idnm_map;
static std::map<std::string, FLDID32> g_nmid_map;

static const std::map<std::string, int> fml32_types = {
    { "short", FLD_SHORT },
    { "long", FLD_LONG },
    { "char", FLD_CHAR },
    { "float", FLD_FLOAT },
    { "double", FLD_DOUBLE },
    { "string", FLD_STRING },
    { "vardata", FLD_CARRAY },
    { "ptr", FLD_PTR },
    { "fml32", FLD_FML32 },
    { "view32", FLD_VIEW32 },
    { "mbstring", FLD_MBSTRING },
    { "fml", FLD_FML }
};

static bool read_fld32_file(const std::string &fname) {
    std::ifstream fields(fname);
    if (!fields) {
        return false;
    }

    std::string line;
    long base = 0;
    while (std::getline(fields, line)) {

        boost::tokenizer<boost::char_separator<char>> tk(line, boost::char_separator<char>(" \t\r\n"));
        auto nparts = std::distance(tk.begin(), tk.end());

        if (nparts < 2) {
            continue;
        }

        auto it = tk.begin();
        if ((*it)[0] == '#' || (*it)[0] == '$') {
            continue;
        } else if (*it == "*base") {
            ++it;
            std::string x = *it;
            base = boost::lexical_cast<long>(x);
        } else if (nparts > 2) {
            std::string name = *it;
            ++it;
            long fieldno = base + boost::lexical_cast<long>(*it);
            ++it;
            std::string type = *it;

            auto type_it = fml32_types.find(type);
            if (type_it == fml32_types.end()) {
            }

            auto fieldid = Fmkfldid32(type_it->second, fieldno);
            g_idnm_map.insert(make_pair(fieldid, name));
            g_nmid_map.insert(make_pair(name, fieldid));
        }
    }

    return true;
}

static void read_fieldtbls32() {
    auto fieldtbls32 = getenv("FIELDTBLS32");
    auto fldtbldir32 = getenv("FLDTBLDIR32");

    if (fieldtbls32 == nullptr || fldtbldir32 == nullptr) {
        return;
    }

    std::string sfieldtbls32(fieldtbls32), sfldtbldir32(fldtbldir32);
    boost::tokenizer<boost::char_separator<char>> files(sfieldtbls32, boost::char_separator<char>(","));

    boost::tokenizer<boost::char_separator<char>> dirs(sfldtbldir32, boost::char_separator<char>(":"));

    for (std::string fname : files) {
        for (auto dname : dirs) {
            if (read_fld32_file(dname + "/" + fname)) {
                break;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////

class fml32 {
    public:

        int init(FLDLEN32 buflen) {
            if (buflen < min_size()) {
                Ferror32 = FNOSPACE;
                return -1;
            }
            m_size = buflen - min_size();
            m_len = 0;
        }

        long size() {
            return m_size + min_size();
        }

        long used() {
            return m_len + min_size();
        }

        int chg(FLDID32 fieldid, FLDOCC32 oc, char *value, FLDLEN32 flen) {
            field_t *field = where(fieldid, oc);

            size_t value_size = value_len(fieldid, value, flen);
            ssize_t need = align(sizeof(FLDID32) + value_size);

            if (field == nullptr) {
                field = reinterpret_cast<field_t *>(m_data + m_len);
            } else if (field->fieldid == fieldid) {
                need -= align(sizeof(FLDID32) + field_len(field));
            }

            if (need != 0) {
                if (m_len + need > m_size) {
                    Ferror32 = FNOSPACE;
                    return -1;
                }

                char *ptr = reinterpret_cast<char *>(field);
                //std::copy(ptr, m_data + m_len, ptr + need);
                // copy does not work for overlaps?
                memmove(ptr + need, ptr, (m_data + m_len) - ptr);
            }

            field->fieldid = fieldid;
            std::copy_n(value, value_size, field->value.data);
            m_len += need;
            return 0;
        }

        char *find(FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *flen) {
            field_t *field = where(fieldid, oc);
            if (field == nullptr || field->fieldid != fieldid) {
                Ferror32 = FNOTPRES;
                return nullptr;
            }

            if (flen != nullptr) {
                *flen = field_len(field);
            }
            return field_value(field);
        }

        int pres(FLDID32 fieldid, FLDOCC32 oc) {
            field_t *field = where(fieldid, oc);
            if (field == nullptr || field->fieldid != fieldid) {
                return 0;
            }
            return 1;
        }

        FLDOCC32 occur(FLDID32 fieldid) {
            field_t *field = where(fieldid, 0);
            FLDOCC32 oc = 0;
            while (field != nullptr && field->fieldid == fieldid) {
                oc++;
                field = next_field(field);
            }
            return oc;
        }

        long len(FLDID32 fieldid, FLDOCC32 oc) {
            field_t *field = where(fieldid, oc);
            if (field == nullptr || field->fieldid != fieldid) {
                Ferror32 = FNOTPRES;
                return -1;
            }

            return field_len(field);
        }

        int fprint(FILE *iop) {
            field_t *field = first_field();
            while (field != nullptr) {
                auto it = g_idnm_map.find(field->fieldid);
                if (it != g_idnm_map.end()) {
                    printf("%s\t", it->second.c_str());
                } else {
                    printf("(FLDID32(%d))\t", field->fieldid);
                }

                auto type = Fldtype32(field->fieldid);
                if (type == FLD_SHORT) {
                    short data;
                    memcpy(&data, field->value.data, sizeof(data));
                    printf("%d", data);
                } else if (type == FLD_LONG) {
                    long data;
                    memcpy(&data, field->value.data, sizeof(data));
                    printf("%ld", data);
                } else if (type == FLD_CHAR) {
                    printf("%c", field->value.data[0]);
                } else if (type == FLD_FLOAT) {
                    float data;
                    memcpy(&data, field->value.data, sizeof(data));
                    printf("%f", data);
                } else if (type == FLD_DOUBLE) {
                    double data;
                    memcpy(&data, field->value.data, sizeof(data));
                    printf("%f", data);
                } else if (type == FLD_STRING) {
                    print_string(iop, field->value.data, strlen(field->value.data));
                } else if (type == FLD_CARRAY) {
                    print_string(iop, field->value.vardata.data, field->value.vardata.len);
                }

                printf("\n");

                field = next_field(field);
            }
            // FMALLOC
            printf("\n");
            return 0;
        }

        int print() {
            return fprint(stdout);
        }

        int del(FLDID32 fieldid, FLDOCC32 oc) {
            field_t *field = where(fieldid, oc);

            while (field == nullptr || field->fieldid != fieldid) {
                Ferror32 = FNOTPRES;
                return -1;
            }

            field_t *next = next_field(field);
            erase(reinterpret_cast<char *>(field), reinterpret_cast<char *>(next));
            return 0;
        }

        int delall(FLDID32 fieldid) {
            field_t *field = where(fieldid, 0);

            while (field == nullptr || field->fieldid != fieldid) {
                Ferror32 = FNOTPRES;
                return -1;
            }

            field_t *next = field;
            while (next != nullptr && next->fieldid == fieldid) {
                next = next_field(next);
            }
            erase(reinterpret_cast<char *>(field), reinterpret_cast<char *>(next));
            return 0;
        }

        int get(FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 *flen) {
            field_t *field = where(fieldid, 0);

            while (field == nullptr || field->fieldid != fieldid) {
                Ferror32 = FNOTPRES;
                return -1;
            }
            return 0;
        }

        long idxused() { return 0; }
        int index(FLDOCC32 intvl) { return 0; }
        int unindex() { return 0; }
        int rstrindex(FLDOCC32 numidx) { return 0; }
    private:

        void print_string(FILE *iop, const char *s, int len) {
            for (int i = 0; i < len; i++, s++) {
                if (isprint(*s)) {
                    fprintf(iop, "%c", *s);
                } else {
                    fprintf(iop, "\\%02x", *s);
                }
            }
        }

        void erase(char *from, char *to) {
            if (to == nullptr) {
                m_len = from - m_data;
            } else {
                memmove(from, to, (m_data + m_len) - to);
                m_len -= (to - from);
            }
        }

        boost::uint32_t m_size;
        boost::uint32_t m_len;
        char m_buf[32];
        char m_data[];

        size_t min_size() {
            return offsetof(fml32, m_data);
        }

        typedef struct {
            FLDID32 fieldid;
            union {
                struct {
                    boost::uint32_t len;
                    char data[];
                } vardata;
                char data[];
            } value;
        } field_t;

        char *field_value(field_t *field) {
            switch (Fldtype32(field->fieldid)) {
                case FLD_SHORT:
                case FLD_LONG:
                case FLD_CHAR:
                case FLD_FLOAT:
                case FLD_DOUBLE:
                case FLD_STRING:
                    return field->value.data;
                case FLD_CARRAY:
                    return field->value.vardata.data;
                default:
                    return 0;
            }

        }

        size_t value_len(FLDID32 fieldid, char *data, FLDLEN32 flen) {
            switch (Fldtype32(fieldid)) {
                case FLD_SHORT: return sizeof(short);
                case FLD_LONG:  return sizeof(long);
                case FLD_CHAR:  return sizeof(char);
                case FLD_FLOAT: return sizeof(float);
                case FLD_DOUBLE:  return sizeof(double);
                case FLD_STRING:  return strlen(data) + 1;
                case FLD_CARRAY:  return flen;
                default:
                    return 0;
            }
        }

        size_t field_len(field_t *field) {
            switch (Fldtype32(field->fieldid)) {
                case FLD_SHORT: return sizeof(short);
                case FLD_LONG:  return sizeof(long);
                case FLD_CHAR:  return sizeof(char);
                case FLD_FLOAT: return sizeof(float);
                case FLD_DOUBLE:  return sizeof(double);
                case FLD_STRING:  return strlen(field->value.data) + 1;
                case FLD_CARRAY:  return field->value.vardata.len;
                default:
                    return 0;
            }

        }

        field_t *where(FLDID32 fieldid, FLDOCC32 oc) {
            field_t *field = first_field();
            FLDOCC32 count = 0;
            while (field != nullptr) {
                if (field->fieldid == fieldid) {
                    if (count == oc) {
                        return field;
                    }
                    count++;
                } else if (field->fieldid > fieldid) {
                    return field;
                }

                field = next_field(field);
            }
            return nullptr;
        }

        field_t *first_field() {
            if (m_len == 0) {
                return nullptr;
            }
            return reinterpret_cast<field_t *>(m_data);
        }

        field_t *next_field(field_t *field) {
            auto ptr = reinterpret_cast<char *>(field);
            ptr += align(sizeof(FLDID32) + field_len(field));
            if (ptr < m_data + m_len) {
                return reinterpret_cast<field_t *>(ptr);
            }
            return nullptr;
        }

        size_t align(size_t flen) {
            if (flen & 3) {
                flen += 4 - (flen & 3);
            }
            return flen;
        }
};

////////////////////////////////////////////////////////////////////////////

class fml32typeinfo : public typeinfo {
public:
    fml32typeinfo() : typeinfo("FML32", "*", 512) {
        register_typeinfo(this);
        read_fieldtbls32();
    }
    virtual void initbuf(char *buf, long size) {
        reinterpret_cast<fml32 *>(buf)->init(size);
    }
    virtual void reinitbuf(char *buf, long size) {
        reinterpret_cast<fml32 *>(buf)->init(size);
    }
    virtual void uninitbuf(char *buf, long size) {
    }
private:
    static fml32typeinfo once;
};
fml32typeinfo fml32typeinfo::once;

////////////////////////////////////////////////////////////////////////////

int *_thread_specific_Ferror32() {
    static boost::thread_specific_ptr<int> _Ferror32;
    if (_Ferror32.get() == nullptr) {
        _Ferror32.reset(new int);
    }
    return &(*_Ferror32);
}

char *Fstrerror32(int err) {
    return nullptr;
}

FLDID32 Fmkfldid32(int type, FLDID32 num) {
    if (type != FLD_SHORT && type != FLD_LONG && type != FLD_CHAR
            && type != FLD_FLOAT && type != FLD_DOUBLE && type != FLD_STRING
            && type != FLD_CARRAY
            && type != FLD_PTR && type != FLD_FML32 && type != FLD_VIEW32
            && type != FLD_MBSTRING && type != FLD_FML) {
        Ferror32 = FTYPERR;
        return -1;
    }
    if (num < 1) {
        Ferror32 = FBADFLD;
        return -1;
    }

    // Tuxedo puts type in most significant byte
    // We do it the other way so data is sorted by "num" not "type"
    return (num << 8) | type;
}

int Fldtype32(FLDID32 fieldid) {
    return fieldid & 0xff;
}

long Fldno32(FLDID32 fieldid) {
    return fieldid >> 8;
}

char *Fname32(FLDID32 fieldid) {
    auto it = g_idnm_map.find(fieldid);
    if (it == g_idnm_map.end()) {
        Ferror32 = FBADFLD;
        return nullptr;
    }
    return const_cast<char *>(it->second.c_str());
}

FLDID32 Fldid32(char *name) {
    auto it = g_nmid_map.find(name);
    if (it != g_nmid_map.end()) {
        Ferror32 = FBADNAME;
        return BADFLDID;
    }
    return it->second;
}

void Fidnm_unload32() {
    // nop, memory is cheap
}

void Fnmid_unload() {
    // nop, memory is cheap
}

FBFR32 *Falloc32(FLDOCC32 F, FLDLEN32 V) {
    return reinterpret_cast<FBFR32 *>(new fml32());
}

//#define GOOD_ALIGNMENT 16
#define GOOD_ALIGNMENT 2
#define FBFR32_CHECK_INT(fbfr) \
    do { \
        if (fbfr == nullptr) { \
            Ferror32 = FNOTFLD; \
            return -1; \
        } \
        if ((reinterpret_cast<intptr_t>(fbfr) & GOOD_ALIGNMENT) != 0) { \
            Ferror32 = FALIGNERR; \
            return -1; \
        } \
    } while (false);

#define FBFR32_CHECK_PTR(fbfr) \
    do { \
        if (fbfr == nullptr) { \
            Ferror32 = FNOTFLD; \
            return NULL; \
        } \
        if ((reinterpret_cast<intptr_t>(fbfr) & GOOD_ALIGNMENT) != 0) { \
            Ferror32 = FALIGNERR; \
            return NULL; \
        } \
    } while (false);

int Finit32(FBFR32 *fbfr, FLDLEN32 buflen) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->init(buflen);
}

int Ffree32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    delete reinterpret_cast<fml32 *>(fbfr);
    return 0;
}

long Fsizeof32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->size();
}

long Fused32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->used();
}

long Fidxused32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->idxused();
}

int Findex32(FBFR32 *fbfr, FLDOCC32 intvl) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->index(intvl);
}

int Funindex32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->unindex();
}

int Frstrindex32(FBFR32 *fbfr, FLDOCC32 numidx) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->rstrindex(numidx);
}

int Fchg32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *value, FLDLEN32 len) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->chg(fieldid, oc, value, len);
}

char *Ffind32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, FLDLEN32 *len) {
    FBFR32_CHECK_PTR(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->find(fieldid, oc, len);
}

int Fpres32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->pres(fieldid, oc);
}

FLDOCC32 Foccur32(FBFR32 *fbfr, FLDID32 fieldid) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->occur(fieldid);
}

long Flen32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->len(fieldid, oc);
}

int Fprint32(FBFR32 *fbfr) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->print();
}

int Ffprint32(FBFR32 *fbfr, FILE *iop) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->fprint(iop);
}

int Fdel32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->del(fieldid, oc);
}

int Fdelall32(FBFR32 *fbfr, FLDID32 fieldid) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->delall(fieldid);
}

int Fget32(FBFR32 *fbfr, FLDID32 fieldid, FLDOCC32 oc, char *loc, FLDLEN32 *len) {
    FBFR32_CHECK_INT(fbfr);
    return reinterpret_cast<fml32 *>(fbfr)->get(fieldid, oc, loc, len);
}
