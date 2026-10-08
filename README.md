# RemoteOps — IE3090 Network Programming

## Student
- Registration Number: IT24300059
- Project: RemoteOps — A Remote System Monitoring and Management Tool over TCP/IP

## Overview
RemoteOps is a C client-server application using BSD sockets. The Agent acts as the managed-machine server and the Controller provides the administrator interface.

TCP is used for reliable control commands and file transfers. UDP is used for periodic system monitoring.

## Personalised Values
- TCP Port: 9430
- Session ID: SID:9500
- Authentication Token: OPS-0059
- Agent: `agent_059.c`
- Controller: `controller_059.c`
- Makefile: `Makefile_059`
- Log: `remoteops_IT24300059.log`
- Storage: `./agentfiles/IT24300059/`

## Main Features
- AUTH authentication
- SYSINFO system information
- LISTPROC process listing
- Restricted EXEC commands: DATE, UPTIME, DISKFREE, HOSTNAME, WHOAMI
- PUT file upload
- GET file download
- UDP MONITOR START / MONITOR STOP
- Session logging
- Concurrent Controller connections using POSIX threads
- Graceful QUIT/disconnect handling

## Build

On Linux/CentOS with GCC:

```bash
make -f Makefile_059
```

This builds:

```text
agent_059
controller_059
```

To remove the compiled programs:

```bash
make -f Makefile_059 clean
```

## Run

Start the Agent:

```bash
./agent_059
```

In another terminal, start the Controller:

```bash
./controller_059
```

Authenticate using the personalised token:

```text
OPS-0059
```

The Agent listens on TCP port `9430`.

## Testing
The implementation was tested for authentication, SYSINFO, LISTPROC, restricted EXEC, PUT, GET, UDP monitoring start/stop, logging, and personalised storage. The Implementation Report contains the execution screenshots and testing summary.
