// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

// tpxmltofml32()/tpfml32toxml() semantics, pinned against Oracle
// Tuxedo 12.2 (probe session in private/compat/xml). Field names come
// from tests/fields (FLDTBLDIR32/FIELDTBLS32 set by the test harness).

#include <catch.hpp>

#include <cstring>
#include <string>

#include <atmi.h>
#include <fml32.h>

namespace fux::xml {
int xml_to_fml32(char *xmlbuf, FBFR32 *fmlbuf, FBFR32 **out);
int fml32_to_xml(FBFR32 *fmlbuf, char *xmlbuf, long maxlen);
}  // namespace fux::xml

namespace {

struct fml {
  fml() { p = Falloc32(16, 2048); }
  ~fml() {
    if (p != nullptr) {
      Ffree32(p);
    }
  }
  FBFR32 *p;
};

std::string to_xml(FBFR32 *f) {
  std::string out(8192, '\0');
  REQUIRE(fux::xml::fml32_to_xml(f, &out[0], static_cast<long>(out.size())) ==
          0);
  out.resize(strlen(out.c_str()));
  return out;
}

}  // namespace

TEST_CASE("tpxmltofml32 maps elements to typed fields", "[xmlfml]") {
  fml f;
  char doc[] = "<q><NAME>fuxedo</NAME><AGE>42</AGE><SALARY>12.5</SALARY>"
               "<DEPT>7</DEPT></q>";
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == 0);
  FLDLEN32 len = 0;
  auto name = Fgetalloc32(f.p, Fldid32(const_cast<char *>("NAME")), 0, &len);
  REQUIRE(name != nullptr);
  REQUIRE(std::string(name) == "fuxedo");
  free(name);
  long dept = 0;
  REQUIRE(Fget32(f.p, Fldid32(const_cast<char *>("DEPT")), 0,
                 reinterpret_cast<char *>(&dept), nullptr) != -1);
  REQUIRE(dept == 7);
  float salary = 0;
  REQUIRE(Fget32(f.p, Fldid32(const_cast<char *>("SALARY")), 0,
                 reinterpret_cast<char *>(&salary), nullptr) != -1);
  REQUIRE(salary == Approx(12.5f));
}

TEST_CASE("tpxmltofml32 repeated elements become occurrences", "[xmlfml]") {
  fml f;
  char doc[] = "<q><NAME>one</NAME><NAME>two</NAME></q>";
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == 0);
  REQUIRE(Foccur32(f.p, Fldid32(const_cast<char *>("NAME"))) == 2);
}

TEST_CASE("tpxmltofml32 nests elements under fml32 fields", "[xmlfml]") {
  fml f;
  char doc[] = "<q><ARGS><NAME>inner</NAME></ARGS></q>";
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == 0);
  FLDLEN32 len = 0;
  auto sub = reinterpret_cast<FBFR32 *>(Ffind32(
      f.p, Fldid32(const_cast<char *>("ARGS")), 0, &len));
  REQUIRE(sub != nullptr);
  auto inner = Ffind32(sub, Fldid32(const_cast<char *>("NAME")), 0, &len);
  REQUIRE(inner != nullptr);
  REQUIRE(std::string(inner) == "inner");
}

TEST_CASE("tpxmltofml32 skips unknown elements with FBADNAME", "[xmlfml]") {
  fml f;
  char doc[] = "<q><NOSUCHELEMENT>x</NOSUCHELEMENT><NAME>ok</NAME></q>";
  Ferror32 = 0;
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == 0);
  REQUIRE(Ferror32 == FBADNAME);
  auto name = Ffind32(f.p, Fldid32(const_cast<char *>("NAME")), 0, nullptr);
  REQUIRE(name != nullptr);
  REQUIRE(Ffind32(f.p, Fldid32(const_cast<char *>("NOSUCHELEMENT")), 0,
                  nullptr) == nullptr);
}

TEST_CASE("tpxmltofml32 ignores the root element and attributes",
          "[xmlfml]") {
  fml f;
  char doc[] = "<ARGS id=\"7\"><NAME>ok</NAME></ARGS>";
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == 0);
  // the document element is not added as a field, attributes are dropped
  REQUIRE(Ffind32(f.p, Fldid32(const_cast<char *>("ARGS")), 0, nullptr) ==
          nullptr);
  REQUIRE(Ffind32(f.p, Fldid32(const_cast<char *>("NAME")), 0, nullptr) !=
          nullptr);
}

TEST_CASE("tpxmltofml32 rejects malformed input", "[xmlfml]") {
  fml f;
  char doc[] = "<q><NAME>broken</q>";
  REQUIRE(fux::xml::xml_to_fml32(doc, f.p, nullptr) == -1);
  REQUIRE(Ferror32 == FSYNTAX);
}

TEST_CASE("tpfml32toxml emits the platform shape", "[xmlfml]") {
  auto g = Falloc32(16, 2048);
  REQUIRE(g != nullptr);
  const char *name = "fux&edo";
  REQUIRE(Fadd32(g, Fldid32(const_cast<char *>("NAME")),
                 const_cast<char *>(name), 0) != -1);
  long dept = 42;
  REQUIRE(Fadd32(g, Fldid32(const_cast<char *>("DEPT")),
                 reinterpret_cast<char *>(&dept), 0) != -1);
  auto out = to_xml(g);
  // fields come out in FML32 field-id order, like Fnext32
  REQUIRE(out ==
          "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>"
          "<FML32 Type=\"FML32\"><DEPT>42</DEPT><NAME>fux&amp;edo</NAME>"
          "</FML32>");
  Ffree32(g);
}

TEST_CASE("tpfml32toxml nests fml32 fields", "[xmlfml]") {
  auto g = Falloc32(16, 2048);
  auto sub = Falloc32(16, 1024);
  const char *name = "inner";
  Fadd32(sub, Fldid32(const_cast<char *>("NAME")), const_cast<char *>(name),
         0);
  Fadd32(g, Fldid32(const_cast<char *>("ARGS")),
         reinterpret_cast<char *>(sub), 0);
  Ffree32(sub);
  auto out = to_xml(g);
  REQUIRE(out.find("<ARGS Type=\"FML32\"><NAME>inner</NAME></ARGS>") !=
          std::string::npos);
  Ffree32(g);
}

TEST_CASE("XML is a typed buffer", "[xmlfml]") {
  char *x = tpalloc(const_cast<char *>("XML"), nullptr, 512);
  REQUIRE(x != nullptr);
  char type[8];
  REQUIRE(tptypes(x, type, nullptr) != -1);
  REQUIRE(std::string(type) == "XML");
  strcpy(x, "<q><NAME>x</NAME></q>");
  char *fml = tpalloc(const_cast<char *>("FML32"), nullptr, 4096);
  REQUIRE(fml != nullptr);
  FBFR32 *out = reinterpret_cast<FBFR32 *>(fml);
  REQUIRE(tpxmltofml32(x, nullptr, &out, nullptr, 0) == 0);
  REQUIRE(out != nullptr);
  auto name = Ffind32(out, Fldid32(const_cast<char *>("NAME")), 0, nullptr);
  REQUIRE(name != nullptr);
  tpfree(fml);
  tpfree(x);
}
