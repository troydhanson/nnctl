nnctl

Troy D. Hanson (tdh@tkhanson.net)

nnctl: a control port for nanomsg applications

The nnctl library is a small API for embedding an administrative control port
into a nanomsg-based C program. The control port is an interactive command shell.
Its commands, typically administrative in nature, are implemented as C callbacks.

```
% ./nnctl tcp://127.0.0.1:3333
Connecting to tcp://127.0.0.1:3333.
nnctl> help
help                 this text
version              version info
shutdown             shutdown server
quit                 close session

nnctl> quit
```

The nnctl library is designed for:

 * nanomsg-based C programs on Linux or similar
 * nanomsg programs based on an epoll or similar event loop

The `nnctl` utility comes with the nnctl library. It's a simple utility
to issue commands to any nanomsg-based server that implements a nnctl control port.

How it works

The control port is a nanomsg req-rep socket. Your nanomsg application should,

1. Create a NN_REP socket for the control port
2. Define some command callbacks 
3. Call `nnctl_init` to set up the control port commands 
4. Call `epoll` or similar in your event loop to check for incoming messages
5. Call `nnctl_exec` whenever a message comes in on the control port
6. Call `nnctl_free` at program termination

For the epoll loop, the NN_REP socket's underlying descriptor can be obtained by:

    nn_getsockopt(rep_socket, NN_SOL_SOCKET, NN_RCVFD, &fd, &fd_sz);

Note that the nnctl library does not create any nanomsg sockets itself, nor does
it do any polling on its own.  It relies on the application to provide a rep
socket, and to poll it as above. When a a message is available, the application calls
`nnctl_exec`. At this point the nnctl library internally dequeues the request, and
issues a reply, on the REP socket.

Example

An example is included with the nnctl library in `sample/server.c`. To try it:

    make sample
    sample/server tcp://127.0.0.1:3333 &
    ./nnctl tcp://127.0.0.1:3333

Use `shutdown` to tell the sample server to shutdown. Then issue `quit` to stop
nnctl.

Built-in commands

The `help` and `quit` commands are always built-in to the control port.
The `quit` command disconnects nnctl from the control port.

Build/Install

    git clone git://github.com/troydhanson/nnctl.git
    cd nnctl
    make
    sudo make install

The install target installs nnctl, libnnctl.a and libnnctl.h in the
`/usr/local/` subdirectories `bin`, `lib` and `include`, respectively.

API 

The control port library has these API functions as listed in `libnnctl.h`:

```
nnctl_init     - Set up a data structure for running a control port
nnctl_add_cmd  - Add commands to a control port
nnctl_exec     - Call when epoll says the control port is readable
nnctl_free     - Call when terminating the program to release memory
nnctl_printf   - Called within a command callback to add response text
nnctl_append   - Called within a command callback to append a response buffer
```

See `libnnctl.h` for the full prototypes.

Command callbacks

The control port commands you define must have this prototype:

    int (nnctl_cmd)(nnctl *cp, nnctl_arg *arg, void *data, uint64_t *cookie);

The first argument is there as an opaque pointer which you can pass along to
`nnctl_printf` when forming a command response. The second argument contains the
parameters to the command, as a conventional C argc/argv list (with the addition
of a lenv which has the length of each argument).  Note that `nnctl` turns
double-quoted strings into a single argument similar to a traditional shell.

```
  typedef struct {
    int     argc;
    char  **argv;
    size_t *lenv;
  } nnctl_arg;
```

The third command argument, data, is the opaque value that you passed to
`nnctl_init` or `nnctl_add_cmd` when registering the command.

The fourth command argument, cookie, points to a 64-bit unsigned integer for the
application to use for per-session state. Each nnctl client that connects has an
initially-NULL cookie (that is, `*cookie==NULL`). In the command callback you
can set this to anything (including a pointer, or an index, etc). On subsequent
commands 'from this particular nnctl client' whatever value you set as the
cookie is returned to you. (Security is up to you, if you worry about cookie
forgery). 

`nnctl_init` or `nnctl_add_cmd` when registering the command.

The return value is currently not used but good convention is to return 0 on
success.

// vim: set tw=80 wm=2: 
