#include "BreakPointManager.hpp"
#include "utils.h"
#include <stdio.h>
#include <string.h>

BreakPointManager::BreakPointManager() {
    memset(breakpoints,0,sizeof(breakpoints));
}

breakpoint * BreakPointManager::GetBreakPoint(uint32_t addr, uint32_t num) {
    for (int i = 0; i < num; i++) {
        if (this->breakpoints[i].address == addr) {
            return &this->breakpoints[i];
        }
    }
    return NULL;
}


breakpoint * BreakPointManager::GetFreeBreakPoint() {
    breakpoint *bplist = this->breakpoints;
    for (int i = 0; i < BREAKPOINT_LIST_SIZE_USABLE; i++) {
        if (bplist[i].address == 0) {
            return &bplist[i];
        }
    }
    return 0;
}

breakpoint * BreakPointManager::GetBreakPointInRange(uint32_t addr, uint32_t range, breakpoint *prev) {
    breakpoint *bplist = this->breakpoints;

    int start = 0;
    if (prev) {
        start = (prev - bplist) + 1;
    }

    for (int i = start; i < BREAKPOINT_LIST_SIZE; i++) {
        if (bplist[i].address >= addr && bplist[i].address < addr + range) {
            return &bplist[i];
        }
    }
    return 0;
}

void BreakPointManager::restoreInstructionForBreakPointsInRange(uint32_t addr, uint32_t range) {
    breakpoint *bp = GetBreakPointInRange(addr, range, 0);
    while (bp) {
        WriteCode(bp->address, bp->instruction);
        bp = GetBreakPointInRange(addr, range, bp);
    }
}
void BreakPointManager::restoreTRAPForBreakPointsInRange(uint32_t addr, uint32_t range) {
    breakpoint * bp = GetBreakPointInRange(addr, range, 0);
    while (bp) {
        WriteCode(bp->address, TRAP);
        bp = GetBreakPointInRange(addr, range, bp);
    }
}

uint32_t BreakPointManager::GetInstruction(uint32_t address) {
    breakpoint *bp = GetBreakPoint(address, BREAKPOINT_LIST_SIZE);
    if (bp) {
        return bp->instruction;
    }
    return *(uint32_t *)address;
}


void BreakPointManager::PredictStepAddresses(OSContext * crashContext, bool stepOver) {
    uint32_t currentAddr = crashContext->srr0;
    uint32_t instruction = GetInstruction(currentAddr);

    breakpoint *step1 = &breakpoints[STEP1];
    breakpoint *step2 = &breakpoints[STEP2];
    step1->address = currentAddr + 4;
    step2->address = 0;

    uint8_t opcode = instruction >> 26;
    if (opcode == 19) {
        uint16_t XO = (instruction >> 1) & 0x3FF;
        bool LK = instruction & 1;
        if (!LK || !stepOver) {
            if (XO ==  16) step2->address = crashContext->lr; //bclr
            if (XO == 528) step2->address = crashContext->ctr; //bcctr
        }
    }

    else if (opcode == 18) { //b
        bool AA = instruction & 2;
        bool LK = instruction & 1;
        uint32_t LI = instruction & 0x3FFFFFC;
        if (!LK || !stepOver) {
            if (AA) step1->address = LI;
            else {
                if (LI & 0x2000000) LI -= 0x4000000;
                step1->address = currentAddr + LI;
            }
        }
    }

    else if (opcode == 16) { //bc
        bool AA = instruction & 2;
        bool LK = instruction & 1;
        uint32_t BD = instruction & 0xFFFC;
        if (!LK || !stepOver) {
            if (AA) step2->address = BD;
            else {
                if (BD & 0x8000) BD -= 0x10000;
                step2->address = currentAddr + BD;
            }
        }
    }
}

void BreakPointManager::RemoveAllBreakPoints() {
    for (int i = 0; i < BREAKPOINT_LIST_SIZE_USABLE; i++) {
        if (breakpoints[i].address) {
            WriteCode(breakpoints[i].address, breakpoints[i].instruction);
            breakpoints[i].address = 0;
            breakpoints[i].instruction = 0;
        }
    }
}

void BreakPointManager::writeTRAPInstructionToSteps() {
    breakpoints[STEP1].instruction = *(uint32_t *)(breakpoints[STEP1].address);
    WriteCode(breakpoints[STEP1].address, TRAP);
    if (breakpoints[STEP2].address) {
        breakpoints[STEP2].instruction = *(uint32_t *)(breakpoints[STEP2].address);
        WriteCode(breakpoints[STEP2].address, TRAP);
    }
}

void BreakPointManager::RestoreStepInstructions(uint32_t stepSource) {
    //Write back the instructions that were replaced for the step
    WriteCode(breakpoints[STEP1].address, breakpoints[STEP1].instruction);
    breakpoints[STEP1].address = 0;
    breakpoints[STEP1].instruction = 0;
    if (breakpoints[STEP2].address) {
        WriteCode(breakpoints[STEP2].address, breakpoints[STEP2].instruction);
        breakpoints[STEP2].address = 0;
        breakpoints[STEP2].instruction = 0;
    }

    breakpoint *bp = GetBreakPoint(stepSource, BREAKPOINT_LIST_SIZE_USABLE);
    if (bp) {
        WriteCode(bp->address, TRAP);
    }
}
