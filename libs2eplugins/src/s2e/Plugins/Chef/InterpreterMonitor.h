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

#ifndef S2E_PLUGINS_INTERPRETERMONITOR_H
#define S2E_PLUGINS_INTERPRETERMONITOR_H

//#include <s2e/Plugin.h>
#include "../../../../../libs2ecore/include/s2e/Plugin.h"

//#include <s2e/Plugins/Core/BaseInstructions.h>
#include "../Core/BaseInstructions.h"

#include "HighLevelUtilities.h"

namespace s2e {
namespace plugins {


enum S2E_INTERPRETERMONITOR_COMMANDS {
    TraceUpdate
};

struct S2E_INTERPRETERMONITOR_COMMAND {
    // There is just one command - TraceUpdate.
    // In order to not break compatibility with
    // existing Chef code, this has to be left out.
    //S2E_INTERPRETERMONITOR_COMMANDS Command;
    //union {
    uint32_t op_code;
    uint32_t frame_count;
    uint32_t frames[2];
    uint32_t line;
    uint8_t function[61];
    uint8_t filename[61];
    //};
} __attribute((packed));



class InterpreterMonitor : public Plugin, public IPluginInvoker {

    S2E_PLUGIN
public:
    InterpreterMonitor(S2E *s2e);

    void initialize();

    void startTrace(S2EExecutionState * state);
    void stopTrace(S2EExecutionState * state);

    HighLevelTreeNode *getHLTreeNode(S2EExecutionState *state) const;
    void dumpHighLevelTree(std::ostream &os);
    void dumpHighLevelCFG(std::ostream &os);

    HighLevelCFG &cfg() {
        return cfg_;
    }

    bool active() const {
        return root_node_ != NULL;
    }

private:
    // Allow the guest to communicate with this plugin using s2e_invoke_plugin
    virtual void handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize);

    sigc::signal<void, S2EExecutionState*, HighLevelTreeNode*> on_hlpc_update;

private:
    typedef std::vector<S2EExecutionState*> StateVector;
    typedef std::map<S2EExecutionState*, HighLevelTreeNode*> StateNodeMapping;

    HighLevelCFG cfg_;

    HighLevelTreeNode *root_node_;
    HighLevelTreeNode *active_node_;
    S2EExecutionState *active_state_;
    StateNodeMapping state_mapping_;

    // Required connections
    sigc::connection on_state_fork_;
    sigc::connection on_state_switch_;
    sigc::connection on_state_kill_;

    void doUpdateHLPC(S2EExecutionState *state, const HighLevelPC &hlpc,
                      HighLevelOpcode opcode, std::string filename, std::string function, int line);

    void onStateFork(S2EExecutionState *state, const StateVector &new_states,
                     const std::vector<klee::ref<klee::Expr> > &new_conditions);
    void onStateSwitch(S2EExecutionState *state, S2EExecutionState *new_state);
    void onStateKill(S2EExecutionState *state);
};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_INTERPRETERMONITOR_H