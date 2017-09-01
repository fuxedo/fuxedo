#pragma once

#define FUXCONST const

#define TPEBADDESC 2
#define TPEBLOCK 3
#define TPEINVAL 4
#define TPELIMIT 5
#define TPENOENT 6
#define TPEOS 7
#define TPEPROTO 9
#define TPESVCERR 10
#define TPESVCFAIL 11
#define TPESYSTEM 12
#define TPETIME 13
#define TPETRAN 14
#define TPGOTSIG 15
#define TPEITYPE 17
#define TPEOTYPE 18
#define TPEEVENT 22
#define TPEMATCH 23

#ifdef __cplusplus
extern "C" {
#endif

int *_tperrno_tls();
#define tperrno (*_tperrno_tls())

char *tpalloc(FUXCONST char *type, FUXCONST char *subtype, long size);
char *tprealloc(char *ptr, long size);
void tpfree(char *ptr);
long tptypes(char *ptr, char *type, char *subtype);

#ifdef __cplusplus
}
#endif
