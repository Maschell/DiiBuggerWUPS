import sys, struct, socket, time, os, binascii, time
import disassemble
import threading

try:
    import tabulate
except ModuleNotFoundError:
    import pip
    print('Installing tabulate')
    pip.main(['install','tabulate'])
    import tabulate

class Message:
    DSI = 0
    ISI = 1
    Program = 2
    GetStat = 3
    OpenFile = 4
    ReadFile = 5
    CloseFile = 6
    SetPosFile = 7
    GetStatFile = 8

    Continue = 0
    Step = 1
    StepOver = 2

    def __init__(self, type, data, arg):
        self.type = type
        self.data = data
        self.arg = arg

class ExceptionState:

    exceptionNames = ["  DSI  ", "  ISI  ", "Program"]

    def __init__(self):
        self.filled = False

    def load(self, context, type):
        #Convert tuple to list to make it mutable
        self.filled = True
        self.gpr = list(struct.unpack_from(">32I", context, 8))
        self.cr, self.lr, self.ctr, self.xer = struct.unpack_from(">4I", context, 0x88)
        self.srr0, self.srr1, self.ex0, self.ex1 = struct.unpack_from(">4I", context, 0x98)
        self.fpr = list(struct.unpack_from(">32d", context, 0xB8))
        self.gqr = list(struct.unpack_from(">8I", context, 0x1BC))
        self.psf = list(struct.unpack_from(">32d", context, 0x1E0))

        self.exceptionName = self.exceptionNames[type]

    def isBreakPoint(self):
        return self.exceptionName == "Program" and self.srr1 & 0x20000

class Thread:

    cores = {
        1: "Core 0",
        2: "Core 1",
        4: "Core 2"
    }

    def __init__(self, data, offs=0):
        self.core = self.cores[struct.unpack_from(">I", data, offs)[0]]
        self.priority = struct.unpack_from(">I", data, offs + 4)[0]
        self.stackBase = struct.unpack_from(">I", data, offs + 8)[0]
        self.stackEnd = struct.unpack_from(">I", data, offs + 12)[0]
        self.entryPoint = struct.unpack_from(">I", data, offs + 16)[0]

        namelen = struct.unpack_from(">I", data, offs + 20)[0]
        self.name = data[offs + 24 : offs + 24 + namelen].decode("ascii")

class PyBugger:
    def __init__(self):
        super().__init__()
        self.connected = False
        self.breakPoints = []

        self.basePath = b""
        self.currentHandle = 0x12345678
        self.files = {}

        self.messageHandlers = {
            Message.DSI: self.handleException,
            Message.ISI: self.handleException,
            Message.Program: self.handleException,
            Message.GetStat: self.handleGetStat,
            Message.OpenFile: self.handleOpenFile,
            Message.ReadFile: self.handleReadFile,
            Message.CloseFile: self.handleCloseFile,
            Message.SetPosFile: self.handleSetPosFile,
            Message.GetStatFile: self.handleGetStatFile
        }

        self.silent = False

    def handleException(self, msg):
        exceptionState.load(msg.data, msg.type)
        if not self.silent:
            print('+----------------------------------+')
            print('|Exception type %s has occured|'%exceptionState.exceptionName)
            print('+----------------------------------+')

    def handleGetStat(self, msg):
        gamePath = msg.data.decode("ascii")
        path = os.path.join(self.basePath, gamePath.strip("/vol"))
        print("GetStat: %s" %gamePath)
        self.sendFileMessage(os.path.getsize(path))

    def handleOpenFile(self, msg):
        mode = struct.pack(">I", msg.arg).decode("ascii").strip("\x00") + "b"
        path = msg.data.decode("ascii")
        print("Open: %s" %path)

        f = open(os.path.join(self.basePath, path.strip("/vol")), mode)
        self.files[self.currentHandle] = f
        self.sendFileMessage(self.currentHandle)
        self.currentHandle += 1

    def handleReadFile(self, msg):
        print("Read")
        task = Task(blocking=False, cancelable=False)
        bufferAddr, size, count, handle = struct.unpack(">IIII", msg.data)

        data = self.files[handle].read(size * count)
        task.setInfo("Sending file", len(data))

        bytesSent = 0
        while bytesSent < len(data):
            length = min(len(data) - bytesSent, 0x8000)
            self.sendall(b"\x03")
            self.sendall(struct.pack(">II", bufferAddr, length))
            self.sendall(data[bytesSent : bytesSent + length])
            bufferAddr += length
            bytesSent += length
            task.update(bytesSent)
        self.sendFileMessage(bytesSent // size)
        task.end()

    def handleCloseFile(self, msg):
        print("Close")
        self.files.pop(msg.arg).close()
        self.sendFileMessage()

    def handleSetPosFile(self, msg):
        print("SetPos")
        handle, pos = struct.unpack(">II", msg.data)
        self.files[handle].seek(pos)
        self.sendFileMessage()

    def handleGetStatFile(self, msg):
        print("GetStatFile")
        f = self.files[msg.arg]
        pos = f.tell()
        f.seek(0, 2)
        size = f.tell()
        f.seek(pos)
        self.sendFileMessage(size)

    def connect(self, host):
        self.s = socket.socket()
        self.s.connect((host, 1559))
        self.connected = True
        self.breakPoints = []
        self.closeRequest = False

    def close(self):
        self.sendall(b"\x01")
        self.s.close()
        self.connected = False

    def updateMessages(self):
        self.sendall(b"\x07")
        count = struct.unpack(">I", self.recvall(4))[0]
        for i in range(count):
            type, ptr, length, arg = struct.unpack(">IIII", self.recvall(16))
            data = None
            if length:
                data = self.recvall(length)
            self.messageHandlers[type](Message(type, data, arg))

    def read(self, addr, num):
        self.sendall(b"\x02")
        self.sendall(struct.pack(">II", addr, num))
        data = self.recvall(num)
        return data

    def write(self, addr, data):
        self.sendall(b"\x03")
        self.sendall(struct.pack(">II", addr, len(data)))
        self.sendall(data)

    def writeCode(self, addr, instr):
        self.sendall(b"\x04")
        self.sendall(struct.pack(">II", addr, instr))

    def getThreadList(self):
        self.sendall(b"\x05")
        length = struct.unpack(">I", self.recvall(4))[0]
        data = self.recvall(length)

        offset = 0
        threads = []
        while offset < length:
            thread = Thread(data, offset)
            threads.append(thread)
            offset += 24 + len(thread.name)
        return threads

    def toggleBreakPoint(self, addr):
        if addr in self.breakPoints:
            self.breakPoints.remove(addr)
            print('Removed %08X'%addr)
        else:
            if len(self.breakPoints) >= 10:
                return
            self.breakPoints.append(addr)
            print('Added %08X'%addr)

        self.sendall(b"\x0A")
        self.sendall(struct.pack(">I", addr))

    def continueBreak(self): self.sendCrashMessage(Message.Continue)
    def stepBreak(self): self.sendCrashMessage(Message.Step)
    def stepOver(self): self.sendCrashMessage(Message.StepOver)

    def sendCrashMessage(self, message):
        self.sendMessage(message)

    def sendMessage(self, message, data0=0, data1=0, data2=0):
        self.sendall(b"\x06")
        self.sendall(struct.pack(">IIII", message, data0, data1, data2))

    def sendFileMessage(self, data0=0, data1=0, data2=0):
        self.sendall(b"\x0F")
        self.sendall(struct.pack(">IIII", 0, data0, data1, data2))

    def getStackTrace(self):
        self.sendall(b"\x08")
        count = struct.unpack(">I", self.recvall(4))[0]
        trace = struct.unpack(">%iI" %count, self.recvall(4 * count))
        return trace

    def pokeExceptionRegisters(self):
        self.sendall(b"\x09")
        data = struct.pack(">32I32d", *exceptionState.gpr, *exceptionState.fpr)
        self.sendall(data)

    def readDirectory(self, path):
        self.sendall(b"\x0B")
        self.sendall(struct.pack(">I", len(path)))
        self.sendall(path.encode("ascii"))

        entries = []
        namelen = struct.unpack(">I", self.recvall(4))[0]
        while namelen != 0:
            flags = struct.unpack(">I", self.recvall(4))[0]

            size = -1
            if not flags & 0x80000000:
                size = struct.unpack(">I", self.recvall(4))[0]

            name = self.recvall(namelen).decode("ascii")
            entries.append(DirEntry(flags, size, name))

            namelen = struct.unpack(">I", self.recvall(4))[0]
        return entries

    def dumpFile(self, gamePath, outPath, task):
        if task.canceled:
            return

        self.sendall(b"\x0C")
        self.sendall(struct.pack(">I", len(gamePath)))
        self.sendall(gamePath.encode("ascii"))

        length = struct.unpack(">I", self.recvall(4))[0]
        task.setInfo("Dumping %s" %gamePath, length)

        with open(outPath, "wb") as f:
            bytesDumped = 0
            while bytesDumped < length:
                data = self.s.recv(length - bytesDumped)
                f.write(data)
                bytesDumped += len(data)
                task.update(bytesDumped)

    def search(self, startAddress, endAddress, value):
        length = int((endAddress - startAddress) / 4)
        self.sendall(b"\x11")
        self.sendall(struct.pack(">L", startAddress))
        self.sendall(struct.pack(">L", length))
        self.sendall(struct.pack(">L", value))
        location = struct.unpack(">L", self.recvall(4))
        return location

    def loadMods(self, fileBytes):
        self.sendall(b"\x12")
        self.sendall(struct.pack(">L", len(fileBytes)))
        self.sendall(fileBytes)

    def getModuleName(self):
        self.sendall(b"\x0D")
        length = struct.unpack(">I", self.recvall(4))[0]
        return self.recvall(length).decode("ascii") + ".rpx"

    def setPatchFiles(self, fileList, basePath):
        self.basePath = basePath
        self.sendall(b"\x0E")

        fileBuffer = struct.pack(">I", len(fileList))
        for path in fileList:
            fileBuffer += struct.pack(">H", len(path))
            fileBuffer += path.encode("ascii")

        self.sendall(struct.pack(">I", len(fileBuffer)))
        self.sendall(fileBuffer)

    def clearPatchFiles(self):
        self.sendall(b"\x10")

    def sendall(self, data):
        try:
            self.s.sendall(data)
        except socket.error:
            self.connected = False

    def recvall(self, num):
        try:
            data = b""
            while len(data) < num:
                data += self.s.recv(num - len(data))
        except socket.error:
            self.connected = False
            return b"\x00" * num
        return data

exceptionState = ExceptionState()
bugger = PyBugger()

while True:
    try:
        userInput = input("> ").strip()
        splitCmd = userInput.split(" ")
        cmd = splitCmd[0].lower()
    except KeyboardInterrupt:
        print("") #Just to prevent issues with early disconnects
    except Exception as e:
        print(e)
    try:
        if cmd == "help":
            print("                        Command list:")
            print("-----------------------------------------------------------------")
            print("exit")
            print("     Closes connection and exits")
            print("connect [ip]")
            print("     Connects to diibugger at a specified ip address if no ip is")
            print("     given, it will use one from ip.txt in the active directory")
            print("close")
            print("     Closes connection and waits")
            print("eval [statement]")
            print("     Evalutes a python statement")
            print("cmd [command]")
            print("     Evaluates a system command")
            print("read [address] [length]")
            print("     Prints out certain bytes in hex and ascii to the console")
            print("     aliases: preview, r")
            print("dump [address] [length] [filename]")
            print("     Dumps a certain area of memory to file")
            print("search [startAddress] [endAddress] [value]")
            print("     Search a range of memory for a uin32 value")
            print("word [address] [value]")
            print("     Writes a uint32 to an address")
            print("     aliases: ww, writeword, int")
            print("float [address] [value]")
            print("     Writes a float to an address")
            print("hex [address] [hex]")
            print("     Writes bytes to a specified address")
            print("     aliases: bytes, w, writebytes")
            print("ppc [address] [length]")
            print("     Print disassembled code from the specified region")
            print("threads")
            print("     List thread info")
            print("stack")
            print("     Prints a stack trace")
            print("     aliases: stacktrace, trace")
            print("stackdump [stackNum] [filename]")
            print("     Dumps entire thread's stack to file (Note: get stacknum from \"threads\")")
            print("     aliases: sd")
            print("registers (or reg)")
            print("     Print registers")
            print("update (or u)")
            print("     Checks for exceptions")
            print("     this is run after every other command as well")
            print("breakpoints (or bps)")
            print("     List breakpoints")
            print("breapoint [address] (or bp)")
            print("     Toggle breakpoint")
            print("continue (or c)")
            print("     Continue past breakpoint")
            print("step (or s)")
            print("     Step from breakpoint to next line")
        elif cmd == "exit":
            try:
                if bugger.connected:
                    for i in bugger.breakPoints:
                        bugger.toggleBreakPoint(i)
                    bugger.close()
            finally:
                break
        elif cmd == "connect":
            bugger.connect(splitCmd[1])
            if bugger.connected:
                print("Successfully connected\nCurrent title: "+bugger.getModuleName())
            else:
                print("Failed to connect")
        elif cmd == "cmd":
            os.system(userInput[userInput.find(" ")+1:])
        elif cmd == "eval":
            print(eval(userInput[userInput.find(" ")+1:]))
        elif not bugger.connected:
            print("Diibugger server not connected, use 'connect [ip]' to connect")			
        elif cmd == "close":
            if bugger.connected:
                for i in bugger.breakPoints:
                    bugger.toggleBreakPoint(i)
                bugger.close()
        elif cmd == "read" or cmd == "preview" or cmd == "r":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            length = 0x30
            if len(splitCmd) >= 3:
                length = int(splitCmd[2], 16)
            start = address - (address % 0x10)
            end = address + length
            if end % 0x10 != 0:
                end += (0x10 - ((address + length) % 0x10))

            readBytes = bugger.read(start, end - start)
            print("         | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | 012345678ABCDEF");
            print("         |-------------------------------------------------|----------------")
            for i in range(int(len(readBytes) / 0x10)):
                thisString = readBytes[i*0x10:(i+1)*0x10].decode('latin1')
                for char in ['\n', '\t', '\r', chr(0)]:
                    thisString = thisString.replace(char, '.')
                print("%08X | %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X | %s " % tuple([i*0x10 + start] + list(readBytes[i*0x10:(i+1)*0x10]) + [thisString]))
        elif cmd == "dump":
            if len(splitCmd) <= 3:
                print("Not enough arguments\n\tdump [address] [length] [file]")
            else:
                addressStr = splitCmd[1]
                if addressStr[0] == 'r':
                    address = exceptionState.gpr[int(addressStr[1:])]
                else:
                    address = int(addressStr, 16)
                length = int(splitCmd[2], 16)
                filename = ""
                for i in splitCmd[3:]:
                    filename += i + " "
                filename = filename.strip().strip('"').strip("'")
                with open(filename, 'wb') as f:
                    f.write(bugger.read(address, length))
        elif cmd == "search":
            startAddress = int(splitCmd[1],16)
            endAddress = int(splitCmd[2], 16)
            value = int(splitCmd[3], 16)
            address = bugger.search(startAddress, endAddress, value)
            if address == 0:
                print("Not found in range")
            else:
                print("Located at %08X" % address)
        elif cmd == "word" or cmd == "ww" or cmd == "writeword" or cmd == "int":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            value = int(splitCmd[2], 16)
            bugger.write(address, struct.pack(">L", value))
        elif cmd == "float":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            value = float(splitCmd[2])
            bugger.write(address, struct.pack(">f", value))
        elif cmd == "hex" or cmd == "bytes" or cmd == "w" or cmd == "writebytes":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            bugger.write(address, binascii.unhexlify(splitCmd[2]))
        elif cmd == "ppc":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            length = 4
            if len(splitCmd) > 2:
                length = int(splitCmd[2], 16)
            ppcBytes = bugger.read(address, length)
            for i in range(int(len(ppcBytes) / 4)):
                value = struct.unpack('>L',ppcBytes[i*4:i*4 + 4])[0]
                addr = address + (i*4)
                instr = disassemble.disassemble(value, addr)
                print("%08X:  %08X  %s" %(addr, value, instr))
        elif cmd == "stack" or cmd == "stacktrace" or cmd == "trace":
            stackTrace = bugger.getStackTrace()
            print('')
            for address in stackTrace:
                print("%X" % address)
        elif cmd == "registers" or cmd == "regs":
            if exceptionState.filled:
                for i in range(4):
                    for j in range(8):
                        print("r%2i: %08X" % (i*8 + j, exceptionState.gpr[i*8 + j]), end=' ')
                    print('')
                print('CR:%08X LR:%08X CTR:%08X XER:%08X EX0:%08X EX1:%08X SRR0:%08X SRR1:%08X' % (exceptionState.cr, exceptionState.lr, exceptionState.ctr, exceptionState.xer, exceptionState.ex0, exceptionState.ex1, exceptionState.srr0, exceptionState.srr1))
            else:
                print("No exception state")
        elif cmd == "update" or cmd == "u":
            pass
        elif cmd == "threads":
            threads = bugger.getThreadList()
            table = []
            for i,thread in enumerate(threads):
                table.append([i,thread.name,thread.core,'%08X' % thread.stackBase,'%08X' % thread.stackEnd,'%08X' % thread.entryPoint,thread.priority])
            print(tabulate.tabulate(table,["Thread","Name","Core","Stack Start","Stack End","Entrypoint","Priority"]))
        elif cmd == "stackdump" or cmd == "sd":
            stackNum = int(splitCmd[1],0)
            filename = ""
            for i in splitCmd[2:]:
                filename += i + " "
            filename = filename.strip().strip('"').strip("'")
            thread = bugger.getThreadList()[stackNum]
            with open(filename, 'wb') as f:
                f.write(bugger.read(thread.stackEnd, thread.stackBase - thread.stackEnd))
        elif cmd == "breakpoints" or cmd == "bps":
            for bp in bugger.breakPoints:
                print('%08X'%bp)
        elif cmd == "breakpoint" or cmd == "bp":
            addressStr = splitCmd[1]
            if addressStr[0] == 'r':
                address = exceptionState.gpr[int(addressStr[1:])]
            else:
                address = int(addressStr, 16)
            bugger.toggleBreakPoint(address)
        elif cmd == "continue" or cmd == "c":
            bugger.continueBreak()
        elif cmd == "step" or cmd == "s":
            bugger.stepBreak()
            time.sleep(0.2)
            bugger.silent = True
            bugger.updateMessages()
            bugger.silent = False
            print('Stepped to %08X' % exceptionState.srr0)
        elif cmd == "stepover" or cmd == "so":
            bugger.stepOver()
        elif cmd == "load":
            filename = userInput[5:]
            with open(filename, 'rb') as f:
                fileBytes = f.read()
                if fileBytes[:4] == b'MODS':
                    bugger.loadMods(fileBytes)
                else:
                    print("Not a valid mods file")
        else:
            print("Invalid command")
        if bugger.connected:
            bugger.updateMessages()
    except Exception as e:
        print(e)
