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

S2E_DEFINE_PLUGIN(InterpreterMonitor, "Support tracing of high-level interpreted code.", "", );

InterpreterMonitor::InterpreterMonitor(S2E *s2e)
        : Plugin(s2e),
          cfg_(s2e->getInfoStream()), // TODO: Custom stream
          root_node_(NULL),
          active_node_(NULL),
          active_state_(NULL) {

}


void InterpreterMonitor::initialize() {

}

void InterpreterMonitor::startTrace(S2EExecutionState *state) {
    assert(root_node_ == NULL && "Tracing already in progress");

    on_state_fork_ = s2e()->getCorePlugin()->onStateFork.connect(
            sigc::mem_fun(*this, &InterpreterMonitor::onStateFork));
    on_state_switch_ = s2e()->getCorePlugin()->onStateSwitch.connect(
            sigc::mem_fun(*this, &InterpreterMonitor::onStateSwitch));
    on_state_kill_ = s2e()->getCorePlugin()->onStateKill.connect(
            sigc::mem_fun(*this, &InterpreterMonitor::onStateKill));

    HighLevelInstruction *root = cfg_.recordNode(HighLevelPC());
    root_node_ = new HighLevelTreeNode(root, NULL);
    active_node_ = root_node_;
    active_state_ = state;

    active_node_->bumpPathCounter();
}

void InterpreterMonitor::stopTrace(S2EExecutionState *state) {
    root_node_->clear();
    delete root_node_;

    cfg_.clear();

    root_node_ = NULL;
    active_node_ = NULL;
    active_state_ = NULL;

    on_state_fork_.disconnect();
    on_state_switch_.disconnect();
    on_state_kill_.disconnect();
}


void InterpreterMonitor::handleOpcodeInvocation(S2EExecutionState *state,
                                                uint64_t guestDataPtr, uint64_t guestDataSize) {
    S2E_INTERPRETERMONITOR_COMMAND command;

    if (root_node_ == NULL) {
        return; // TODO: 0
    }

    if (guestDataSize != sizeof(command)) {
        getWarningsStream(state) << "mismatched S2E_INTERPRETERMONITOR_COMMAND size\n";
        return;
    }

    if (!state->mem()->read(guestDataPtr, &command, guestDataSize)) {
        getWarningsStream(state) << "failed to read interpretermonitor command\n";
        return;
    }

    HighLevelPC hlpc = HighLevelPC(&command.frames[0],
                                   &command.frames[command.frame_count]);
    HighLevelOpcode opcode = command.op_code;

    doUpdateHLPC(state, hlpc, opcode, (char *)command.filename, (char *)command.function, command.line);
    return; // TODO: 0
}


void InterpreterMonitor::onStateFork(S2EExecutionState *state,
                                     const StateVector &new_states,
                                     const std::vector<klee::ref<klee::Expr> > &new_conditions) {
    assert(state == active_state_);

    for (StateVector::const_iterator it = new_states.begin(),
                 ie = new_states.end(); it != ie; ++it) {
        S2EExecutionState *new_state = *it;
        if (new_state == state)
            continue;

        state_mapping_[new_state] = active_node_;
        active_node_->bumpForkCounter();
    }
}


void InterpreterMonitor::onStateSwitch(S2EExecutionState *state,
                                       S2EExecutionState *new_state) {
    getDebugStream(state) << "Switching to state " << new_state->getID() << '\n';

    assert(state == active_state_);

    state_mapping_[state] = active_node_;
    active_node_ = getHLTreeNode(new_state);
    assert(active_node_ != NULL);
    active_state_ = new_state;

    // We assume here that we switch to a new state, so we don't risk
    // counting the same path twice.
    active_node_->bumpPathCounter();
}


void InterpreterMonitor::onStateKill(S2EExecutionState *state) {
    state_mapping_.erase(state);
}


void InterpreterMonitor::doUpdateHLPC(S2EExecutionState *state,
                                      const HighLevelPC &hlpc, HighLevelOpcode opcode, std::string filename, std::string function, int line) {
    assert(state == active_state_);

    HighLevelInstruction *inst = cfg_.recordEdge(
            active_node_->instruction()->hlpc(),
            hlpc, opcode);

    inst->filename = filename;
    inst->function = function;
    inst->line = line;

    active_node_ = active_node_->getOrCreateSuccessor(inst);
    active_node_->bumpPathCounter();

    on_hlpc_update.emit(state, active_node_);
}


HighLevelTreeNode *InterpreterMonitor::getHLTreeNode(S2EExecutionState *state) const {
    if (root_node_ == NULL)
        return NULL;

    if (state == active_state_) {
        return active_node_;
    }

    StateNodeMapping::const_iterator it = state_mapping_.find(state);

    if (it == state_mapping_.end()) {
        return NULL;
    }

    return it->second;
}


void InterpreterMonitor::dumpHighLevelTree(std::ostream &os) {
    assert(root_node_ != NULL);

    HighLevelTreeVisualizer visualizer(os);
    visualizer.dumpTree(root_node_);
}


void InterpreterMonitor::dumpHighLevelCFG(std::ostream &os) {
    HighLevelCFGVisualizer visualizer(os);
    cfg_.analyzeCFG();
    visualizer.dumpCFG(cfg_);
}


} // namespace plugins
} // namespace s2e