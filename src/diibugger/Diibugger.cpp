#include "Diibugger.hpp"
#include "handler.h"
#include "utils.h"
#include <coreinit/exception.h>
#include <coreinit/dynload.h>

Diibugger * Diibugger::instance = NULL;

Diibugger::Diibugger() {
    FSInit();
    DEBUG_FUNCTION_LINE("FSInit() done\n");

    this->fileCMDBlock = (FSCmdBlock*) memalign(0x40,sizeof(FSCmdBlock));
    this->fileClient = (FSClient*) memalign(0x40,sizeof(FSClient));
    FSInitCmdBlock(this->fileCMDBlock);
    FSAddClient(this->fileClient, -1);
    DEBUG_FUNCTION_LINE("FS Stuff done\n");

    OSInitMessageQueue(&serverQueue, serverMessages, MESSAGE_COUNT);
    OSInitMessageQueue(&clientQueue, clientMessages, MESSAGE_COUNT);

    DEBUG_FUNCTION_LINE("Setting the ExceptionCallbacks\n");
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Fatal);

    memset(&crashContext,0,sizeof(crashContext));

    stepState = STEP_STATE_RUNNING;

    bpManager = new BreakPointManager();

    startTCPServer();
}

Diibugger::~Diibugger() {
    stopTCPServer();
    if(this->fileCMDBlock != NULL) {
        free(this->fileCMDBlock);
        this->fileCMDBlock = NULL;
    }

    if(this->fileClient != NULL) {
        FSDelClient(this->fileClient,-1);
        free(this->fileClient);
        this->fileClient = NULL;
    }
}

OSContext * Diibugger::getCrashContext() {
    return &(this->crashContext);
}

uint8_t Diibugger::getCrashType() {
    return crashType;
}

void Diibugger::cmd_close() {
    //Remove all breakpoints
    bpManager->RemoveAllBreakPoints();

    //Make sure we're not stuck in an exception when the
    //debugger disconnects without handling it
    if (crashState == CRASH_STATE_BREAKPOINT) {
        OSMessage message;
        message.message = CLIENT_MESSAGE_CONTINUE;
        OSSendMessage(&clientQueue, &message, OS_MESSAGE_FLAGS_BLOCKING);
        //Wait until execution is resumed before installing the OSFatal crash handler
        while (crashState != CRASH_STATE_NONE) {
            OSSleepTicks(100000);
        }
    }

    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Fatal);
}

void Diibugger::cmd_read(uint32_t addr, uint32_t num) {
    //TODO
}

void Diibugger::cmd_write(uint32_t addr, uint32_t num) {
    //TODO
}

void Diibugger::cmd_write_code(uint32_t addr, uint32_t instr) {
    //Make sure we don't overwrite breakpoint traps
    breakpoint *bp = bpManager->GetBreakPoint(addr, BREAKPOINT_LIST_SIZE);
    if (bp) {
        bp->instruction = instr;
    } else {
        WriteCode(addr, instr);
    }
}

void Diibugger::cmd_get_thread_list(char * buffer, uint32_t * buffer_size) {
    OSThread *currentThread = OSGetCurrentThread();
    OSThread *iterThread = currentThread;
    OSThreadLink threadLink;
    do { //Loop previous threads
        *buffer_size += PushThread(buffer, *buffer_size, iterThread);
        OSGetActiveThreadLink(iterThread, &threadLink);
        iterThread = threadLink.prev;
    } while (iterThread);

    OSGetActiveThreadLink(currentThread, &threadLink);
    iterThread = threadLink.next;
    while (iterThread) { //Loop next threads
        *buffer_size += PushThread(buffer, *buffer_size, iterThread);
        OSGetActiveThreadLink(iterThread, &threadLink);
        iterThread = threadLink.next;
    }
}

void Diibugger::cmd_push_message(OSMessage * message) {
    OSSendMessage(&clientQueue, message, OS_MESSAGE_FLAGS_BLOCKING);
}

void Diibugger::cmd_get_messages(OSMessage * messages, uint32_t * count) {
    OSMessage message;

    while (OSReceiveMessage(&serverQueue, &message, OS_MESSAGE_FLAGS_NONE)) {
        memcpy(&messages[*count],&message,sizeof(OSMessage));
        (*count)++;
    }
}

void Diibugger::cmd_get_stack_trace(uint32_t* stacktrace, uint32_t* index) {
    uint32_t sp = crashContext.gpr[1];
    while (isValidStackPtr(sp)) {
        sp = *(uint32_t *)sp;
        if (!isValidStackPtr(sp)) {
            break;
        }

        stacktrace[*index] = *(uint32_t *)(sp + 4);
        (*index)++;
    }
}

void Diibugger::cmd_poke_registers(uint8_t* gpr, uint8_t* fpr) {
    memcpy((uint8_t*)&crashContext.gpr,gpr,4*32);
    memcpy((uint8_t*)&crashContext.fpr,fpr,8*32);
}

void Diibugger::cmd_toggle_breakpoint(uint32_t address) {
    breakpoint *bp = bpManager->GetBreakPoint(address, BREAKPOINT_LIST_SIZE_USABLE);
    if (bp) {
        WriteCode(address, bp->instruction);
        bp->address = 0;
        bp->instruction = 0;
    } else {
        bp = bpManager->GetFreeBreakPoint();
        bp->address = address;
        bp->instruction = *(uint32_t *)address;
        WriteCode(address, TRAP);
    }
}

void Diibugger::cmd_get_module_name(char * name, int32_t * length) {
    OSDynLoad_GetModuleName((OSDynLoad_Module) -1, name, length);
}

void Diibugger::cmd_send_file_message(OSMessage * message) {
    //OSSendMessage(&fileQueue, message, OS_MESSAGE_BLOCK);
}

void Diibugger::restoreInstructionForBreakPointsInRange(uint32_t addr, uint32_t range){
    bpManager->restoreInstructionForBreakPointsInRange(addr,range);
}

void Diibugger::restoreTRAPForBreakPointsInRange(uint32_t addr, uint32_t range){
    bpManager->restoreTRAPForBreakPointsInRange(addr,range);
}
