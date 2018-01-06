#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern char *proc_name;
int userlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
