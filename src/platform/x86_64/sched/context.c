/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Platform-Specific Code for x86_64
 */

/* This file contains the context manipulation for the scheduler */
/* The definition of context varies by CPU architecture, so hide the difference
 * behind this abstraction layer. */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <platform/context.h>
#include <platform/platform.h>
#include <platform/smp.h>
#include <platform/x86_64.h>
#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/memory.h>
#include <kernel/syscalls.h>

/* platformGetPid(): returns the PID of the process running on the current CPU
 * params: none
 * returns: process ID, zero if idle
 */

pid_t platformGetPid() {
    KernelCPUInfo *kinfo = getKernelCPUInfo();
    if(kinfo->process) return kinfo->process->pid;
    return 0;
}

/* platformGetTid(): returns the TID of the thread running on the current CPU
 * params: none
 * returns: thread ID, zero if idle
 */

pid_t platformGetTid() {
    KernelCPUInfo *kinfo = getKernelCPUInfo();
    if(kinfo->thread) return kinfo->thread->tid;
    return 0;
}

/* platformGetProcess(): returns the current process structure
 * params: none
 * returns: pointer to process struct
 */

Process *platformGetProcess() {
    KernelCPUInfo *kinfo = getKernelCPUInfo();
    if(kinfo) return kinfo->process;
    return NULL;
}

/* platformGetThread(): returns the current thread structure
 * params: none
 * returns: pointer to thread struct
 */

Thread *platformGetThread() {
    KernelCPUInfo *kinfo = getKernelCPUInfo();
    if(kinfo) return kinfo->thread;
    return NULL;
}

/* platformCreateContext(): creates the context for a new thread
 * params: ptr - pointer to the context structure
 * params: level - kernel/user space
 * params: entry - entry point of the thread
 * params: arg - argument to be passed to the thread
 * returns: pointer to the context structure, NULL on failure
 */

void *platformCreateContext(void *ptr, int level, uintptr_t entry, uintptr_t arg) {
    memset(ptr, 0, PLATFORM_CONTEXT_SIZE);

    ThreadContext *context = (ThreadContext *)ptr;
    context->regs.rip = entry;
    context->regs.rdi = arg;
    context->regs.rflags = 0x202;
    context->cr3 = (uint64_t)platformCloneKernelSpace() - KERNEL_BASE_ADDRESS;
    //if(!context->cr3) return NULL;
    void *stack;

    memset(context->ioports, 0xFF, 8192);   // disable I/O port access by default

    if(level == PLATFORM_CONTEXT_KERNEL) {
        context->regs.cs = GDT_KERNEL_CODE << 3;
        context->regs.ss = GDT_KERNEL_DATA << 3;
        stack = calloc(1, PLATFORM_THREAD_STACK);
        if(!stack) return NULL;
        context->regs.rsp = (uint64_t)stack + PLATFORM_THREAD_STACK;

        // TODO: handle implicit thread termination by returning
        // to do so we would need to push a return function to the thread's stack

        return ptr;
    } else {
        context->regs.cs = (GDT_USER_CODE << 3) | PRIVILEGE_USER;
        context->regs.ss = (GDT_USER_DATA << 3) | PRIVILEGE_USER;

        // stack will not be setup here for user processes because it depends
        // on the process's size and location and etc
        // same for entry point and args

        return ptr;
    }
}

/* platformSwitchContext(): switches the current thread context 
 * params: t - thread to switch to
 * returns: doesn't return
 */

void platformSwitchContext(Thread *t) {
    KernelCPUInfo *kinfo = getKernelCPUInfo();
    ThreadContext *ctx = (ThreadContext *)t->context;
    //

    if((ctx->regs.cs & 0x03) || (ctx->regs.ss & 0x03)) {
        ctx->regs.cs = (GDT_USER_CODE << 3) | 3;
        ctx->regs.ss = (GDT_USER_DATA << 3) | 3;
        ctx->regs.rflags |= 0x202;
    }

    // modify the TSS with the current thread's I/O permissions if necessary
    Thread *old = kinfo->thread;
    ThreadContext *oldctx = NULL;
    if(old) oldctx = (ThreadContext *) old->context;

    if(ctx->iopl || (oldctx && oldctx->iopl)) {
        memcpy(kinfo->tss->ioports, ctx->ioports, 8192);
    }

    kinfo->thread = t;
    kinfo->process = getProcess(t->pid);
    platformLoadContext(t->context);
}

/* platformUseContext(): switches to the paging context of a thread
 * params: ptr - thread context
 * returns: zero
 */

int platformUseContext(void *ptr) {
    ThreadContext *ctx = (ThreadContext *)ptr;
    writeCR3(ctx->cr3);
    return 0;
}

/* platformSetContext(): sets up the context for a user space thread
 * params: t - thread
 * params: entry - entry point
 * params: highest - highest address loaded
 * params: argv - arguments to be passed
 * params: envp - environmental variables to be passed
 * returns: zero on success
 */

int platformSetContext(Thread *t, uintptr_t entry, uintptr_t highest, const char **argv, const char **envp) {
    /* this sets up an entry point for the thread that's something like
     * void _start(const char **argv, const char **envp) */

    if(platformSignalSetup(t)) return -1;

    ThreadContext *ctx = (ThreadContext *)t->context;
    ctx->regs.rip = entry;
    ctx->regs.rdi = (uint64_t)argv;
    ctx->regs.rsi = (uint64_t)envp;

    // allocate memory for the arguments and environmental variables
    uintptr_t base = highest;
    while(base % PAGE_SIZE) {
        base++;
    }

    base += PAGE_SIZE;      // guard page

    // parse args
    if(argv) {
        int argc;
        for(argc = 0; argv[argc]; argc++);

        uintptr_t *args = (uintptr_t *) vmmAllocate(base, USER_LIMIT_ADDRESS, 1, VMM_WRITE | VMM_USER);
        if(!args) return -1;

        ctx->regs.rdi = (uint64_t) args;

        // copy args
        for(int i = 0; argc && i < argc; i++) {
            args[i] = vmmAllocate(base, USER_LIMIT_ADDRESS, 1, VMM_WRITE | VMM_USER);
            if(!args[i]) return -1;
            strcpy((char *) args[i], argv[i]);
        }

        // and null terminate
        args[argc] = 0;
    }

    // likewise for the env variables
    if(envp) {
        int envc;
        for(envc = 0; envp[envc]; envc++);

        uintptr_t *envs = (uintptr_t *) vmmAllocate(base, USER_LIMIT_ADDRESS, 1, VMM_WRITE | VMM_USER);
        if(!envs) return -1;

        ctx->regs.rsi = (uint64_t) envs;

        // copy env variables
        for(int i = 0; envc && i < envc; i++) {
            envs[i] = vmmAllocate(base, USER_LIMIT_ADDRESS, 1, VMM_WRITE | VMM_USER);
            if(!envs[i]) return -1;
            strcpy((void *) envs[i], envp[i]);
        }

        // and null terminate
        envs[envc] = 0;
    }

    // now allocate a stack
    size_t pages = (PLATFORM_THREAD_STACK+PAGE_SIZE-1)/PAGE_SIZE;
    pages++;

    uintptr_t stack = vmmAllocate(base, USER_LIMIT_ADDRESS, pages, VMM_WRITE | VMM_USER);
    if(!stack) return -1;
    memset((void *)stack, 0, PLATFORM_THREAD_STACK + PAGE_SIZE);

    stack += PLATFORM_THREAD_STACK;
    ctx->regs.rsp = stack;

    t->highest = stack + PAGE_SIZE;     // requisite to sbrk()

    t->pages = (t->highest - USER_BASE_ADDRESS + PAGE_SIZE - 1) / PAGE_SIZE;
    return 0;
}

/* platformCreateSyscallContext(): creates a structure with syscall params from thread context
 * this is architecture-specific because of different registers and ABIs
 * params: t - thread structure
 * returns: pointer to syscall request
 */

SyscallRequest *platformCreateSyscallContext(Thread *t) {
    ThreadContext *ctx = (ThreadContext *)t->context;

    // syscall function is passed in RAX
    // remaining parameters follow the SysV ABI with the exception of RCX
    // because RCX is trashed by the SYSCALL instruction (see syscalls.asm)

    // long story short, FOUR C-style params are in RDI, RSI, RDX, and R8
    t->syscall.next = NULL;
    t->syscall.busy = false;
    t->syscall.function = ctx->regs.rax;
    t->syscall.params[0] = ctx->regs.rdi;
    t->syscall.params[1] = ctx->regs.rsi;
    t->syscall.params[2] = ctx->regs.rdx;
    t->syscall.params[3] = ctx->regs.r8;
    t->syscall.thread = t;      // back pointer to the thread
    t->syscall.retry = false;

    return &t->syscall;
}

/* platformCloneContext(): creates a deep clone of a thread's context
 * params: cctx - pointer to child context
 * params: pctx - pointer to parent context
 * returns: pointer to child context on success, NULL on failure
 */

void *platformCloneContext(void *cctx, const void *pctx) {
    ThreadContext *child = (ThreadContext *)cctx;
    ThreadContext *parent = (ThreadContext *)pctx;

    // first copy the register states
    memcpy(child, parent, sizeof(ThreadContext));

    // now create a deep clone of the LOWER HALF of the paging structures
    // the kernel is always present in the higher half of every address space
    // and is unchanging, so it doesn't need cloning
    child->cr3 = (uint64_t)platformCloneUserSpace(parent->cr3);
    if(!child->cr3) return NULL;
    return child;
}

/* platformSetContextStatus(): sets the return value after a syscall
 * this is a platform-specific function because the register used to store the
 * return value differs by ABI and thus by platform
 * params: ctx - pointer to thread context
 * params: value - return value to be passed
 * returns: nothing
 */

void platformSetContextStatus(void *ctx, uint64_t value) {
    // again on x86_64 we're following the System V ABI
    // so return values will simply be passed in the RAX register
    ThreadContext *context = (ThreadContext *)ctx;
    context->regs.rax = value;
}

/* setLocalSched(): enables or disables scheduling on one CPU
 * params: sched - true/false
 * returns: nothing
 */

void setLocalSched(bool sched) {
    if(sched) enableIRQs();
    else disableIRQs();
}

/* freePT(): frees a page table
 * params: base - base pointer to the page table
 * params: depth - 0 for PML4, 1 for PDP, 2 for PD, 3 for PT
 * params: maxdepth - depth to stop recursing at
 * returns: nothing
 */

void freePT(uint64_t *base, int depth, int maxdepth) {
    if(depth < 0 || depth > maxdepth) return;

    PhysicalMemoryStatus st;
    pmmStatus(&st);

    for(int i = 0; i < 512; i++) {
        uint64_t entry = base[i];
        uint64_t phys = entry & ~((PAGE_SIZE-1) | PT_PAGE_NXE);
        if((entry & PT_PAGE_PRESENT) && (phys) && (phys < st.highestUsableAddress)) {
            if(depth < maxdepth)
                freePT((uint64_t *) vmmMMIO(phys, true), depth+1, maxdepth);

            pmmFree(phys);
        }
    }
}

/* platformCleanThread(): cleans up the memory space used by a thread after it
 * is no longer running
 * params: ptr - pointer to thread context
 * params: highest - highest memory address used by thread
 * returns: nothing
 */

void platformCleanThread(void *ptr, uintptr_t highest) {
    if(!ptr || highest <= USER_BASE_ADDRESS+PAGE_SIZE) return;
    ThreadContext *ctx = ptr;
    if(!ctx->cr3) return;
    
    // free the page tables themselves and all associated physical mem
    //uint64_t saved = readCR3();
    //writeCR3(ctx->cr3);
    //vmmFree(USER_BASE_ADDRESS, (highest-USER_BASE_ADDRESS) / PAGE_SIZE);
    //writeCR3(saved);

    uint64_t *pml4 = (uint64_t *) vmmMMIO(ctx->cr3, true);
    for(int i = 0; i < 256; i++) {
        freePT((uint64_t *) vmmMMIO(pml4[i] & ~(PAGE_SIZE-1), true), 1, 3);
    }

    pmmFree(ctx->cr3);
}
