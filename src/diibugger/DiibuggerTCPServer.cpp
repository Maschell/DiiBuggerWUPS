#include "DiibuggerTCPServer.hpp"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <coreinit/exception.h>
#include <network/net.h>
#include <utils/logger.h>
#include "utils.h"
#include "handler.h"
#include "Diibugger.hpp"

DiibuggerTCPServer::DiibuggerTCPServer(int32_t port,int32_t priority):TCPServer(port,priority) {
    DEBUG_FUNCTION_LINE("Init DiibuggerTCPServer\n");
}

DiibuggerTCPServer::~DiibuggerTCPServer() {

}

BOOL DiibuggerTCPServer::acceptConnection() {
    DEBUG_FUNCTION_LINE("Set debug exceptions\n");
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Debug);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Debug);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Debug);
    DEBUG_FUNCTION_LINE("Let's accept the connection\n");

    return true;
}

void DiibuggerTCPServer::onConnectionClosed() {
    DEBUG_FUNCTION_LINE("Connection closed\n");
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, DSIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, ISIHandler_Fatal);
    OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, ProgramHandler_Fatal);
}

BOOL DiibuggerTCPServer::whileLoop() {
    int32_t cmd;
    volatile int32_t clientfd = getClientFD();
    Diibugger* diibugger = Diibugger::getInstance();
    while (true) {
        if(shouldExit()) {
            break;
        }

        cmd = checkbyte(clientfd);
        if (cmd < 0) {
            if(socketlasterr() != 6) {
                return false;
            }
            OSSleepTicks(OSMicrosecondsToTicks(1000));
            continue;
        }

        if (cmd == 1) { //Close
            DEBUG_FUNCTION_LINE("Close!\n");
            diibugger->cmd_close();
            break;
        } else if (cmd == 2) { //Read
            DEBUG_FUNCTION_LINE("Read!\n");
            uint32_t addr = recvword(clientfd);
            uint32_t num = recvword(clientfd);

            // Remove the TRAP instructions.
            diibugger->restoreInstructionForBreakPointsInRange(addr,num);

            sendwait(clientfd, (void *)addr, num);

            // Restore the TRAP instructions.
            diibugger->restoreTRAPForBreakPointsInRange(addr,num);

        } else if (cmd == 3) { //Write
            DEBUG_FUNCTION_LINE("Write!\n");
            uint32_t addr = recvword(clientfd);
            uint32_t num = recvword(clientfd);

            //diibugger->cmd_write(addr,num);
            recvwait(clientfd, (uint8_t *)addr, num);
        } else if (cmd == 4) { //Write code
            DEBUG_FUNCTION_LINE("Write code!\n");
            uint32_t addr = recvword(clientfd);
            uint32_t instr = recvword(clientfd);

            diibugger->cmd_write_code(addr,instr);
        } else if (cmd == 5) { //Get thread list
            DEBUG_FUNCTION_LINE("Get thread list!\n");
            //Might need OSDisableInterrupts here?
            char buffer[0x1000]; //This should be enough
            uint32_t buffer_size = 0;

            diibugger->cmd_get_thread_list(buffer, &buffer_size);

            sendwait(clientfd, &buffer_size, 4);
            sendwait(clientfd, buffer, buffer_size);
        } else if (cmd == 6) { //Push message
            DEBUG_FUNCTION_LINE("Push message \n");
            OSMessage message;
            recvwait(clientfd, (uint8_t*)&message, sizeof(OSMessage));
            diibugger->cmd_push_message(&message);
        } else if (cmd == 7) { //Get messages
            DEBUG_FUNCTION_LINE("Get messages!\n");
            OSMessage messages[10];
            uint32_t count = 0;

            diibugger->cmd_get_messages(messages,&count);

            sendwait(clientfd, &count, 4);
            for (uint32_t i = 0; i < count; i++) {
                sendwait(clientfd, &messages[i], sizeof(OSMessage));
                if (messages[i].args[0]) {
                    sendwait(clientfd, (void *)messages[i].args[0], messages[i].args[1]);
                }
            }
        } else if (cmd == 8) { //Get stack trace
            DEBUG_FUNCTION_LINE("Get stack trace\n");
            uint32_t index = 0;
            uint32_t stackTrace[30];

            diibugger->cmd_get_stack_trace(stackTrace, &index);

            sendwait(clientfd, &index, 4);
            sendwait(clientfd, stackTrace, index * 4);
        } else if (cmd == 9) { //Poke registers
            DEBUG_FUNCTION_LINE("Poke registers!\n");
            uint8_t gpr[4*32];
            uint8_t fpr[8*32];

            recvwait(clientfd, (uint8_t*)gpr, 4 * 32);
            recvwait(clientfd, (uint8_t*)fpr, 8 * 32);

            diibugger->cmd_poke_registers(gpr,fpr);
        } else if (cmd == 10) { //Toggle breakpoint
            DEBUG_FUNCTION_LINE("Toggle breakpoint!\n");
            uint32_t address = recvword(clientfd);
            diibugger->cmd_toggle_breakpoint(address);
        } else if (cmd == 11) { //Read directory
            DEBUG_FUNCTION_LINE("Read directory!\n");
            char path[640] = {0}; //512 + 128
            uint32_t pathlen = recvword(clientfd);
            if (pathlen < 640) {
                recvwait(clientfd, (uint8_t*)path, pathlen);
                FSStatus error;
                FSDirectoryHandle handle;
                FSDirectoryEntry entry;
                error = FSOpenDir(diibugger->getFileClient(), diibugger->getFileBlock(), path, &handle, -1);

                while (FSReadDir(diibugger->getFileClient(), diibugger->getFileBlock(), handle, &entry, -1) == 0) {
                    int32_t namelen = strlen(entry.name);
                    sendwait(clientfd, &namelen, 4);
                    sendwait(clientfd, &entry.info.flags, 4);
                    if (!(entry.info.flags & 0x80000000)) {
                        sendwait(clientfd, &entry.info.size, 4);
                    }
                    sendwait(clientfd, &entry.name, namelen);
                }

                error = FSCloseDir(diibugger->getFileClient(), diibugger->getFileBlock(), handle, -1);
            }
            int32_t terminator = 0;
            sendwait(clientfd, &terminator, 4);
        } else if (cmd == 12) { //Dump file
            DEBUG_FUNCTION_LINE("Dump file!\n");
            char path[640] = {0};
            uint32_t pathlen = recvword(clientfd);
            if (pathlen < 640) {
                recvwait(clientfd, (uint8_t*)path, pathlen);

                FSStatus error;
                FSFileHandle handle;
                error = FSOpenFile(diibugger->getFileClient(), diibugger->getFileBlock(), path, "r", &handle, -1);
                //CHECK_ERROR(error, "FSOpenFile");

                FSStat stat;
                error = FSGetStatFile(diibugger->getFileClient(), diibugger->getFileBlock(), handle, &stat, -1);
                //CHECK_ERROR(error, "FSGetStatFile");

                uint32_t size = stat.size;
                sendwait(clientfd, &stat.size, 4);

                uint8_t *buffer = (uint8_t *)memalign(0x40,0x20000);

                uint32_t read = 0;
                while (read < size) {
                    FSStatus num = FSReadFile(diibugger->getFileClient(), diibugger->getFileBlock(), buffer, 1, 0x20000, handle, 0, -1);
                    //CHECK_ERROR(num, "FSReadFile");
                    read += num;

                    sendwait(clientfd, buffer, num);
                }

                error = FSCloseFile(diibugger->getFileClient(), diibugger->getFileBlock(), handle, -1);
                //CHECK_ERROR(error, "FSCloseFile");

                free(buffer);
            } else {
                DEBUG_FUNCTION_LINE("pathlen >= 640");
            }
        } else if (cmd == 13) { //Get module name
            DEBUG_FUNCTION_LINE("Get module name!\n");
            char name[100] = {0};
            int32_t length = 100;

            diibugger->cmd_get_module_name(name, &length);

            length = strlen(name);
            sendwait(clientfd, &length, 4);
            sendwait(clientfd, name, length);
        } else if (cmd == 14) { //Set patch files
            //We don't want this anymore...
            diibugger->cmd_close();
        } else if (cmd == 15) { //Send file message
            //We don't want this anymore...
            diibugger->cmd_close();
        } else if (cmd == 16) { //Clear patch files
            //We don't want this anymore...
            diibugger->cmd_close();
        } else if (cmd == 17) { //Get persistent id
            //uint32_t persistentId = 1337;//diibugger->GetPersistentId();
            //sendwait(clientfd, &persistentId, 4);
            diibugger->cmd_close();
        } else {
            return false;
        }
    }
    DEBUG_FUNCTION_LINE("End of whileLoop!\n");
    return true;
}

