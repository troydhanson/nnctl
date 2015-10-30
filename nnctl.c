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

struct _CF {
  int run;
  int verbose;
  char *prompt;
  char *nn_addr; 
  int nn_socket;
  int nn_eid;
} CF = {
  .run = 1,
  .nn_addr = "tcp://127.0.0.1:3333",
  .nn_socket = -1,
  .prompt = "nn> ",
};

void usage(char *prog) {
  fprintf(stderr, "usage: %s [-v] <address>\n", prog);
  fprintf(stderr, "     -v verbose\n");  
  exit(-1);
}

/* This little parsing function finds one word at a time from the
 * input line. It supports double quotes to group words together. */
const int ws[256] = {[' ']=1, ['\t']=1};
char *find_word(char *c, char **start, char **end) {
  int in_qot=0;
  while ((*c != '\0') && ws[*c]) c++; // skip leading whitespace
  if (*c == '"') { in_qot=1; c++; }
  *start=c;
  if (in_qot) {
    while ((*c != '\0') && (*c != '"')) c++;
    *end = c;
    if (*c == '"') { 
      in_qot=0; c++; 
      if ((*c != '\0') && !ws[*c]) {
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

#define alloc_msg(n,m) ((m)=realloc((m),(++(n))*sizeof(*(m))))
#define MAX_IV 100
int do_rqst(char *line) {
  char *c=line, *start=NULL, *end=NULL, *buf;
  int rc = -1;

  struct nn_msghdr hdr;
  memset(&hdr, 0, sizeof(hdr));
  struct nn_iovec iv[MAX_IV], *w;
  hdr.msg_iov = iv;
  hdr.msg_iovlen = 0;

  /* parse the line into argv style words, pack and transmit the request */
  while(*c != '\0') {
    if ( (c = find_word(c,&start,&end)) == NULL) goto done; // TODO confirm: normal exit?
    //fprintf(stderr,"[%.*s]\n", (int)(end-start), start);
    assert(start && end);
    w = &iv[hdr.msg_iovlen++];
    if (hdr.msg_iovlen == MAX_IV) goto done;
    w->iov_base = start;
    w->iov_len = end-start;
    start = end = NULL;
  }

  // send request
  rc = nn_sendmsg(CF.nn_socket, &hdr, 0);
  if (rc < 0) {
    fprintf(stderr,"nn_sendmsg: %s\n", nn_strerror(errno));
    goto done;
  }

  // get reply 
  void *reply;
  memset(&hdr, 0, sizeof(hdr));
  iv[0].iov_base = &reply;
  iv[0].iov_len = NN_MSG;
  hdr.msg_iov = iv;
  hdr.msg_iovlen = 1;
  rc = nn_recvmsg(CF.nn_socket, &hdr, 0);
  if (rc < 0) {
    fprintf(stderr,"nn_recvmsg: %s\n", nn_strerror(errno));
    goto done;
  }

  printf("%.*s\n", rc, (char*)reply);
  nn_freemsg(reply);

  rc = 0;

 done:
  if (rc) CF.run=0;
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
  struct sockaddr_un addr;
  int opt,rc,quit;
  char *line;

  while ( (opt = getopt(argc, argv, "v+h")) != -1) {
    switch (opt) {
      case 'v': CF.verbose++; break;
      case 'h': default: usage(argv[0]); break;
    }
  }
  if (optind < argc) CF.nn_addr = strdup(optarg);
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

