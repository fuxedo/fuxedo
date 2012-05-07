#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE "ATMI memory allocation"
#include <boost/test/unit_test.hpp>
#include <vector>
#include <atmi.h>

BOOST_AUTO_TEST_SUITE(atmi_mem)

BOOST_AUTO_TEST_CASE(tc_alloc_carray)
{
    std::vector<char *> mem;
    for (auto i = 0; i < 100; i++) {
        auto m = tpalloc("CARRAY", nullptr, 512);
        BOOST_REQUIRE(m != nullptr);
        mem.push_back(m);
    }

    for (auto m : mem) {
        tpfree(m);
    }
}

BOOST_AUTO_TEST_SUITE_END()
