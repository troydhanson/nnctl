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
int do_rqst(char *line) {
#if 0
  char *c=line, *start=NULL, *end=NULL, *buf;
  int nmsgs=0; zmq_msg_t *msgs = NULL;
  int rmsgs=0; zmq_msg_t *msgr = NULL;
  size_t sz;
  zmq_rcvmore_t more; size_t more_sz = sizeof(more);
  int i, rc = -1;

  /* parse the line into argv style words, pack and transmit the request */
  while(*c != '\0') {
    if ( (c = find_word(c,&start,&end)) == NULL) goto done; // TODO confirm: normal exit?
    //fprintf(stderr,"[%.*s]\n", (int)(end-start), start);
    assert(start && end);
    alloc_msg(nmsgs,msgs);
    buf = start; sz = end-start;
    zmq_msg_init_size(&msgs[nmsgs-1],sz);
    memcpy(zmq_msg_data(&msgs[nmsgs-1]),buf,sz);
    start = end = NULL;
  }

  // send request
  for(i=0; i<nmsgs; i++) {
    if (zmq_sendmsg(CF.req_socket, &msgs[i], (i<nmsgs-1)?ZMQ_SNDMORE:0) == -1) {
      fprintf(stderr,"zmq_sendmsg: %s\n", zmq_strerror(errno));
      goto done;
    }
  }

  // get reply 
  do {
    alloc_msg(rmsgs,msgr);
    zmq_msg_init(&rmsgs[msgr-1]);
    if (zmq_recvmsg(CF.req_socket, &rmsgs[msgr-1], 0) == -1) {
      fprintf(stderr,"zmq_recvmsg: %s\n", zmq_strerror(errno));
      goto done;
    }
    buf = zmq_msg_data(&rmsgs[msgr-1]); 
    sz = zmq_msg_size(&rmsgs[msgr-1]); 
    printf("%.*s", (int)sz, (char*)buf);
    if (zmq_getsockopt(CF.req_socket, ZMQ_RCVMORE, &more, &more_sz)) more=0;
  } while (more);
  
  printf("\n");
  rc = 0;

 done:
  for(i=0; i<nmsgs; i++) zmq_msg_close(&msgs[i]);
  for(i=0; i<rmsgs; i++) zmq_msg_close(&msgr[i]);
  if (rc) CF.run=0;
  return rc;
#endif
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

