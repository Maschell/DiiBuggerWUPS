#ifndef _DIIBUGGER_DEBUGGER_H_
#define _DIIBUGGER_DEBUGGER_H_

#include "common/diibugger_defs.h"
#include <system/CThread.h>

#ifdef __cplusplus
extern "C" {
#endif
BOOL ProgramHandler_Initialize(OSContext *context);

BOOL DSIHandler_Debug(OSContext *context);

BOOL ISIHandler_Debug(OSContext *context);

BOOL ProgramHandler_Debug(OSContext *context);

void HandleProgram();

void ReportCrash(uint32_t msg);

void HandleDSI();

void HandleISI();

BOOL DSIHandler_Fatal(OSContext *context);

BOOL ISIHandler_Fatal(OSContext *context);

BOOL ProgramHandler_Fatal(OSContext *context);

void FatalCrashHandler();

#ifdef __cplusplus
}
#endif

#endif /* _DIIBUGGER_DEBUGGER_H_ */
