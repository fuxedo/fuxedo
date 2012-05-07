#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE "FML32"
#include <boost/test/unit_test.hpp>

#include <vector>
#include <atmi.h>
#include <fml32.h>

BOOST_AUTO_TEST_SUITE(fml32)

BOOST_AUTO_TEST_CASE(tc_FLDID32) {
    int types[] = {
            FLD_SHORT,
            FLD_LONG,
            FLD_CHAR,
            FLD_FLOAT,
            FLD_DOUBLE,
            FLD_STRING,
            FLD_CARRAY,
            FLD_PTR,
            FLD_FML32,
            FLD_VIEW32,
            FLD_MBSTRING,
            FLD_FML
    };

    long num = 13;
    for (auto type : types) {
        FLDID32 id = Fmkfldid32(type, num);
        BOOST_REQUIRE(id != BADFLDID);
        BOOST_REQUIRE(Fldtype32(id) == type);
        BOOST_REQUIRE(Fldno32(id) == num);
    }
}

void dump(FBFR32 *f) {
    auto len = Fused32(f);
    auto *ptr = reinterpret_cast<unsigned char *>(f);
    for (auto i = 0; i < len; i++) {
        printf("%02x", ptr[i]);
    }
    printf("\n");
}

BOOST_AUTO_TEST_CASE(tc_Fchg32) {
    auto m = tpalloc("FML32", nullptr, 1024);
    BOOST_REQUIRE(m != nullptr);
    auto fbfr = reinterpret_cast<FBFR32 *>(m);

    FLDID32 fld_long = Fmkfldid32(FLD_LONG, 10);
    FLDID32 fld_string = Fmkfldid32(FLD_STRING, 11);

    for (long i = 0; i < 10; i++) {
        BOOST_REQUIRE(Fchg32(fbfr, fld_long, i, reinterpret_cast<char *>(&i), 0) != -1);
        BOOST_REQUIRE(Fchg32(fbfr, fld_string, i, "foo\t\nbar", 0) != -1);
    }

    for (long i = 0; i < 10; i++) {
        auto ptr =Ffind32(fbfr, fld_long, i, nullptr);
        BOOST_REQUIRE(ptr != nullptr);
        BOOST_REQUIRE(*reinterpret_cast<long *>(ptr) == i);
    }
    Fprint32(fbfr);
}

BOOST_AUTO_TEST_CASE(tc_Ferror32) {
    for (auto i = 0; i < 10; i++) {
        Ferror32 = i;
        BOOST_REQUIRE(Ferror32 == i);
    }
}

BOOST_AUTO_TEST_CASE(tc_fml32_alloc) {
    auto m = tpalloc("FML32", nullptr, 0);
    BOOST_REQUIRE(m != nullptr);
    auto fbfr = reinterpret_cast<FBFR32 *>(m);
    Finit32(fbfr, Fsizeof32(fbfr));
    tpfree(m);
}

BOOST_AUTO_TEST_SUITE_END()
