#ifndef _DIIBUGGER_TCPSERVER_H_
#define _DIIBUGGER_TCPSERVER_H_

#include <stdbool.h>
#include <utils/TCPServer.hpp>
#include <system/CThread.h>

class DiibuggerTCPServer: TCPServer {
public:
     DiibuggerTCPServer(int32_t port, int32_t priority);
    ~DiibuggerTCPServer();
private:
    virtual BOOL whileLoop();

    virtual BOOL acceptConnection();

    virtual void onConnectionClosed();
};

#endif //_DIIBUGGER_TCPSERVER_H_
