#ifndef ME2_TRACE_PIPE_H
#define ME2_TRACE_PIPE_H

#include <windows.h>

#define PIPE_NAME "\\\\.\\pipe\\me2-trace"
#define PIPE_BUF_SIZE 4096

int  pipe_init(void);
void pipe_shutdown(void);
void pipe_write(const char *msg);

#endif
