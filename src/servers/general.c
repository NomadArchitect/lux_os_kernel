/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

/* Kernel-Server Communication */

#include <string.h>
#include <platform/mmap.h>
#include <platform/platform.h>
#include <kernel/socket.h>
#include <kernel/servers.h>
#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/memory.h>
#include <kernel/tty.h>

static void (*generalRequests[])(Thread *, int, const MessageHeader *req, void *res);

/* handleGeneralRequest(): handles a general server request
 * params: sd - socket descriptor to reply to
 * params: req - request buffer
 * params: res - response buffer
 * returns: nothing - reply is sent to socket
 */

void handleGeneralRequest(int sd, const MessageHeader *req, void *res) {
    if(req->response || !req->requester || req->length < sizeof(MessageHeader))
        return;

    Thread *t = getThread(req->requester);
    if(!t) return;

    // only lumen and its immediate children can communicate with the kernel
    // using this kinda socket
    if(req->requester != getLumenPID()) {
        Process *p = getProcess(t->pid);
        if(!p || p->parent != getLumenPID()) return;
    }

    // and dispatch the call
    if(generalRequests[req->command])
        return generalRequests[req->command](t, sd, req, res);
    else
        KWARN("unhandled general request 0x%02X, dropping\n", req->command);
}

/* getFramebuffer(): provides frame buffer access to the requesting thread */

void getFramebuffer(Thread *t, int sd, const MessageHeader *req, void *res) {
    // set up response header
    FramebufferResponse *response = (FramebufferResponse *) res;
    memcpy(response, req, sizeof(MessageHeader));
    response->header.response = 1;
    response->header.length = sizeof(FramebufferResponse);

    KTTY ttyStatus;
    getTtyStatus(&ttyStatus);

    // we will need to map the frame buffer into the thread's address space
    // so temporarily switch to it
    schedLock();
    if(threadUseContext(t->tid)) return;

    uintptr_t phys = ((uintptr_t)ttyStatus.fb - KERNEL_MMIO_BASE);

    size_t pages = (ttyStatus.h * ttyStatus.pitch + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t base = vmmAllocate(USER_MMIO_BASE, USER_LIMIT_ADDRESS, pages, VMM_USER | VMM_WRITE);
    if(!base) {
        schedRelease();
        return;
    }

    // and finally map it
    for(int i = 0; i < pages; i++) {
        platformMapPage(base + (i * PAGE_SIZE), phys + (i * PAGE_SIZE), PLATFORM_PAGE_PRESENT | PLATFORM_PAGE_USER | PLATFORM_PAGE_WRITE);
    }

    schedRelease(); 

    response->buffer = base;
    response->w = ttyStatus.w;
    response->h = ttyStatus.h;
    response->bpp = ttyStatus.bpp;
    response->pitch = ttyStatus.pitch;

    // and finally send the response
    send(NULL, sd, response, sizeof(FramebufferResponse), 0);
}

/* dispatch table, much like syscalls */

static void (*generalRequests[])(Thread *, int, const MessageHeader *req, void *res) = {
    NULL,               // 0 - sysinfo
    NULL,               // 1 - rand
    NULL,               // 2 - request I/O access
    NULL,               // 3 - get process I/O privileges
    NULL,               // 4 - get list of processes/threads
    NULL,               // 5 - get status of process/thread
    getFramebuffer,     // 6 - request framebuffer access
};