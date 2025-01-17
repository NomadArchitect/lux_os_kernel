/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <kernel/sched.h>
#include <platform/lock.h>

/* system-wide limits */
#define MAX_SOCKETS             (1 << 18)   // 262k
#define SOCKET_DEFAULT_BACKLOG  1024        // default socket backlog size

#define SOCKET_IO_BACKLOG       64          // default I/O backlog size

/* socket family/domain - only Unix sockets will be implemented in the kernel */
#define AF_UNIX                 1
#define AF_LOCAL                AF_UNIX

/* socket type - these will be ignored for local Unix sockets */
/* the kernel will ensure packets are sent and received in the same order */
#define SOCK_STREAM             1       // stream-oriented
#define SOCK_DGRAM              2       // datagram-oriented
#define SOCK_SEQPACKET          3       // connection-oriented

/* additional socket flags */
#define SOCK_NONBLOCK           0x100
#define SOCK_CLOEXEC            0x200

/* flags for socket I/O */
#define MSG_PEEK                0x01
#define MSG_OOB                 0x02
#define MSG_WAITALL             0x04

typedef uint16_t sa_family_t;
typedef size_t socklen_t;

/* generic socket */
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[512];
};

/* Unix domain socket */
struct sockaddr_un {
    sa_family_t sun_family;     // AF_UNIX
    char sun_path[512];         // filename
};

/* socket-specific I/O descriptor (see io.h) */
typedef struct SocketDescriptor {
    Process *process;
    struct sockaddr address;
    socklen_t addressLength;
    lock_t lock;
    bool listener;
    int globalIndex;
    int type, protocol, backlogMax, backlogCount;
    int inboundMax, outboundMax;        // buffer sizes
    int inboundCount, outboundCount;
    void **inbound, **outbound;
    size_t *inboundLen, *outboundLen;
    struct SocketDescriptor **backlog;  // for incoming connections via connect()
    struct SocketDescriptor *peer;      // for peer-to-peer connections
    int refCount;
} SocketDescriptor;

void socketInit();
SocketDescriptor *getLocalSocket(const struct sockaddr *, socklen_t);
void socketLock();
void socketRelease();
int socketRegister(SocketDescriptor *);
SocketDescriptor *socketUnregister(int);

/* socket system calls */
int socket(Thread *, int, int, int);
int connect(Thread *, int, const struct sockaddr *, socklen_t);
int bind(Thread *, int, const struct sockaddr *, socklen_t);
int listen(Thread *, int, int);
int accept(Thread *, int, struct sockaddr *, socklen_t *);
ssize_t recv(Thread *, int, void *, size_t, int);
ssize_t send(Thread *, int, const void *, size_t, int);
int closeSocket(Thread *, int);
