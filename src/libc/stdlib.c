/*
 * lux - a lightweight unix-like operating system
 * Omar Elghoul, 2024
 * 
 * Kernel-Level Partial Implementation of the C Library
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <kernel/memory.h>

struct mallocHeader {
    uint64_t byteSize;
    uint64_t pageSize;
};

char *itoa(int n, char *buffer, int radix) {
    return ltoa((long)n, buffer, radix);
}

int atoi(const char *s) {
    return (long)atol(s);
}

char *ltoa(long n, char *buffer, int radix) {
    if(!radix || radix > HEX) return NULL;

    if(!n) {
        buffer[0] = '0';
        buffer[1] = 0;
        return buffer;
    }

    int length = 0;

    while(n) {
        // convert digit by digit and then reverse the string
        long digit = n % radix;
        if(digit < 0) digit *= -1;

        if(digit >= 10) {
            buffer[length] = 'a' + digit - 10;
        } else {
            buffer[length] = '0' + digit;
        }

        length++;
        n /= radix;
    }

    buffer[length] = 0;   // null terminator

    // now reverse the string
    if(length >= 2) {
        for(int i = 0; i < length/2; i++) {
            char tmp = buffer[i];
            buffer[i] = buffer[length-i-1];
            buffer[length-i-1] = tmp;
        }
    }

    return buffer;
}

long atol(const char *s) {
    long v = 0;
    int len = 0;

    while(s[len] >= '0' && s[len] <= '9') {
        len++;      // didn't use strlen so we can only account for numerical characters
    }

    if(!len) return 0;

    char buffer[20];

    long multiplier = 1;
    for(int i = 1; i < len; i++) {
        multiplier *= 10;
    }

    for(int i = 0; i < len; i++) {
        long digit = s[i] - '0';
        v += (digit * multiplier);
        multiplier /= 10;
    }

    return v;
}

void *malloc(size_t size) {
    if(!size) return NULL;
    size_t pageSize = (size + sizeof(struct mallocHeader) + PAGE_SIZE - 1) / PAGE_SIZE;

    // allocate memory with kernel permissions, write permissions, and no execute
    // there's probably never a scenario where it's a good idea to execute code
    // in a block of memory allocated by malloc() or its derivatives
    uintptr_t ptr = vmmAllocate(KERNEL_HEAP_BASE, USER_BASE_ADDRESS, pageSize, VMM_WRITE);
    if(!ptr) return NULL;

    struct mallocHeader *header = (struct mallocHeader *)ptr;
    header->byteSize = size;
    header->pageSize = pageSize;

    return (void *)((uintptr_t)ptr + sizeof(struct mallocHeader));
}

void *calloc(size_t num, size_t size) {
    void *ptr = malloc(num * size);
    if(!ptr) return NULL;
    memset(ptr, 0, num * size);
    return ptr;
}

void *realloc(void *ptr, size_t newSize) {
    if(!newSize) return NULL;
    if(!ptr) return malloc(newSize);

    void *newPtr = malloc(newSize);
    if(!newPtr) return NULL;

    uintptr_t oldBase = (uintptr_t)ptr;
    oldBase &= ~(PAGE_SIZE-1);
    struct mallocHeader *header = (struct mallocHeader *)oldBase;
    size_t oldSize = header->byteSize;

    if(oldSize > newSize) {
        // we're shrinking the memory, copy the new size only
        memcpy(newPtr, ptr, newSize);
    } else {
        memcpy(newPtr, ptr, oldSize);
    }

    free(ptr);
    return newPtr;
}

void free(void *ptr) {
    if(!ptr) return;
    uintptr_t base = (uintptr_t)ptr;
    base &= ~(PAGE_SIZE-1);
    struct mallocHeader *header = (struct mallocHeader *)base;
    vmmFree(base, header->pageSize);
}