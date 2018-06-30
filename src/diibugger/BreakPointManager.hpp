#ifndef _BREAKPOINT_MANAGER_H_
#define _BREAKPOINT_MANAGER_H_

#include "common/diibugger_defs.h"
#include <coreinit/context.h>

class BreakPointManager {
public:
    BreakPointManager();

    ~BreakPointManager(){

    }

    /**
        \brief Returns a pointer to a breakpoint element for a given address.

        \param addr: Address of the breakpoint.
        \param num: end of iteration

        \return Returns the address of the breakpoint data if a breakpoint was set at the given address.
                Return NULL if no breakpoint was found at the given address.
    **/
    breakpoint * GetBreakPoint(uint32_t addr, uint32_t num);

    /**
        \brief Gets a pointer to the next free breakpoint element.

        \return The address of the free breakpoint element that should be filled with information.
                Returns NULL if there is no more space for another breakpoint.
    **/
    breakpoint * GetFreeBreakPoint();

    /**
        \brief  Checks if a address with an given range contains any breakpoints.
                This functions is supposed to be used more than one time.
                After the initial call pass the previous result until it returns NULL.

        \param addr:    start address of the area that should be checked
        \param range:   size of the range.
        \param prev:    the previous breakpoint

        \return Return a pointer to a breakpoint element that is in the given range.
                Return NULL if there is no (more) breakpoint in the given area.
    **/

    breakpoint * GetBreakPointInRange(uint32_t addr, uint32_t range, breakpoint *prev);

    /**
        \brief  Restores the instruction for all breakpoints in a given range.
                The breakpoint slots will still be occupied, the address will NOT be set to 0.
                Use restoreTRAPForBreakPointsInRange with the same arguments to revert it.

        \param addr:    start address of the area that should be checked
        \param range:   size of the range.
    **/
    void restoreInstructionForBreakPointsInRange(uint32_t addr, uint32_t range);

    /**
        \brief  Sets the instruction of all breakpoints in the given range to the TRAP instruction.
                The breakpoint slots will still be occupied, the address will NOT be set to 0.
                This is supposed to revert the changes of restoreInstructionForBreakPointsInRange.

        \param addr:    start address of the area that should be checked
        \param range:   size of the range.
    **/
    void restoreTRAPForBreakPointsInRange(uint32_t addr, uint32_t range);

    /**
        \brief Gets the instruction for a given address with the breakpoints in mind.
        It's ignoring TRAP instruction, and gets the instruction behind the TRAP

        \return Returns the "real" instruction of a given address.
                If a breakpoint is currently set a the given address, the original instruction instead
                of the TRAP instruction will be returned.
    **/
    uint32_t GetInstruction(uint32_t address);

    /**
        \brief Sets the internal step-breakpoints. TODO!!!!!!!!!!!!!!!!!!

        \param crashContext The current crash context
        \param stepOver: Set this to true to step over the next instruction, false will step in.
    **/
    void PredictStepAddresses(OSContext * crashContext, bool stepOver);

    /**
        \brief Removes all breakpoints.
    **/
    void RemoveAllBreakPoints();


    /**
        \brief Writes TRAP instructions to handle steps
    **/
    void writeTRAPInstructionToSteps();

    /**
        \brief Restores the step instructions
        \param the step source
    **/
    void RestoreStepInstructions(uint32_t stepSource);

private:
    breakpoint breakpoints[BREAKPOINT_LIST_SIZE];
};

#endif //_BREAKPOINT_MANAGER_H_
