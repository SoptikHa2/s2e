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

#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>

#include "ConcolicSession.h"

#include "../ExecutionTracers/MemoryTracer.h"
#include "../ExecutionTracers/TranslationBlockTracer.h"

#include "Utils.hpp"

#include <s2e/ConfigFile.h>
#include <s2e/S2E.h>
#include <s2e/S2EExecutor.h>
#include <s2e/Utils.h>

#include <klee/ExecutionState.h>

#include <fstream>
#include <queue>
#include <cmath>
#include <unistd.h>

using namespace llvm;
using namespace klee;

namespace s2e {
namespace plugins {

typedef enum {
    CONCOLIC_RET_OK,
    CONCOLIC_RET_TOO_SMALL,
    CONCOLIC_RET_ERROR
} ConcolicStatus;

namespace {

// This class can optionally be used to store per-state plugin data.
//
// Use it as follows:
// void ConcolicSession::onEvent(S2EExecutionState *state, ...) {
//     DECLARE_PLUGINSTATE(ConcolicSessionState, state);
//     plgState->...
// }
//
class ConcolicSessionState: public PluginState {
    // Declare any methods and fields you need here

public:
    static PluginState *factory(Plugin *p, S2EExecutionState *s) {
        return new ConcolicSessionState();
    }

    virtual ~ConcolicSessionState() {
        // Destroy any object if needed
    }

    virtual ConcolicSessionState *clone() const {
        return new ConcolicSessionState(*this);
    }
};

}

S2E_DEFINE_PLUGIN(ConcolicSession, "Describe what the plugin does here", "", );

ConcolicSession::ConcolicSession(S2E* s2e_)
        : Plugin(s2e_),
          cfg_tc_stream_(NULL),
          paths_tc_stream_(NULL),
          error_tc_stream_(NULL),
          all_tc_stream_(NULL),
          compl_feature_stream_(NULL),
          pending_feature_stream_(NULL),
          stop_on_error_(true),
          tree_dump_interval_(0),
          extra_details_(false),
          interp_monitor_(NULL),
          root_fork_point_(NULL),
          active_state_(NULL),
          tree_divergence_node_(NULL),
          cfg_divergence_node_(NULL),
          starting_fork_point_(NULL),
          active_fork_point_(NULL),
          active_fork_index_(0),
          start_time_stamp_(chrono_clock::now()),
          path_time_stamp_(chrono_clock::now()),
          next_dump_stamp_(chrono_clock::now()),
          tb_tracer_(NULL) {
}

ConcolicSession::~ConcolicSession() {
    delete pending_feature_stream_;
    delete compl_feature_stream_;
    delete cfg_tc_stream_;
    delete paths_tc_stream_;
    delete error_tc_stream_;
    delete all_tc_stream_;
}

void ConcolicSession::initialize() {
    stop_on_error_ = s2e()->getConfig()->getBool(getConfigKey() + ".stopOnError", false);
    tree_dump_interval_ = s2e()->getConfig()->getInt(getConfigKey() + ".treeDumpInterval", 60);
    extra_details_ = s2e()->getConfig()->getBool(getConfigKey() + ".extraDetails", false);

    cfg_tc_stream_ = s2e()->openOutputFile("cfg_test_cases.dat");
    paths_tc_stream_ = s2e()->openOutputFile("hl_test_cases.dat");
    error_tc_stream_ = s2e()->openOutputFile("err_test_cases.dat");
    all_tc_stream_ = s2e()->openOutputFile("all_test_cases.dat");
    compl_feature_stream_ = s2e()->openOutputFile("complete_features.dat");
    pending_feature_stream_ = s2e()->openOutputFile("pending_features.dat");

    tb_tracer_ = static_cast<TranslationBlockTracer*>(
            s2e()->getPlugin("TranslationBlockTracer"));

    interp_monitor_ = static_cast<InterpreterMonitor*>(
            s2e()->getPlugin("InterpreterMonitor"));
}



void ConcolicSession::handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize)
{
    S2E_CONCOLICSESSION_COMMAND command;

    if (guestDataSize != sizeof(command)) {
        getWarningsStream(state) << "mismatched S2E_CONCOLICSESSION_COMMAND size\n";
        return;
    }

    if (!state->mem()->read(guestDataPtr, &command, guestDataSize)) {
        getWarningsStream(state) << "could not read transmitted data\n";
        return;
    }

    switch (command.Command) {
        case START_CONCOLIC_SESSION:
            startConcolicSession(state, command.max_time);
            break;
        case END_CONCOLIC_SESSION:
            endConcolicSession(state, command.is_error_path);
            break;
        default:
            getWarningsStream(state) << "Unknown command " << command.Command << "\n";
            break;
    }
}


int ConcolicSession::startConcolicSession(S2EExecutionState *state,
                                          uint32_t max_time) {
    assert(active_state_ == NULL);

    interp_monitor_->startTrace(state);

    active_state_ = state;
    tree_divergence_node_ = NULL;
    cfg_divergence_node_ = NULL;

    start_time_stamp_ = chrono_clock::now();
    path_time_stamp_ = start_time_stamp_;

    if (tree_dump_interval_ > 0) {
        next_dump_stamp_ = start_time_stamp_ + std::chrono::seconds(tree_dump_interval_);
    } else {
        next_dump_stamp_ = chrono_time_point();
    }

    root_fork_point_ = new ForkPoint(NULL, -1, active_state_->pc,
                                     interp_monitor_->getHLTreeNode(active_state_), 1);
    starting_fork_point_ = root_fork_point_;

    if (tb_tracer_) {
        tb_tracer_->enableTracing(state, TranslationBlockTracer::TraceType::TB_START);
    }

    // Activate the callbacks
    on_state_fork_ = s2e()->getCorePlugin()->onStateFork.connect(
            sigc::mem_fun(*this, &ConcolicSession::onStateFork));
    on_state_kill_ = s2e()->getCorePlugin()->onStateKill.connect(
            sigc::mem_fun(*this, &ConcolicSession::onStateKill));
    on_timer_ = s2e()->getCorePlugin()->onTimer.connect(
            sigc::mem_fun(*this, &ConcolicSession::onTimer));
    on_interpreter_trace_ = interp_monitor_->on_hlpc_update.connect(
            sigc::mem_fun(*this, &ConcolicSession::onInterpreterTrace));

    s2e()->getInfoStream(state) << "***** CONCOLIC SESSION - START *****"
                                    << '\n';

    return CONCOLIC_RET_OK;
}


int ConcolicSession::endConcolicSession(S2EExecutionState *state,
                                        bool is_error_path) {
    HighLevelTreeNode *trace_node = interp_monitor_->getHLTreeNode(state);

    chrono_time_point time_stamp = chrono_clock::now();

    if (is_error_path && stop_on_error_) {
        assert(trace_node->path_counter() == 1
               && "How could you miss it the first time?");
        s2e()->getInfoStream(state) << "Error path hit!" << '\n';
    } else {
        assert(trace_node->path_counter() > 0);

        if (is_error_path) {
            // We hit an error branch (but we carry on), log test case into special file
            s2e()->getInfoStream(state) << "Error path hit, generating test case." << '\n';
            dumpTestCase(state, time_stamp, time_stamp - path_time_stamp_, *error_tc_stream_);
        }
    }

    s2e()->getInfoStream(state) << "Processing test case for " << state->
                                    << klee::concolics(state) << '\n';

    if (interp_monitor_->cfg().changed()) {
        assert(trace_node->path_counter() == 1
               && "How could you miss it the first time?");
        s2e()->getDebugStream(state) << "New CFG fragment discovered!"
                                        << '\n';

        dumpTestCase(state, time_stamp, time_stamp - path_time_stamp_,
                     *cfg_tc_stream_);
    }

    if (trace_node->path_counter() == 1) {
        s2e()->getDebugStream(state) << "New HL tree path!" << '\n';

        dumpTestCase(state, time_stamp, time_stamp - path_time_stamp_,
                     *paths_tc_stream_);
    }

    dumpTestCase(state, time_stamp, time_stamp - path_time_stamp_,
                 *all_tc_stream_);

    interp_monitor_->cfg().analyzeCFG();
    //pending_states_->updateWeights();

    // Measure this again since the CFG analysis may be expensive
    time_stamp = chrono_clock::now();

    if (/*emptyPendingStates() ||*/ (is_error_path && stop_on_error_)) {
        s2e()->getWarningsStream(state)
                << ((is_error_path && stop_on_error_) ? "Premature termination."
                                                      : "Exhaustive search complete.")
                << '\n';
        terminateSession(state);
    } else {
        path_time_stamp_ = time_stamp;
    }

    // s2e()->getExecutor()->yieldState(*state);
    s2e()->getExecutor()->terminateState(*state);
    // Unreachable at this point

    return CONCOLIC_RET_OK;
}


void ConcolicSession::dumpTestCase(S2EExecutionState *state,
                                   chrono_time_point time_stamp,
                                   chrono_time_point total_delta,
                                   llvm::raw_ostream &out) {
    out << (time_stamp - start_time_stamp_).usec();
    out << " " << hexval(starting_fork_point_->pc());
    out << " " << starting_fork_point_->hl_node()->instruction()->filename << ":" <<
        starting_fork_point_->hl_node()->instruction()->function << ":" << starting_fork_point_->hl_node()->instruction()->line;

    if (extra_details_) {
        int min_dist, max_dist;
        //computeMinMaxDistToUncovered(state, min_dist, max_dist);

        HighLevelTreeNode *starting_node = starting_fork_point_->hl_node();

        out << " " << starting_node->instruction()->dist_to_uncovered();

        if (tree_divergence_node_) {
            out << " " << tree_divergence_node_->distanceToAncestor(starting_node)
                << "/" << interp_monitor_->cfg().computeMinDistance(
                    starting_node->instruction(),
                    tree_divergence_node_->instruction());
        } else {
            out << " " << "-/-";
        }

        if (cfg_divergence_node_) {
            out << " " << cfg_divergence_node_->distanceToAncestor(starting_node)
                << "/" << interp_monitor_->cfg().computeMinDistance(
                    starting_node->instruction(),
                    cfg_divergence_node_->instruction());
        } else {
            out << " " << "-/-";
        }

        out << " " << min_dist << "/" << max_dist;
    }

    klee::Assignment assignment = state->concolics;

    for (klee::Assignment::bindings_ty::iterator
                 bit = assignment.bindings.begin(),
                 bie = assignment.bindings.end(); bit != bie; ++bit) {
        std::string assgn_value(bit->second.begin(), bit->second.end());

        out << " " << bit->first->getName() << "=>";
        out << hexstring(assgn_value);
    }

    out << '\n';
    out.flush();
}


void ConcolicSession::terminateSession(S2EExecutionState *state) {
    if (tb_tracer_) {
        tb_tracer_->disableTracing(state, TranslationBlockTracer::TraceType::TB_START);
    }

    dumpTraceGraphs();

    if (!emptyPendingStates()) {
        StateVector state_vector;
        copyAndClearPendingStates(state_vector);

        s2e()->getMessagesStream(state) << "Terminating "
                                        << state_vector.size() << " pending states." << '\n';

        for (StateVector::iterator it = state_vector.begin(),
                     ie = state_vector.end(); it != ie; ++it) {
            S2EExecutionState *pending_state = *it;
#if 0 // TODO: Make this a configurable option
            dumpTestCase(pending_state, timed_out_test_cases_);
#endif
            s2e()->getExecutor()->terminateState(*pending_state);
        }
    }

    s2e()->getMessagesStream(state) << "***** CONCOLIC SESSION - END *****" << '\n';

    active_state_ = NULL;

    on_timer_.disconnect();
    on_state_fork_.disconnect();
    on_state_kill_.disconnect();
    on_state_switch_.disconnect();
    on_interpreter_trace_.disconnect();

    interp_monitor_->stopTrace(state);

    root_fork_point_->clear();
    delete root_fork_point_;
    root_fork_point_ = NULL;
}


void ConcolicSession::onInterpreterTrace(S2EExecutionState *state,
                                         HighLevelTreeNode *tree_node) {
    assert(state == active_state_);

    // Clear any lucky strike
    // fork_strike_.clear();

    if (tree_divergence_node_ == NULL && tree_node->path_counter() == 1) {
        tree_divergence_node_ = tree_node;
    }
    if (cfg_divergence_node_ == NULL && interp_monitor_->cfg().changed()) {
        cfg_divergence_node_ = tree_node;
    }
}


void ConcolicSession::onStateFork(S2EExecutionState *state,
                                  const StateVector &newStates,
                                  const std::vector<ref<Expr> > &newConditions) {
    assert(state == active_state_);

    active_fork_point_ = new ForkPoint(active_fork_point_, active_fork_index_,
                                       active_state_->pc,
                                       interp_monitor_->getHLTreeNode(active_state_),
                                       newStates.size() + 1);
    active_fork_index_ = 0;

    for (int i = 0; i < newStates.size(); ++i) {
        S2EExecutionState *new_state = newStates[i];

        if (new_state == state)
            continue;

        pending_fork_points_[new_state].first = active_fork_point_;
        pending_fork_points_[new_state].second = (i + 1);

        insertPendingState(new_state);
    }
}


void ConcolicSession::onStateKill(S2EExecutionState *state) {
    assert(active_state_ != NULL);

    if (state != active_state_) {
        // This happens when a state was killed at the end of
        // the scheduleNextState call. The active_state_ is updated before
        // killing the current state.
        return;
    }

    // In case of an unplanned kill, we must schedule a new alternate
    endConcolicSession(state, false);
}


void ConcolicSession::onTimer() {
    if (active_state_ == NULL) {
        return;
    }

    chrono_time_point time_stamp = chrono_clock::now();
    S2EExecutionState *state = active_state_;

    if (path_deadline_ != sys::TimeValue::ZeroTime && time_stamp >= path_deadline_) {
        s2e()->getMessagesStream(state) << "State time-out." << '\n';
        endConcolicSession(state, true);
        return;
    }

    if (session_deadline_ == sys::TimeValue::ZeroTime
        && next_dump_stamp_ == sys::TimeValue::ZeroTime)
        return;

    if (session_deadline_ != sys::TimeValue::ZeroTime && time_stamp >= session_deadline_) {
        s2e()->getMessagesStream(state) << "Concolic session time-out."
                                        << '\n';
        terminateSession(state);
        s2e()->getExecutor()->terminateState(*state);
        return;
    }

    if (next_dump_stamp_ != sys::TimeValue::ZeroTime && time_stamp >= next_dump_stamp_) {
        s2e()->getMessagesStream(state) << "Dumping execution tree." << '\n';
        dumpTraceGraphs();
        next_dump_stamp_ = time_stamp + sys::TimeValue((int64_t)tree_dump_interval_);
    }
}

void ConcolicSession::dumpTraceGraphs() {
    std::ofstream tree_of;
    std::string file_name = s2e()->getNextOutputFilename("interp_tree.dot");
    tree_of.open(file_name.c_str());
    assert(tree_of.good() && "Could not open interpreter tree dump");

    interp_monitor_->dumpHighLevelTree(tree_of);
    tree_of.close();

    std::ofstream cfg_of;
    file_name = s2e()->getNextOutputFilename("interp_cfg.dot");
    cfg_of.open(file_name.c_str());
    assert(cfg_of.good() && "Could not open interpreter CFG dump");

    interp_monitor_->dumpHighLevelCFG(cfg_of);
    cfg_of.close();
}


} // namespace plugins
} // namespace s2e