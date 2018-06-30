#ifndef _DIIBUGGER_H_
#define _DIIBUGGER_H_

#include "DiibuggerTCPServer.hpp"
#include "BreakPointManager.hpp"
#include "common/diibugger_defs.h"
#include <coreinit/filesystem.h>

class Diibugger {
public:
    static Diibugger *getInstance() {
        if(!instance){
            instance = new Diibugger();
        }
        return instance;
    }

    static void destroyInstance() {
        if(instance) {
            delete instance;
            instance = NULL;
        }
    }

    void init();

    void startTCPServer() {
        if(tcpServer != NULL) {
            return;
        }
        tcpServer = new DiibuggerTCPServer(1559,28);
    }

    void stopTCPServer() {
        delete tcpServer;
        tcpServer = NULL;
    }

    FSClient* getFileClient() {
        return fileClient;
    }

    FSCmdBlock* getFileBlock() {
        return fileCMDBlock;
    }

    OSContext * getCrashContext();

    uint8_t getCrashType();

    void cmd_close();

    void cmd_read(uint32_t addr, uint32_t num);

    void cmd_write(uint32_t addr, uint32_t num);

    void cmd_write_code(uint32_t addr, uint32_t instr);

    void cmd_get_thread_list(char * buffer, uint32_t * buffer_size);

    void cmd_push_message(OSMessage * message);

    void cmd_get_messages(OSMessage * messages, uint32_t * count);

    void cmd_get_stack_trace(uint32_t* stacktrace, uint32_t* index);

    void cmd_poke_registers(uint8_t* gpr, uint8_t* fpr);

    void cmd_toggle_breakpoint(uint32_t address);

    void cmd_get_module_name(char * name, int32_t * length);

    void cmd_send_file_message(OSMessage * message);

    void HandleProgram();

    bool handle_crash(uint32_t type, void * handler, OSContext * context);

    void ReportCrash(uint32_t msg);

    void restoreInstructionForBreakPointsInRange(uint32_t addr, uint32_t range);

    void restoreTRAPForBreakPointsInRange(uint32_t addr, uint32_t range);

private:
    Diibugger();

    ~Diibugger();

    OSMessageQueue serverQueue;
    OSMessageQueue clientQueue;
    OSMessage serverMessages[MESSAGE_COUNT];
    OSMessage clientMessages[MESSAGE_COUNT];
    OSContext crashContext;
    uint32_t crashType;

    uint8_t crashState;

    uint8_t stepState;

    uint32_t stepSource;

    FSCmdBlock* fileCMDBlock = NULL;
    FSClient* fileClient = NULL;

    static Diibugger *instance;

    DiibuggerTCPServer* tcpServer = NULL;
    BreakPointManager* bpManager = NULL;
};

#endif //_DIIBUGGER_H_
