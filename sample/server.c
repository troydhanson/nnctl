#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <nanomsg/nn.h>
#include <nanomsg/reqrep.h>
#include "libnnctl.h"

struct _CF {
  /* application state */
  int verbose;
  int request_exit;
  int signal_fd;
  int epoll_fd;
  int ticks;
  /* nanomsg related */
  int rep_socket;
  int rep_socket_fd;
  char *rep_addr;
  void *nnctl;
} CF = {
  .signal_fd = -1,
  .epoll_fd = -1,
  .rep_addr = "tcp://127.0.0.1:9995",
  .rep_socket = -1,
  .rep_socket_fd = -1,
};

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

/* place holder for periodic (every 10sec) worker */
void periodic_work() {
  if (CF.verbose) fprintf(stderr,"periodic work...\n");
}

int handle_signal() {
  int rc=-1;
  struct signalfd_siginfo info;
  
  if (read(CF.signal_fd, &info, sizeof(info)) != sizeof(info)) {
    fprintf(stderr,"failed to read signal fd buffer\n");
    goto done;
  }

  switch(info.ssi_signo) {
    case SIGALRM: 
      if ((++CF.ticks % 10) == 0) periodic_work();
      alarm(1); 
      break;
    default: 
      fprintf(stderr,"got signal %d\n", info.ssi_signo);  
      goto done;
      break;
  }

 rc = 0;

 done:
  return rc;
}

int new_epoll(int events, int fd) {
  int rc;
  struct epoll_event ev;
  memset(&ev,0,sizeof(ev)); // placate valgrind
  ev.events = events;
  ev.data.fd= fd;
  if (CF.verbose) fprintf(stderr,"adding fd %d to epoll\n", fd);
  rc = epoll_ctl(CF.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (rc == -1) {
    fprintf(stderr,"epoll_ctl: %s\n", strerror(errno));
  }
  return rc;
}

/* enumerate nano symbols */
int symbols_cmd(nnctl *cp, nnctl_arg *arg, void *data) {
  const char *c=NULL;
  int i=0,val;
  do {
    c = nn_symbol(i, &val);
    if (c) nnctl_printf(cp,"%d) %s: %d\n", i, c, val);
    i++;
  } while(c);
  return 0;
}

int shutdown_cmd(nnctl *cp, nnctl_arg *arg, void *data) {
  nnctl_printf(cp,"Shutting down\n");
  CF.request_exit=1;
  return 0;
}

nnctl_cmd cmds[] = { 
  {"symbols",      symbols_cmd,      "symbol info"},
  {"shutdown",     shutdown_cmd,     "shutdown server"},
  {NULL,           NULL,             NULL},
};

void usage(char *prog) {
  fprintf(stderr, "usage: %s [-v] <local-address>\n", prog);
  exit(-1);
}

int setup_nano(void) {
  int rc = -1, eid;

  rc = (CF.rep_socket = nn_socket(AF_SP, NN_REP));
  if (rc < 0) goto done;
  rc = (eid = nn_bind(CF.rep_socket, CF.rep_addr));
  if (rc < 0) goto done;

  rc = 0;

 done:
  return rc;
}
 
int msg_loop(void) {

  int rc=-1;
  size_t sz = sizeof(CF.rep_socket_fd);
  struct epoll_event ev;

  /* get underlying OS descriptor for our socket, add it to epoll */
  rc = nn_getsockopt(CF.rep_socket, NN_SOL_SOCKET, NN_RCVFD, &CF.rep_socket_fd, &sz);
  if (rc < 0) goto done;
  if (new_epoll(EPOLLIN, CF.rep_socket_fd)) goto done;
  if (new_epoll(EPOLLIN, CF.signal_fd)) goto done;

  alarm(1);
  while (epoll_wait(CF.epoll_fd, &ev, 1, -1) > 0) {
    if (CF.verbose > 1)  fprintf(stderr,"epoll reports fd %d\n", ev.data.fd);
    if (ev.data.fd == CF.rep_socket_fd) rc = nnctl_exec(CF.nnctl, CF.rep_socket);
    if (ev.data.fd == CF.signal_fd)     rc = handle_signal();
    if (rc < 0) goto done;
    if (CF.request_exit) break;
  }

  rc = 0;

 done:
  return rc;
}

int main(int argc, char *argv[]) {
  int opt, n, rc=-1;

  while ( (opt = getopt(argc, argv, "v+")) != -1) {
    switch (opt) {
      case 'v': CF.verbose++; break;
      default: usage(argv[0]); break;
    }
  }

  if (optind < argc) CF.rep_addr = strdup(argv[optind++]);

  /* block all signals. we take signals synchronously via signalfd */
  sigset_t all;
  sigfillset(&all);
  sigprocmask(SIG_SETMASK,&all,NULL);

  /* a few signals we'll accept via our signalfd */
  sigset_t sw;
  sigemptyset(&sw);
  for(n=0; n < sizeof(sigs)/sizeof(*sigs); n++) sigaddset(&sw, sigs[n]);

  /* create the signalfd for receiving signals */
  CF.signal_fd = signalfd(-1, &sw, 0);
  if (CF.signal_fd == -1) {
    fprintf(stderr,"signalfd: %s\n", strerror(errno));
    goto done;
  }

  /* set up the epoll instance */
  CF.epoll_fd = epoll_create(1); 
  if (CF.epoll_fd == -1) {
    fprintf(stderr,"epoll: %s\n", strerror(errno));
    goto done;
  }

  /* fire up nano socket. begin loop */
  if (setup_nano() == -1) goto done;
  CF.nnctl = nnctl_init(cmds,NULL);
  if (msg_loop() < 0) goto done;
  
  rc = 0;

 done:
  if (CF.rep_socket != -1) nn_close(CF.rep_socket);
  if (CF.epoll_fd != -1) close(CF.epoll_fd);
  if (CF.signal_fd != -1) close(CF.signal_fd);
  if (CF.nnctl) nnctl_free(CF.nnctl);
  return rc;
}

