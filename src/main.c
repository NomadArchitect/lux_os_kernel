/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

#include <string.h>
#include <stdlib.h>
#include <kernel/logger.h>
#include <kernel/socket.h>
#include <kernel/sched.h>
#include <kernel/modules.h>
#include <kernel/memory.h>
#include <kernel/servers.h>
#include <platform/platform.h>

static int idleThreshold = 0;

void *idleThread(void *args) {
    int count = 0;
    for(;;) {
        if(!syscallProcess()) platformIdle();
        count++;
        if(count >= idleThreshold) {
            count = 0;
            platformIdle();
        }
    }
}

void *kernelThread(void *args) {
    setLocalSched(false);
    setScheduling(false);
    setKernelPID(getPid());

    // open the kernel socket for server communication
    serverInit();

    KDEBUG("attempt to load lumen from ramdisk...\n");

    // spawn the router in user space
    int64_t size = ramdiskFileSize("lumen");
    if(size <= 9) {
        KERROR("lumen not present on the ramdisk, halting because there's nothing to do\n");
        while(1) platformHalt();
    }

    void *lumen = malloc(size);
    if(!lumen) {
        KERROR("failed to allocate memory for lumen, halting because there's nothing to do\n");
        while(1) platformHalt();
    }

    if(ramdiskRead(lumen, "lumen", size) != size) {
        KERROR("failed to read lumen into memory, halting because there's nothing to do\n");
        while(1) platformHalt();
    }

    // TODO: maybe pass boot arguments to lumen?
    pid_t pid = execveMemory(lumen, NULL, NULL);
    free(lumen);

    if(!pid) {
        KERROR("failed to start lumen, halting because there's nothing to do\n");
        while(1) platformHalt();
    }

    setLumenPID(pid);

    PhysicalMemoryStatus ps;
    pmmStatus(&ps);
    KDEBUG("early boot complete, memory usage: %d MiB / %d MiB\n", ps.usedPages>>8, ps.usablePages>>8);

    setLocalSched(true);
    setScheduling(true);

    int count = 0;
    for(;;) {
        serverIdle();
        if(!syscallProcess()) platformIdle();
        count++;
        if(count >= idleThreshold) {
            count = 0;
            platformIdle();
        }
    }
}

// the true kernel entry point is called after platform-specific initialization
// platform-specific code is in platform/[PLATFORM]/main.c

int main(int argc, char **argv) {
    /* the platform-specific main() function must initialize some basic form of
     * output for debugging, physical and virtual memory, and multiprocessing; the
     * boot process will continue here in a more platform-independent fashion */

    socketInit();       // sockets
    schedInit();        // scheduler

    if(platformCountCPU() > 16)
        idleThreshold = 2;
    else if(platformCountCPU() > 8)
        idleThreshold = 4;
    else
        idleThreshold = 8;

    // number of kernel threads = number of CPU cores + 1
    kthreadCreate(&kernelThread, NULL);

    for(int i = 0; i < platformCountCPU(); i++)
        kthreadCreate(&idleThread, NULL);

    // now enable the scheduler
    setScheduling(true);

    while(1) platformHalt();
}