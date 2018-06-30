#include <stdio.h>
#include <string.h>

#include <wups.h>
#include "diibugger/utils.h"
#include <coreinit/cache.h>
#include <coreinit/memorymap.h>

void WriteCode(uint32_t address, uint32_t instr) {
    uint32_t replace_instr = instr;

    ICInvalidateRange(&replace_instr, 4);
    DCFlushRange(&replace_instr, 4);

    WUPS_KernelCopyDataFunction((uint32_t)OSEffectiveToPhysical((uint32_t) address), (uint32_t)OSEffectiveToPhysical((uint32_t)&replace_instr), 4);
    ICInvalidateRange((void*)(address), 4);
    DCFlushRange((void*)(address), 4);

    //DEBUG_FUNCTION_LINE("Did KernelCopyData. %08X = %08X\n",address, *(uint32_t*)address);
}

bool isValidStackPtr(uint32_t sp) {
    return sp >= 0x10000000 && sp < 0x20000000;
}

uint32_t PushThread(char *buffer, uint32_t offset, OSThread *thread) {
    *(uint32_t *)(buffer + offset) = OSGetThreadAffinity(thread);
    *(uint32_t *)(buffer + offset + 4) = OSGetThreadPriority(thread);
    *(uint32_t *)(buffer + offset + 8) = (uint32_t)thread->stackStart;
    *(uint32_t *)(buffer + offset + 12) = (uint32_t)thread->stackEnd;
    *(uint32_t *)(buffer + offset + 16) = (uint32_t)thread->entryPoint;

    const char *threadName = OSGetThreadName(thread);
    if (threadName) {
        uint32_t namelen = strlen(threadName);
        *(uint32_t *)(buffer + offset + 20) = namelen;
        memcpy(buffer + offset + 24, threadName, namelen);
        return 24 + namelen;
    }

    *(uint32_t *)(buffer + offset + 20) = 0;
    return 24;
}
