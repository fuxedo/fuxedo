#pragma once
// This file is part of Fuxedo
// Copyright (C) 2017 Aivars Kalvans <aivars.kalvans@gmail.com>

#include <fml32.h>

// XML <-> FML32 conversion engine (src/xml.cpp). tpxmltofml32() and
// tpfml32toxml() are the public entry points; the boundary conversion
// for *SERVICES BUFTYPECONV=XML2FML32 goes through these two helpers so
// behavior stays identical.
namespace fux::xml {

// Parses xmlbuf and rebuilds fmlbuf as FML32 fields. Returns 0 or -1
// with Ferror32 set.
int xml_to_fml32(char *xmlbuf, FBFR32 *fmlbuf, FBFR32 **out);

// Serializes fmlbuf into xmlbuf, at most maxlen bytes including the
// terminator. Returns 0 or -1 with Ferror32 set.
int fml32_to_xml(FBFR32 *fmlbuf, char *xmlbuf, long maxlen);

}  // namespace fux::xml
