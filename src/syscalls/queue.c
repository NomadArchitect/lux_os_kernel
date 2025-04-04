/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

#include <platform/platform.h>
#include <platform/context.h>
#include <kernel/syscalls.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <kernel/logger.h>

static SyscallRequest *requests = NULL;    // sort of a linked list in a sense

/* syscallHandle(): generic handler for system calls
 * params: ctx - context of the current thread
 * returns: nothing
 */

void syscallHandle(void *ctx) {
    setLocalSched(false);
    Thread *t = getThread(getTid());
    if(t) {
        platformSaveContext(t->context, ctx);
        
        SyscallRequest *req = platformCreateSyscallContext(t);

        // allow immediate handling of IPC syscalls without going through the
        // syscall queue for performance
        if((req->function >= SYSCALL_IPC_START && req->function <= SYSCALL_IPC_END) ||
            (req->function >= SYSCALL_RW_START && req->function <= SYSCALL_RW_END) ||
            (req->function == SYSCALL_LSEEK)) {
            syscallDispatchTable[req->function](req);

            if(req->unblock) {
                req->thread->status = THREAD_RUNNING;
                platformSetContextStatus(t->context, req->ret);
                platformLoadContext(t->context);
            } else {
                req->thread->status = THREAD_BLOCKED;
            }
        } else {
            syscallEnqueue(req);
            t->status = THREAD_BLOCKED;
        }
    }

    for(;;) schedule();  // force context switch!
}

/* syscallEnqueue(): enqueues a syscall request
 * params: request - pointer to the request
 * returns: pointer to the request
 */

SyscallRequest *syscallEnqueue(SyscallRequest *request) {
    schedLock();

    request->queued = true;
    request->unblock = false;
    request->busy = false;

    if(!requests) {
        requests = request;
    } else {
        SyscallRequest *q = requests;
        while(q->next) {
            q = q->next;
        }

        q->next = request;
    }

    if(request->thread->status == THREAD_BLOCKED)
        request->retry = true;

    schedRelease();
    return request;
}

/* syscallDequeue(): dequeues a syscall request
 * params: none
 * returns: pointer to the request, NULL if queue is empty
 */

SyscallRequest *syscallDequeue() {
    schedLock();
    if(!requests) {
        schedRelease();
        return NULL;
    }

    SyscallRequest *request = requests;
    requests = requests->next;
    request->busy = true;
    request->queued = false;

    schedRelease();
    return request;
}

/* syscallProcess(): processes syscalls in the queue from the kernel threads
 * params: none
 * returns: zero if syscall queue is empty
 */

int syscallProcess() {
    if(!requests) return 0;
    SyscallRequest *syscall = syscallDequeue();
    if(!syscall) return 0;
    if(syscall->thread->status != THREAD_BLOCKED) return 0;

    setLocalSched(false);

    // essentially just dispatch the syscall and store the return value
    // in the thread's context so it can get it back
    if(syscall->function > MAX_SYSCALL || !syscallDispatchTable[syscall->function]) {
        KWARN("undefined syscall request %d from tid %d, killing thread...\n", syscall->function, syscall->thread->tid);
        schedLock();
        terminateThread(syscall->thread, -1, false);
        schedRelease();
    } else {
        signalHandle(syscall->thread);
        if(syscall->thread->status == THREAD_ZOMBIE) {
            setLocalSched(true);
            return 1;
        } else if(syscall->thread->status == THREAD_QUEUED) {
            syscallEnqueue(syscall);
        } else if(syscall->thread->status == THREAD_BLOCKED) {
            threadUseContext(syscall->thread->tid);
            syscallDispatchTable[syscall->function](syscall);
            platformSetContextStatus(syscall->thread->context, syscall->ret);
        }
    }

    if((syscall->thread->status == THREAD_BLOCKED) && syscall->unblock) {
        // this way we prevent accidentally running threads that exit()
        syscall->thread->status = THREAD_QUEUED;
        syscall->thread->time = schedTimeslice(syscall->thread, syscall->thread->priority);
        syscall->busy = false;
    }

    setLocalSched(true);
    return 1;
}

/* getSyscall(): returns the syscall request structure of a thread
 * params: tid - thread ID
 * returns: pointer to syscall structure, NULL on fail
 */

SyscallRequest *getSyscall(pid_t tid) {
    Thread *t = getThread(tid);
    if(!t) return NULL;

    return &t->syscall;
}
