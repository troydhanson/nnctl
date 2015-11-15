#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>
#include "tpl.h"

/* 
 * nnctl
 *
 * usage:  nnctl <nnctl-remote-address>
 * 
 */

struct _CF {
  int run;
  int verbose;
  char *prompt;
  char *nn_addr; 
  int nn_socket;
  int nn_eid;
} CF = {
  .run = 1,
  .nn_addr = "tcp://127.0.0.1:9995",
  .nn_socket = -1,
  .prompt = "nnctl> ",
};

void usage(char *prog) {
  fprintf(stderr, "usage: %s [-v] <address>\n", prog);
  fprintf(stderr, "options:\n");  
  fprintf(stderr, "\t-v verbose\n");  
  exit(-1);
}

/* This little parsing function finds one word at a time from the
 * input line. It supports double quotes to group words together. */
const int ws[256] = {[' ']=1, ['\t']=1};
char *find_word(char *c, char **start, char **end) {
  int in_qot=0;
  while ((*c != '\0') && ws[(int)(*c)]) c++; // skip leading whitespace
  if (*c == '"') { in_qot=1; c++; }
  *start=c;
  if (in_qot) {
    while ((*c != '\0') && (*c != '"')) c++;
    *end = c;
    if (*c == '"') { 
      in_qot=0; c++; 
      if ((*c != '\0') && !ws[(int)(*c)]) {
        fprintf(stderr,"text follows quoted text without space\n"); return NULL;
      }
    }
    else {fprintf(stderr,"quote mismatch\n"); return NULL;}
  }
  else {
    while ((*c != '\0') && (*c != ' ')) {
      if (*c == '"') {fprintf(stderr,"start-quote within word\n"); return NULL; }
      c++;
    }
    *end = c;
  }
  return c;
}

int do_rqst(char *line) {
  char *c=line, *start=NULL, *end=NULL;
  char *buf=NULL;
  size_t len;
  int rc = -1;
  tpl_node *tn=NULL;
  tpl_bin b;

  tn = tpl_map("A(B)", &b);
  if (tn == NULL) goto done;

  /* parse the line into argv style words, pack and transmit the request */
  while(*c != '\0') {
    if ( (c = find_word(c,&start,&end)) == NULL) goto done;
    //fprintf(stderr,"[%.*s]\n", (int)(end-start), start);
    assert(start && end);
    b.addr = start;
    b.sz = end-start;
    tpl_pack(tn,1);
    start = end = NULL;
  }

  // send request
  if (tpl_dump(tn, TPL_MEM, &buf, &len) < 0) goto done;
  rc = nn_send(CF.nn_socket, buf, len, 0);
  if (rc < 0) {
    fprintf(stderr,"nn_send: %s\n", nn_strerror(errno));
    goto done;
  }

  // get reply 
  void *reply;
  rc = nn_recv(CF.nn_socket, &reply, NN_MSG, 0);
  if (rc < 0) {
    fprintf(stderr,"nn_recvmsg: %s\n", nn_strerror(errno));
    goto done;
  }

  printf("%.*s\n", rc, (char*)reply);
  nn_freemsg(reply);

  rc = 0;

 done:
  if (rc) CF.run=0;
  if (tn) tpl_free(tn);
  if (buf) free(buf);
  return rc;
}
 
int setup_nn() {
  int rc=-1;
  CF.nn_socket = nn_socket(AF_SP, NN_REQ);
  if (CF.nn_socket < 0) {
    fprintf(stderr,"nn_socket: %s\n",nn_strerror(errno));
    goto done;
  }

  fprintf(stderr,"Connecting to %s.\n", CF.nn_addr);
  CF.nn_eid = nn_connect(CF.nn_socket, CF.nn_addr);
  if (CF.nn_eid < 0) {
    fprintf(stderr,"nn_connect: %s\n", nn_strerror(errno));
    goto done;
  }

  rc = 0; // success

 done:
  return rc;
}

int main(int argc, char *argv[]) {
  int opt,quit;
  char *line;

  while ( (opt = getopt(argc, argv, "v+h")) != -1) {
    switch (opt) {
      case 'v': CF.verbose++; break;
      case 'h': default: usage(argv[0]); break;
    }
  }
  if (optind < argc) CF.nn_addr = strdup(argv[optind++]);
  if (setup_nn()) goto done;
  using_history();

  while(CF.run) {
    line=readline(CF.prompt);
    quit = (!line) || (!strcmp(line,"exit")) || (!strcmp(line,"quit"));
    if (quit) CF.run=0;
    else if (*line != '\0') {
      add_history(line);
      do_rqst(line);
    }
    if (line) free(line); 
  }

 done:
  clear_history();
  if (CF.nn_socket != -1) nn_close(CF.nn_socket);
  return 0;
}

