#include <assert.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include "libnnctl.h"
#include "libut.h"
#include "tpl.h"

typedef struct {  // wraps a command structure 
  nnctl_cmd cmd;
  UT_hash_handle hh;
  void *data;
} nnctl_cmd_w;

struct _nnctl {
  nnctl_cmd_w *cmds; // hash table of commands
  void *data;        // opaque data pointer passed into commands
  // below: used during command execution. only one command executes
  // at a time; nnctl_exec is designed to be used from one thread
  nnctl_arg arg;
  UT_string out;
};

static int help_cmd(nnctl *cp, nnctl_arg *arg, void *data, uint64_t *cookie) {
  UT_string *t;
  utstring_new(t);
  nnctl_cmd_w *cw, *tmp;
  HASH_ITER(hh, cp->cmds, cw, tmp) {
    utstring_printf(t, "%-20s ", cw->cmd.name);
    utstring_printf(t, "%s\n",   cw->cmd.help);
  }
  utstring_printf(t, "%-20s %s\n", "quit", "close session");
  nnctl_append(cp,utstring_body(t),utstring_len(t));
  utstring_free(t);
  return 0; 
}

static int unknown_cmd(nnctl *cp, nnctl_arg *arg, void *data, uint64_t *cookie) {
  char unknown_msg[] = "command not found\n";
  if (arg->argc == 0) return 0;  /* no command; no-op */
  nnctl_append(cp, unknown_msg, sizeof(unknown_msg)-1);
  return -1;
}
// unknown is not registered in the commands hash table
// so we point to this record if we need to invoke it.
static nnctl_cmd_w unknown_cmdw = {{"unknown",unknown_cmd}};

nnctl *nnctl_init(nnctl_cmd *cmds, void *data) {
  nnctl_cmd *cmd;
  nnctl *cp;
  
  if ( (cp=calloc(1,sizeof(nnctl))) == NULL) goto done;
  cp->data = data;
  nnctl_add_cmd(cp, "help", help_cmd, "this text", NULL);
  for(cmd=cmds; cmd && cmd->name; cmd++) {
    nnctl_add_cmd(cp,cmd->name,cmd->cmdf,cmd->help,data);
  }
  utstring_init(&cp->out);

 done:
  return cp;
}

void nnctl_add_cmd(nnctl *cp, char *name, nnctl_cmdf *cmdf, char *help, void *data) {
  nnctl_cmd_w *cw;

  /* create new command if it isn't in the hash; else update in place */
  HASH_FIND(hh, cp->cmds, name, strlen(name), cw);
  if (cw == NULL) {
    if ( (cw = malloc(sizeof(*cw))) == NULL) exit(-1);
    memset(cw,0,sizeof(*cw));
    cw->cmd.name = strdup(name);
    cw->cmd.help = help ? strdup(help) : strdup("");
    HASH_ADD_KEYPTR(hh, cp->cmds, cw->cmd.name, strlen(cw->cmd.name), cw);
  }
  cw->cmd.cmdf = cmdf;
  cw->data = data;
}

#define MAX_ARGC 10
int nnctl_exec(nnctl *cp, int nn_rep_socket) {
  int rc=-1, argc, i=0;
  nnctl_cmd_w *cw=NULL;
  tpl_node *tn=NULL,*tr=NULL;
  tpl_bin b;
  void *msg=NULL, *o;
  size_t len, l;
  uint64_t cookie;

  /* get the message buffer from nano */
  len = nn_recv(nn_rep_socket, &msg, NN_MSG, 0);
  if (len < 0) {
     fprintf(stderr,"nn_recv: %s\n", nn_strerror(errno));
     goto done;
  }

  /* unpack it, enforce a bit of reasonable size */
  if ( (tn = tpl_map("UA(B)", &cookie, &b)) == NULL) goto done;
  if (tpl_load(tn, TPL_MEM, msg, len) < 0) goto done;
  tpl_unpack(tn, 0);
  if ( (argc = tpl_Alen(tn,1)) > MAX_ARGC) {
    fprintf(stderr,"excessive arg count: %d\n", argc);
    goto done;
  }

  /* set up the argv for the command callback */
  cp->arg.argc = argc; if (argc == 0) goto done;
  cp->arg.argv = calloc(argc,sizeof(char*));
  cp->arg.lenv = calloc(argc,sizeof(size_t));
  if (!cp->arg.argv || !cp->arg.lenv) {
    fprintf(stderr,"out of memory\n");
    goto done;
  }
  /* ensure each argument has null-terminator. this is not strictly
   * required, because there is a separate length field. We do not 
   * add to the length of the argument. This just makes it easier
   * to write command callbacks that manipulate arguments as strings. 
   * It preserves the ability for callbacks to take binary buffers. */
  while (tpl_unpack(tn,1) > 0) {
    if (b.addr && (b.sz > 0) && (((char*)b.addr)[b.sz-1] != '\0')) {
        char *tmp = malloc(b.sz + 1); if (!tmp) goto done;
        memcpy(tmp, b.addr, b.sz);
        tmp[b.sz] = '\0';
        free(b.addr);
        b.addr = tmp;
    }
    cp->arg.argv[i] = b.addr;
    cp->arg.lenv[i] = b.sz;
    i++;
  }

  /* find and invoke the command callback */
  HASH_FIND(hh, cp->cmds, cp->arg.argv[0], cp->arg.lenv[0], cw);
  if (!cw) cw = &unknown_cmdw;
  utstring_clear(&cp->out);
  cw->cmd.cmdf(cp, &cp->arg, cw->data, &cookie);

  /* reply to client */
  tr = tpl_map("UA(B)", &cookie, &b);
  tpl_pack(tr, 0);
  b.sz = utstring_len(&cp->out);
  b.addr = utstring_body(&cp->out);
  tpl_pack(tr,1);
  tpl_dump(tr, TPL_MEM, &o, &l);
  //fprintf(stderr,"sent %ld bytes to client\n%.*s\n", l, (int)l, o);
  rc = nn_send(nn_rep_socket, o, l, 0);
  free(o);
  if (rc < 0) goto done;
  rc = 0;

 done:
  while(cp->arg.argc) free(cp->arg.argv[--cp->arg.argc]);
  if (cp->arg.argv) { free(cp->arg.argv); cp->arg.argv=NULL; }
  if (cp->arg.lenv) { free(cp->arg.lenv); cp->arg.lenv=NULL; }
  if (msg) nn_freemsg(msg);
  if (tn) tpl_free(tn);
  if (tr) tpl_free(tr);
  return rc;
}

void nnctl_free(nnctl *cp) {
  nnctl_cmd_w *cw, *tmp;
  HASH_ITER(hh, cp->cmds, cw, tmp) {
    HASH_DEL(cp->cmds, cw);
    free(cw->cmd.name);
    free(cw->cmd.help);
    free(cw);
  }
  utstring_done(&cp->out);
  free(cp);
}

void nnctl_append(nnctl *cp, void *buf, size_t len) { 
  utstring_bincpy(&cp->out, buf, len);
}

static void nnctl_printf_va(nnctl *cp, const char *fmt, va_list _ap) {
   int n;
   va_list ap;
   UT_string *s;
   utstring_new(s);

   while (1) {
     va_copy(ap, _ap);
     n = vsnprintf (&s->d[s->i], s->n-s->i, fmt, ap);
     va_end(ap);

     if ((n > -1) && (n < (int)(s->n-s->i))) {
       s->i += n;
       goto done;
     }

     /* Else try again with more space. */
     if (n > -1) utstring_reserve(s,n+1); /* exact */
     else utstring_reserve(s,(s->n)*2);   /* 2x */
   }

  done:
   nnctl_append(cp, utstring_body(s), utstring_len(s));
   utstring_free(s);
}

void nnctl_printf(nnctl *cp, const char *fmt, ...) {
  va_list ap;
  va_start(ap,fmt);
  nnctl_printf_va(cp, fmt, ap);
  va_end(ap);
}

