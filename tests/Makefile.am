check_PROGRAMS=test_mem test_fml32

test_mem_SOURCES=test_mem.cpp
test_mem_LDADD=../src/libfux.la @BOOST_UNIT_TEST_FRAMEWORK_LIB@
test_mem_CXXFLAGS=-I../include

test_fml32_SOURCES=test_fml32.cpp
test_fml32_LDADD=../src/libfux.la @BOOST_UNIT_TEST_FRAMEWORK_LIB@
test_fml32_CXXFLAGS=-I../include

TESTS=$(check_PROGRAMS)
