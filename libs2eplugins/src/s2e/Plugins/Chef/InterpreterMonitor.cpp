///
/// Copyright (C) 2023, Petr Stastny
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy
/// of this software and associated documentation files (the "Software"), to deal
/// in the Software without restriction, including without limitation the rights
/// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
/// copies of the Software, and to permit persons to whom the Software is
/// furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in all
/// copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
/// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
/// SOFTWARE.
///

//#include <s2e/S2E.h>
#include "../../../../../libs2ecore/include/s2e/S2E.h"
//#include <s2e/ConfigFile.h>
#include "../../../../../libs2ecore/include/s2e/ConfigFile.h"
#include <s2e/Utils.h>

#include "../../../../../libs2ecore/include/s2e/S2EExecutionState.h"
#include "../../../../../libs2ecore/include/s2e/CorePlugin.h"
#include "../../../../../libs2ecore/include/s2e/S2ETranslationBlock.h"

#include "InterpreterMonitor.h"

namespace s2e {
namespace plugins {

namespace {

//
// This class can optionally be used to store per-state plugin data.
//
// Use it as follows:
// void InterpreterMonitor::onEvent(S2EExecutionState *state, ...) {
//     DECLARE_PLUGINSTATE(InterpreterMonitorState, state);
//     plgState->...
// }
//
class InterpreterMonitorState: public PluginState {
    // Declare any methods and fields you need here
    unsigned long instruction_count;
public:
    InterpreterMonitorState() : instruction_count(0) {}

public:
    static PluginState *factory(Plugin *p, S2EExecutionState *s)
    {
        return new InterpreterMonitorState();
    }

    virtual ~InterpreterMonitorState() {
        // Destroy any object if needed
    }

    virtual InterpreterMonitorState *clone() const {
        return new InterpreterMonitorState(*this);
    }

    void IncrementInstructionCount() {
        instruction_count++;
    }

    unsigned long GetInstructionCount() {
        return instruction_count;
    }
};

}

S2E_DEFINE_PLUGIN(InterpreterMonitor, "Describe what the plugin does here", "", );

void InterpreterMonitor::initialize() {
    S2E * s = s2e();
    s->getCorePlugin()->onTranslateInstructionStart.connect(
        sigc::mem_fun(*this, &InterpreterMonitor::onInstructionTranslation)
        );
    s->getCorePlugin()->onStateKill.connect(
        sigc::mem_fun(*this, &InterpreterMonitor::printFinalInstructionCount)
        );
}



void InterpreterMonitor::handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize)
{
    S2E_INTERPRETERMONITOR_COMMAND command;

    if (guestDataSize != sizeof(command)) {
        getWarningsStream(state) << "mismatched S2E_INTERPRETERMONITOR_COMMAND size\n";
        return;
    }

    if (!state->mem()->read(guestDataPtr, &command, guestDataSize)) {
        getWarningsStream(state) << "could not read transmitted data\n";
        return;
    }

    switch (command.Command) {
        // TODO: add custom commands here
        case COMMAND_1:
            break;
        default:
            getWarningsStream(state) << "Unknown command " << command.Command << "\n";
            break;
    }
}

void InterpreterMonitor::onInstructionTranslation(ExecutionSignal *signal, S2EExecutionState *state,
                                                  TranslationBlock *tb, uint64_t pc) {
    signal->connect(sigc::mem_fun(*this, &InterpreterMonitor::onInstructionExecution));
}

void InterpreterMonitor::onInstructionExecution(S2EExecutionState *state, uint64_t pc) {
    DECLARE_PLUGINSTATE(InterpreterMonitorState, state);

    plgState->IncrementInstructionCount();
}

void InterpreterMonitor::printFinalInstructionCount(S2EExecutionState *state) {
    DECLARE_PLUGINSTATE(InterpreterMonitorState, state);

    S2E * s = s2e();
    s->getInfoStream() << "Instructions executed: " << plgState->GetInstructionCount() << '\n';
}

} // namespace plugins
} // namespace s2e