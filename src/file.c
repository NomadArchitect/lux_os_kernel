/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Core Microkernel
 */

/* Wrappers for file I/O functions */
/* None of these functions actually do anything because the microkernel has no
 * concept of files; these functions just relay the calls to lumen and request
 * a user space server to fulfill their requests */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/file.h>
#include <kernel/io.h>
#include <kernel/sched.h>
#include <kernel/servers.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <kernel/logger.h>

int mount(Thread *t, uint64_t id, const char *src, const char *tgt, const char *type, int flags) {
    // send a request to lumen
    MountCommand *command = calloc(1, sizeof(MountCommand));
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_MOUNT;
    command->header.header.length = sizeof(MountCommand);
    command->header.id = id;
    command->flags = flags;
    strcpy(command->source, src);
    strcpy(command->target, tgt);
    strcpy(command->type, type);

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}

int stat(Thread *t, uint64_t id, const char *path, struct stat *buffer) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    StatCommand *command = calloc(1, sizeof(StatCommand));
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_STAT;
    command->header.header.length = sizeof(StatCommand);
    command->header.id = id;

    if(path[0] == '/') {
        strcpy(command->path, path);
    } else {
        strcpy(command->path, p->cwd);
        if(strlen(p->cwd) > 1) command->path[strlen(command->path)] = '/';
        strcpy(command->path + strlen(command->path), path);
    }

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}

int fstat(Thread *t, uint64_t id, int fd, struct stat *buffer) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;
    if(fd < 0 || fd >= MAX_IO_DESCRIPTORS) return -EBADF;
    if(!p->io[fd].valid || !p->io[fd].data) return -EBADF;  // ensure valid file descriptor
    if(p->io[fd].type != IO_FILE) return -EBADF;

    FileDescriptor *file = (FileDescriptor *) p->io[fd].data;
    return stat(t, id, file->abspath, buffer);
}

int open(Thread *t, uint64_t id, const char *path, int flags, mode_t mode) {
    OpenCommand *command = calloc(1, sizeof(OpenCommand));
    if(!command) return -ENOMEM;

    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    command->header.header.command = COMMAND_OPEN;
    command->header.header.length = sizeof(OpenCommand);
    command->header.id = id;
    command->flags = flags;
    command->mode = mode & (S_IRWXU | S_IRWXG | S_IRWXO);
    command->uid = p->user;
    command->gid = p->group;
    command->umask = p->umask;

    if(path[0] == '/') {
        strcpy(command->abspath, path);
    } else {
        strcpy(command->abspath, p->cwd);
        if(strlen(p->cwd) > 1) command->abspath[strlen(command->abspath)] = '/';
        strcpy(command->abspath + strlen(command->abspath), path);
    }

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}

ssize_t readFile(Thread *t, uint64_t id, IODescriptor *iod, void *buffer, size_t count) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    FileDescriptor *fd = (FileDescriptor *) iod->data;
    if(!fd) return -EBADF;

    if(!(iod->flags & O_RDONLY)) return -EPERM;

    RWCommand *command = calloc(1, sizeof(RWCommand));
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_READ;
    command->header.header.length = sizeof(RWCommand);
    command->header.id = id;
    command->uid = p->user;
    command->gid = p->group;
    command->position = fd->position;
    command->flags = iod->flags;
    command->length = count;
    command->id = fd->id;
    strcpy(command->device, fd->device);
    strcpy(command->path, fd->path);

    int status = requestServer(t, fd->sd, command);

    free(command);
    return status;
}

ssize_t writeFile(Thread *t, uint64_t id, IODescriptor *iod, const void *buffer, size_t count) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    FileDescriptor *fd = (FileDescriptor *) iod->data;
    if(!fd) return -EBADF;

    if(!(iod->flags & O_WRONLY)) return -EPERM;

    RWCommand *command = calloc(1, sizeof(RWCommand) + count);
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_WRITE;
    command->header.header.length = sizeof(RWCommand) + count;
    command->header.id = id;
    command->uid = p->user;
    command->gid = p->group;

    // persistent file system drivers will know to append when the position
    // is a negative value
    if(iod->flags & O_APPEND) command->position = -1;
    else command->position = fd->position;

    command->flags = iod->flags;
    command->length = count;
    command->id = fd->id;
    strcpy(command->device, fd->device);
    strcpy(command->path, fd->abspath);
    memcpy(command->data, buffer, count);

    if(fd->charDev) command->silent = 1;

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}

int closeFile(Thread *t, int fd) {
    if(fd < 0 || fd >= MAX_IO_DESCRIPTORS) return -EBADF;

    Process *p;
    if(t) p = getProcess(t->pid);
    else p = getProcess(getKernelPID());
    if(!p) return -ESRCH;

    FileDescriptor *file = (FileDescriptor *) p->io[fd].data;
    if(!file) return -EBADF;

    // TODO: flush the file buffers here to allow drivers to implement caching

    file->refCount--;
    if(!file->refCount) free(file);

    closeIO(p, &p->io[fd]);
    return 0;
}

off_t lseek(Thread *t, int fd, off_t offset, int where) {
    if(fd < 0 || fd >= MAX_IO_DESCRIPTORS) return -EBADF;

    Process *p;
    if(t) p = getProcess(t->pid);
    else p = getProcess(getKernelPID());
    if(!p) return -ESRCH;

    FileDescriptor *file = (FileDescriptor *) p->io[fd].data;
    if(!file) return -EBADF;

    off_t newOffset;

    switch(where) {
    case SEEK_SET:
        newOffset = offset;
        break;
    case SEEK_CUR:
        newOffset = file->position + offset;
        break;
    default:
        /* TODO: SEEK_END */
        newOffset = -1;
    }

    if(newOffset < 0) return -EINVAL;
    file->position = newOffset;
    return newOffset;
}

int fcntl(Thread *t, int fd, int cmd, uintptr_t arg) {
    if(fd < 0 || fd >= MAX_IO_DESCRIPTORS) return -EBADF;
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    if(!p->io[fd].valid) return -EBADF;

    int status = 0;
    
    switch(cmd) {
    case F_GETFD:
        if(p->io[fd].flags & O_CLOEXEC) status |= FD_CLOEXEC;
        if(p->io[fd].flags & O_CLOFORK) status |= FD_CLOFORK;
        return status;

    case F_GETFL:
        return (int) p->io[fd].flags & (O_APPEND|O_NONBLOCK|O_SYNC|O_DSYNC);

    case F_SETFD:
        if(arg & FD_CLOEXEC) p->io[fd].flags |= O_CLOEXEC;
        else p->io[fd].flags &= ~(O_CLOEXEC);
        if(arg & FD_CLOFORK) p->io[fd].flags |= O_CLOFORK;
        else p->io[fd].flags &= ~(O_CLOFORK);
        break;

    case F_SETFL:
        if(arg & O_APPEND) p->io[fd].flags |= O_APPEND;
        else p->io[fd].flags &= ~(O_APPEND);
        if(arg & O_NONBLOCK) p->io[fd].flags |= O_NONBLOCK;
        else p->io[fd].flags &= ~(O_NONBLOCK);
        if(arg & O_SYNC) p->io[fd].flags |= O_SYNC;
        else p->io[fd].flags &= ~(O_SYNC);
        if(arg & O_DSYNC) p->io[fd].flags |= O_DSYNC;
        else p->io[fd].flags &= ~(O_DSYNC);
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

mode_t umask(Thread *t, mode_t cmask) {
    Process *p = getProcess(t->pid);
    mode_t old = p->umask;
    cmask &= (S_IRWXU | S_IRWXG | S_IRWXO);
    p->umask = cmask;
    return old;
}

int chown(Thread *t, uint64_t id, const char *path, uid_t owner, gid_t group) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    ChownCommand *command = calloc(1, sizeof(ChownCommand));
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_CHOWN;
    command->header.header.length = sizeof(ChownCommand);
    command->header.id = id;
    command->uid = p->user;
    command->gid = p->group;
    command->newUid = owner;
    command->newGid = group;

    if(path[0] == '/') {
        strcpy(command->path, path);
    } else {
        strcpy(command->path, p->cwd);
        if(strlen(p->cwd) > 1) command->path[strlen(command->path)] = '/';
        strcpy(command->path + strlen(command->path), path);
    }

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}

int chmod(Thread *t, uint64_t id, const char *path, mode_t mode) {
    Process *p = getProcess(t->pid);
    if(!p) return -ESRCH;

    ChmodCommand *command = calloc(1, sizeof(ChmodCommand));
    if(!command) return -ENOMEM;

    command->header.header.command = COMMAND_CHMOD;;
    command->header.header.length = sizeof(ChmodCommand);
    command->header.id = id;
    command->uid = p->user;
    command->gid = p->group;
    command->mode = mode;

    if(path[0] == '/') {
        strcpy(command->path, path);
    } else {
        strcpy(command->path, p->cwd);
        if(strlen(p->cwd) > 1) command->path[strlen(command->path)] = '/';
        strcpy(command->path + strlen(command->path), path);
    }

    int status = requestServer(t, 0, command);
    free(command);
    return status;
}
