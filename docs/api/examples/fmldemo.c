// FML32 API examples: the API pages highlight the lines of each
// documented function. The field identifiers come from the "demo"
// field table via mkfldhdr32 (see the Makefile). Run inside the
// sample domain: FLDTBLDIR32=. FIELDTBLS32=demo

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atmi.h>
#include <fml32.h>
#include "demo.h"

static int fmlfail(const char *what) {
  // doc:region Ferror32
  // Ferror32 holds the code of the last failed FML32 call; it is not
  // cleared by success, so read it only after a -1/NULL return
  int code = Ferror32;
  // doc:end
  // doc:region Fstrerror32
  // Fstrerror32 renders an FML32 error code as text
  fprintf(stderr, "%s failed: %s (%d)\n", what, Fstrerror32(code), code);
  // doc:end
  return 1;
}

int main(void) {
  FBFR32 *fb = (FBFR32 *)tpalloc((char *)"FML32", NULL, 1024);
  if (fb == NULL) {
    fprintf(stderr, "tpalloc failed: tperrno=%d\n", tperrno);
    return 1;
  }

  // doc:region Fadd32
  // Fadd32 appends a new occurrence; len 0 lets a string field measure
  // itself
  if (Fadd32(fb, FIRSTNAME, (char *)"Ada", 0) == -1) {
    return fmlfail("Fadd32 FIRSTNAME");
  }
  if (Fadd32(fb, LASTNAME, (char *)"Lovelace", 0) == -1) {
    return fmlfail("Fadd32 LASTNAME");
  }
  long amount = 100;
  if (Fadd32(fb, AMOUNT, (char *)&amount, 0) == -1) {
    return fmlfail("Fadd32 AMOUNT");
  }
  // the same append can be written with Fchg32 and oc -1
  if (Fadd32(fb, FIRSTNAME, (char *)"Grace", 0) == -1) {
    return fmlfail("Fadd32 FIRSTNAME #2");
  }
  // doc:end

  // doc:region Fchg32
  // Fchg32 replaces the value at one occurrence...
  long big = 500;
  if (Fchg32(fb, AMOUNT, 0, (char *)&big, 0) == -1) {
    return fmlfail("Fchg32 AMOUNT");
  }
  // ...appends a new occurrence with oc -1 (like Fadd32)...
  if (Fchg32(fb, AMOUNT, -1, (char *)&big, 0) == -1) {
    return fmlfail("Fchg32 AMOUNT append");
  }
  // ...pads skipped occurrences with default values, and deletes the
  // occurrence when value is NULL
  if (Fchg32(fb, AMOUNT, 0, NULL, 0) == -1) {
    return fmlfail("Fchg32 AMOUNT delete");
  }
  // doc:end

  // doc:region Fget32
  long got = 0;
  if (Fget32(fb, AMOUNT, 0, (char *)&got, 0) == -1) {
    return fmlfail("Fget32 AMOUNT");
  }
  // doc:end
  printf("AMOUNT[0]=%ld\n", got);

  // doc:region Fnext32
  // Fnext32 walks the buffer in field order; FIRSTFLDID starts the walk
  FLDID32 id = FIRSTFLDID;
  FLDOCC32 oc = 0;
  char value[128];
  FLDLEN32 vlen = sizeof(value);
  printf("fields:\n");
  while (true) {
    vlen = sizeof(value);  // len is in/out: Fnext32 reports the copy
    if (Fnext32(fb, &id, &oc, value, &vlen) != 1) {
      break;  // 0 = no more fields, -1 = error (Ferror32)
    }
    printf("  field=%u oc=%d\n", id, oc);
  }
  // doc:end

  // doc:region Fboolev32
  // the expression is compiled once and evaluated against the buffer
  // doc:region Fboolco32
  // Fboolco32 compiles the expression into a tree; field names are
  // resolved through the loaded field tables
  char *tree = Fboolco32((char *)"AMOUNT > 200");
  // doc:end
  if (tree == NULL) {
    return fmlfail("Fboolco32");
  }
  int match = Fboolev32(fb, tree);
  if (match == -1) {
    return fmlfail("Fboolev32");
  }
  printf("AMOUNT > 200: %s\n", match ? "true" : "false");

  // doc:region Fboolpr32
  // Fboolpr32 prints the parsed expression tree
  Fboolpr32(tree, stdout);
  // doc:end

  free(tree);
  // doc:end

  // doc:region Flen32
  // Flen32 is the stored length of one occurrence
  long len0 = Flen32(fb, FIRSTNAME, 0);
  if (len0 == -1) {
    return fmlfail("Flen32");
  }
  // doc:end
  printf("FIRSTNAME[0] len=%ld\n", len0);

  // doc:region Fsizeof32
  // Fsizeof32 is the whole buffer size; Fneeded32 sizes an addition
  long used = Fsizeof32(fb);
  long need = Fneeded32(3, 32);
  // doc:end
  printf("sizeof=%ld needed=%ld\n", used, need);

  // doc:region Fwrite32
  // Fwrite32/Fread32 move a buffer to and from a FILE
  FILE *out = fopen("demo.fml", "w");
  if (out == NULL || Fwrite32(fb, out) == -1) {
    return fmlfail("Fwrite32");
  }
  fclose(out);
  // doc:end

  // doc:region Fread32
  FBFR32 *copy = (FBFR32 *)tpalloc((char *)"FML32", NULL, 1024);
  FILE *in = fopen("demo.fml", "r");
  if (copy == NULL || in == NULL || Fread32(copy, in) == -1) {
    return fmlfail("Fread32");
  }
  fclose(in);
  // doc:end

  // doc:region Fprint32
  // Fprint32 prints the buffer to stdout; Ffprint32 takes a FILE
  if (Fprint32(copy) == -1) {
    return fmlfail("Fprint32");
  }
  // doc:end

  // doc:region Ffprint32
  // Ffprint32 prints the same form to any FILE destination
  if (Ffprint32(copy, stdout) == -1) {
    return fmlfail("Ffprint32");
  }
  // doc:end

  // doc:region Fdel32
  // Fdel32 removes one occurrence; oc -1 removes them all
  if (Fdel32(fb, FIRSTNAME, 1) == -1) {
    return fmlfail("Fdel32");
  }
  // doc:end

  tpfree((char *)fb);
  tpfree((char *)copy);
  return 0;
}
