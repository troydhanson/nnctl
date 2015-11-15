#ifndef LIBNNCTL_H
#define LIBNNCTL_H

#include <stdlib.h>
#include <nanomsg/nn.h>

#if defined __cplusplus
extern "C" {
#endif


typedef struct {
  int     argc; /* conventional argc/argv args */
  char  **argv;
  size_t *lenv; // len of each arg, useful if client sends null-containing args
} nnctl_arg;

struct _nnctl; /* defined internally in libnnctl.c */
typedef struct _nnctl nnctl;

typedef int (nnctl_cmdf)(nnctl *, nnctl_arg *arg, void *data);

typedef struct {
  char *name;
  nnctl_cmdf *cmdf;
  char *help;
} nnctl_cmd;

nnctl *nnctl_init(nnctl_cmd *cmds, void *data);
void nnctl_free(nnctl *cp);
void nnctl_add_cmd(nnctl *, char *name, nnctl_cmdf *cmdf, char *help, void *data);
int nnctl_exec(nnctl *cp, int nn_rep_socket);

/* these are used within command callbacks */
void nnctl_append(nnctl *, void *buf, size_t len);
void nnctl_printf(nnctl *, const char *fmt, ...);

#if defined __cplusplus
 }
#endif

#endif /* LIBNNCTL_H */
