#include <wups.h>
#include <malloc.h>
#include <string.h>
#include <utils/logger.h>
#include <coreinit/debug.h>
#include "diibugger/DiibuggerTCPServer.hpp"
#include "diibugger/Diibugger.hpp"

WUPS_FS_ACCESS()

WUPS_ALLOW_KERNEL()

// Mandatory plugin information.
WUPS_PLUGIN_NAME("Diibugger");
WUPS_PLUGIN_DESCRIPTION("Description");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Kinnay, jam1garner, Maschell");
WUPS_PLUGIN_LICENSE("GPL");

// Called whenever an application was started.
ON_APPLICATION_START(args){
    if(!args.kernel_access){
        OSFatal("The diibugger plugin needs kernel access!");
    }

    socket_lib_init();
    log_init();

    DEBUG_FUNCTION_LINE("ON_APPLICATION_START\n");

    Diibugger::getInstance();
}

ON_APP_STATUS_CHANGED(status){
    if(status == WUPS_APP_STATUS_CLOSED){
        Diibugger::destroyInstance();
    }
}
