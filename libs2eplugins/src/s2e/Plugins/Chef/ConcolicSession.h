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

#ifndef S2E_PLUGINS_CONCOLICSESSION_H
#define S2E_PLUGINS_CONCOLICSESSION_H

#include <s2e/Plugin.h>
#include <s2e/Plugins/Core/BaseInstructions.h>
//#include <s2e/Selectors.h> // TODO: Look into Plugins/Searchers/whatever (CooperativeSearcher ?)
//#include <klee/Searcher.h>

#include "InterpreterMonitor.h"

//#include <llvm/Support/TimeValue.h> // Removed in favor of std::chrono https://reviews.llvm.org/D25416
#include <llvm/Support/raw_ostream.h>

#include <set>
#include <vector>
#include <map>
#include <fstream>


namespace s2e {
namespace plugins {

class ConcolicSession;

class ForkPoint {
public:
    typedef std::vector<ForkPoint*> ForkPointVector;

    ForkPoint(ForkPoint *parent, int index, uint64_t pc,
              HighLevelTreeNode* hl_node, int children_count)
        : parent_(parent),
          children_(children_count),
          depth_(0),
          index_(index),
          pc_(pc),
          hl_node_(hl_node) {
        if (parent != NULL) {
            assert(0 <= index && index < parent->children_.size());

            parent->children_[index] = this;
            depth_ = parent->depth_ + 1;
        } else {
            index_ = -1;
        }
    }

    ForkPoint *parent() const {
        return parent_;
    }

    int index() const {
        return index_;
    }

    int depth() const {
        return depth_;
    }

    uint64_t pc() const {
        return pc_;
    }

    HighLevelTreeNode *hl_node() const {
        return hl_node_;
    }

    void clear() {
        for (ForkPointVector::iterator it = children_.begin(),
                                       ie = children_.end(); it != ie; ++it) {
            ForkPoint *fork_point = *it;
            if (fork_point != NULL) {
                fork_point->clear();
                delete fork_point;
            }
        }
        children_.clear();
    }

private:
    ForkPoint *parent_;
    ForkPointVector children_;

    int depth_;
    int index_;

    uint64_t pc_;
    HighLevelTreeNode *hl_node_;

    // Disallow copy and assign
    ForkPoint(const ForkPoint&);
    void operator=(const ForkPoint&);
};

class TranslationBlockTracer;


enum S2E_CONCOLICSESSION_COMMANDS {
    START_CONCOLIC_SESSION,
    END_CONCOLIC_SESSION
};

struct S2E_CONCOLICSESSION_COMMAND {
    S2E_CONCOLICSESSION_COMMANDS Command;
    uint32_t max_time;
    uint8_t is_error_path;
    uint32_t result_ptr;
    uint32_t result_size;
} __attribute__((packed));



class ConcolicSession : public Plugin, public IPluginInvoker {

    S2E_PLUGIN
public:
    ConcolicSession(S2E *s2e);
    ~ConcolicSession();

    void initialize();

private:
    typedef std::vector<S2EExecutionState*> StateVector;
    typedef std::set<S2EExecutionState*> StateSet;
    typedef std::map<S2EExecutionState*, double> ForkWeightMap;
    typedef std::map<S2EExecutionState*, std::pair<ForkPoint*, int> > ForkPointMap;

    // Allow the guest to communicate with this plugin using s2e_invoke_plugin
    virtual void handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize);


    // Test case tracing
    llvm::raw_ostream *cfg_tc_stream_;
    llvm::raw_ostream *paths_tc_stream_;
    llvm::raw_ostream *error_tc_stream_;
    llvm::raw_ostream *all_tc_stream_;

    llvm::raw_ostream *compl_feature_stream_;
    llvm::raw_ostream *pending_feature_stream_;

    // Session configuration
    bool stop_on_error_;
    int tree_dump_interval_;
    bool extra_details_;

    // Callback connections
    sigc::connection on_interpreter_trace_;
    sigc::connection on_state_fork_;
    sigc::connection on_state_kill_;
    sigc::connection on_timer_;

    // The interpreter monitor plug-in
    InterpreterMonitor *interp_monitor_;

    // The fork tree
    ForkPoint *root_fork_point_;

    // Active state information
    S2EExecutionState *active_state_;
    HighLevelTreeNode *tree_divergence_node_;
    HighLevelTreeNode *cfg_divergence_node_;

    // Fork points
    ForkPoint *starting_fork_point_;
    ForkPoint *active_fork_point_;
    int active_fork_index_;
    ForkPointMap pending_fork_points_; // XXX: Memory leaks everywhere

    // Time tracking
    using chrono_clock = std::chrono::system_clock;
    using chrono_time_point = std::chrono::time_point<chrono_clock>;
    using chrono_duration = std::chrono::duration<chrono_clock>;
    chrono_time_point start_time_stamp_;
    chrono_time_point path_time_stamp_;
    chrono_time_point next_dump_stamp_;


    // Debugging
    TranslationBlockTracer *tb_tracer_;

    void terminateSession(S2EExecutionState *state);
    void dumpTestCase(S2EExecutionState *state,
                      chrono_time_point time_stamp, chrono_time_point total_delta,
                      llvm::raw_ostream &out);

    int startConcolicSession(S2EExecutionState *state, uint32_t max_time);
    int endConcolicSession(S2EExecutionState *state, bool is_error_path);

    void dumpTraceGraphs();

    void onInterpreterTrace(S2EExecutionState *state,
                            HighLevelTreeNode *tree_node);
    void onStateFork(S2EExecutionState *state, const StateVector &newStates,
                     const std::vector<klee::ref<klee::Expr> > &newConditions);
    void onStateKill(S2EExecutionState *state);
    void onTimer();

    // Disallow copy and assign
    ConcolicSession(const ConcolicSession&) = delete;
    void operator=(const ConcolicSession&) = delete;
};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_CONCOLICSESSION_H