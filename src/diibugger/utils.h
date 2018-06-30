#ifndef _DIIBUGGER_UTILS_H_
#define _DIIBUGGER_UTILS_H_

#include <common/diibugger_defs.h>
#include <system/CThread.h>
#include "common/diibugger_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
    \brief Writes an instruction to a given address, bypassing any kernel checks.
    Caution: if the given address needs to be mappable to physical space via OSEffectiveToPhysical

    \param address:     The address where the value will be written.
    \param instr:       The value that will be written to the given address.
**/
void WriteCode(uint32_t address, uint32_t instr);

/**
    \brief Checks if the given address is a valid stack pointer
    \return Return true if it's a valid stack pointer, false otherwise.
**/
bool isValidStackPtr(uint32_t sp);

/**
    \brief  Pushes threads information into an existing buffer with thread information.
            The result will be sent to the client.

    \param  buffer: An existing buffer where the result will be stored
    \param  offset: The offset at where the information of the given thread should be stored
    \param  thread: A pointer to the thread whose information should be stored into the buffer.
**/
uint32_t PushThread(char *buffer, uint32_t offset, OSThread *thread);

#ifdef __cplusplus
}
#endif

#endif /* _DIIBUGGER_UTILS_H_ */
