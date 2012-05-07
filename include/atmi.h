#ifndef __atmi_h
#define __atmi_h

#define FUXCONST const

#ifdef __cplusplus
extern "C" {
#endif

char *tpalloc(FUXCONST char *type, FUXCONST char *subtype, long size);
char *tprealloc(char *ptr, long size);
void tpfree(char *ptr);
long tptypes(char *ptr, char *type, char *subtype);

#ifdef __cplusplus
};
#endif

#endif
