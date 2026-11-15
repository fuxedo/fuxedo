#pragma once

#ifdef __cplusplus
extern "C" {
#endif

extern char *proc_name;
int userlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define USERLOG_H 
#define WRITE_EVENT_LOG_ONLY 0x0
#define WRITE_INIT_VAL 0x999
#define WRITE_ULOG_ONLY 0x1

#ifdef __cplusplus
}
#endif
