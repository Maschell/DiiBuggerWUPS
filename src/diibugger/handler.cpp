#include <stdio.h>
#include <string.h>
#include "Diibugger.hpp"
#include "diibugger/utils.h"
#include "handler.h"
#include <coreinit/debug.h>
#include <coreinit/exception.h>

bool Diibugger::handle_crash(uint32_t type, void * handler,OSContext * context) {
    // This is setting the crash context so we can use it in the client!
    memcpy((char *)&crashContext, (const char *)context, sizeof(OSContext));
    crashType = type;
    context->srr0 = (uint32_t)handler;
    return true;
}

void FatalCrashHandler() {
    OSContext * crashContext = Diibugger::getInstance()->getCrashContext();
    uint8_t crashType = Diibugger::getInstance()->getCrashType();
    char buffer[0x400];
    snprintf(buffer, 0x400,
             "An exception of type %i occurred:\n\n"
             "r0: %08X r1: %08X r2: %08X r3: %08X r4: %08X\n"
             "r5: %08X r6: %08X r7: %08X r8: %08X r9: %08X\n"
             "r10:%08X r11:%08X r12:%08X r13:%08X r14:%08X\n"
             "r15:%08X r16:%08X r17:%08X r18:%08X r19:%08X\n"
             "r20:%08X r21:%08X r22:%08X r23:%08X r24:%08X\n"
             "r25:%08X r26:%08X r27:%08X r28:%08X r29:%08X\n"
             "r30:%08X r31:%08X\n\n"
             "CR: %08X LR: %08X CTR:%08X XER:%08X\n"
             "EX0:%08X EX1:%08X SRR0:%08X SRR1:%08X\n",
             (unsigned int) crashType,
             (unsigned int) crashContext->gpr[0],
             (unsigned int) crashContext->gpr[1],
             (unsigned int) crashContext->gpr[2],
             (unsigned int) crashContext->gpr[3],
             (unsigned int) crashContext->gpr[4],
             (unsigned int) crashContext->gpr[5],
             (unsigned int) crashContext->gpr[6],
             (unsigned int) crashContext->gpr[7],
             (unsigned int) crashContext->gpr[8],
             (unsigned int) crashContext->gpr[9],
             (unsigned int) crashContext->gpr[10],
             (unsigned int) crashContext->gpr[11],
             (unsigned int) crashContext->gpr[12],
             (unsigned int) crashContext->gpr[13],
             (unsigned int) crashContext->gpr[14],
             (unsigned int) crashContext->gpr[15],
             (unsigned int) crashContext->gpr[16],
             (unsigned int) crashContext->gpr[17],
             (unsigned int) crashContext->gpr[18],
             (unsigned int) crashContext->gpr[19],
             (unsigned int) crashContext->gpr[20],
             (unsigned int) crashContext->gpr[21],
             (unsigned int) crashContext->gpr[22],
             (unsigned int) crashContext->gpr[23],
             (unsigned int) crashContext->gpr[24],
             (unsigned int) crashContext->gpr[25],
             (unsigned int) crashContext->gpr[26],
             (unsigned int) crashContext->gpr[27],
             (unsigned int) crashContext->gpr[28],
             (unsigned int) crashContext->gpr[29],
             (unsigned int) crashContext->gpr[30],
             (unsigned int) crashContext->gpr[31],
             (unsigned int) crashContext->cr,
             (unsigned int) crashContext->lr,
             (unsigned int) crashContext->ctr,
             (unsigned int) crashContext->xer,
             (unsigned int) crashContext->__unk1[0],
             (unsigned int) crashContext->__unk1[4],
             (unsigned int) crashContext->srr0,
             (unsigned int) crashContext->srr1
            );

    OSFatal(buffer);
}

BOOL DSIHandler_Fatal(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_DSI, (void*)FatalCrashHandler,context);
}
BOOL ISIHandler_Fatal(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_ISI, (void*)FatalCrashHandler,context);
}
BOOL ProgramHandler_Fatal(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_PROGRAM, (void*)FatalCrashHandler,context);
}

void HandleDSI() {
    Diibugger::getInstance()->ReportCrash((uint32_t) SERVER_MESSAGE_DSI);
}

void HandleISI() {
    Diibugger::getInstance()->ReportCrash((uint32_t) SERVER_MESSAGE_ISI);
}

void HandleProgram() {
    Diibugger::getInstance()->HandleProgram();
}

void Diibugger::HandleProgram() {
    //DEBUG_FUNCTION_LINE("HandleProgram\n");
    //Check if the exception was caused by a breakpoint
    if (!(crashContext.srr1 & 0x20000)) {
        //DEBUG_FUNCTION_LINE("Was caused by a breakpoint!\n");
        ReportCrash((uint32_t) SERVER_MESSAGE_PROGRAM);
    }

    //A breakpoint is done by replacing an instruction by a "trap" instruction
    //When execution is continued this instruction still has to be executed
    //So we have to put back the original instruction, execute it, and insert
    //the breakpoint again

    //We can't simply use the BE and SE bits in the MSR without kernel patches
    //However, since they're optional, they might not be implemented on the Wii U
    //Patching the kernel is not really worth the effort in this case, so I'm
    //simply placing a trap at the next instruction

    //Special case, the twu instruction at the start
    uint32_t  entryPoint = 0x1005E040; //!TODO!!!
    //log_printf("crashContext.srr0 = %08X\n",crashContext.srr0);
    if (crashContext.srr0 == (uint32_t)entryPoint + 0x48) {
        WriteCode(crashContext.srr0, 0x60000000); //nop
    }

    if (stepState == STEP_STATE_RUNNING || stepState == STEP_STATE_STEPPING) {
        crashState = CRASH_STATE_BREAKPOINT;

        OSMessage message;
        message.message = (void *) SERVER_MESSAGE_PROGRAM;
        message.args[0] = (uint32_t)&crashContext;
        message.args[1] = sizeof(crashContext);
        OSSendMessage(&serverQueue, &message, OS_MESSAGE_FLAGS_BLOCKING);
        //DEBUG_FUNCTION_LINE("Added crash context into the serverqueue. Message %08X\n",message.message);

        OSReceiveMessage(&clientQueue, &message, OS_MESSAGE_FLAGS_BLOCKING);

        //DEBUG_FUNCTION_LINE("Client message %08X\n",(uint32_t)message.message);

        if (stepState == STEP_STATE_STEPPING) {
            bpManager->RestoreStepInstructions(stepSource);
        }

        breakpoint *bp = bpManager->GetBreakPoint(crashContext.srr0, BREAKPOINT_LIST_SIZE_USABLE);
        if (bp) {
            WriteCode(bp->address, bp->instruction);
        }

        //A conditional branch can end up at two places, depending on
        //wheter it's taken or not. To work around this, I'm using a
        //second, optional address. This is less work than writing code
        //that checks the condition registers.
        if ((uint32_t)message.message == CLIENT_MESSAGE_STEP_OVER) {
            bpManager->PredictStepAddresses(&crashContext, true);
        } else {
            bpManager->PredictStepAddresses(&crashContext, false);
        }

        bpManager->writeTRAPInstructionToSteps();

        stepSource = crashContext.srr0;

        if ((uint32_t)message.message == CLIENT_MESSAGE_CONTINUE) {
            //DEBUG_FUNCTION_LINE("New stepstate = STEP_STATE_CONTINUE\n");
            stepState = STEP_STATE_CONTINUE;
        } else {
            //DEBUG_FUNCTION_LINE("New stepstate = STEP_STATE_STEPPING\n");
            stepState = STEP_STATE_STEPPING;
        }
    } else if (stepState == STEP_STATE_CONTINUE) {
        //DEBUG_FUNCTION_LINE("Calling RestoreStepInstructions\n");
        bpManager->RestoreStepInstructions(stepSource);
        //DEBUG_FUNCTION_LINE("Setting stepState = STEP_STATE_RUNNING\n");
        stepState = STEP_STATE_RUNNING;
        //DEBUG_FUNCTION_LINE("Setting crashState = CRASH_STATE_NONE\n");
        crashState = CRASH_STATE_NONE;
    }
    OSLoadContext(&crashContext); //Resume execution
}

BOOL DSIHandler_Debug(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_DSI, (void *) HandleDSI,context);
}
BOOL ISIHandler_Debug(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_ISI, (void *) HandleISI,context);
}
BOOL ProgramHandler_Debug(OSContext *context) {
    return Diibugger::getInstance()->handle_crash(OS_EXCEPTION_TYPE_PROGRAM, (void *) HandleProgram,context);
}

void Diibugger::ReportCrash(uint32_t msg) {
    crashState = CRASH_STATE_UNRECOVERABLE;

    OSMessage message;
    message.message = (void*) msg;
    message.args[0] = (uint32_t)&crashContext;
    message.args[1] = sizeof(crashContext);
    OSSendMessage(&serverQueue, &message, OS_MESSAGE_FLAGS_BLOCKING);
    while (true) {
        OSSleepTicks(1000000);
    }
}
