#include <optional>
#include <cstring>

#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>

#include "Chef.h"

namespace s2e {
namespace plugins {

namespace {

using namespace std;

//
// This class can optionally be used to store per-state plugin data.
//
// Use it as follows:
// void Chef::onEvent(S2EExecutionState *state, ...) {
//     DECLARE_PLUGINSTATE(ChefState, state);
//     plgState->...
// }
//
class ChefState: public PluginState {
    // Declare any methods and fields you need here

public:
    optional<HighLevelInstruction> lastInstructionExecuted;
    ChefStatus currentStatus = Inactive;

    static PluginState *factory(Plugin *p, S2EExecutionState *s) {
        return new ChefState();
    }

    virtual ~ChefState() { }

    virtual ChefState *clone() const {
        return new ChefState(*this);
    }
};

}

S2E_DEFINE_PLUGIN(Chef, "Describe what the plugin does here", "", );

void Chef::initialize() {
    // TODO: cfg, path-specific test cases
    //cfg_tc_stream = s2e()->openOutputFile("cfg_test_cases.dat");
    //paths_tc_stream = s2e()->openOutputFile("hl_test_cases.dat");
    error_tc_stream = s2e()->openOutputFile("err_test_cases.dat");
    all_tc_stream = s2e()->openOutputFile("all_test_cases.dat");
}


void Chef::handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize) {
    S2E_CHEF_COMMAND command;

    if (guestDataSize != sizeof(command)) {
        getWarningsStream(state) << "mismatched S2E_CHEF_COMMAND size\n";
        return;
    }

    if (!state->mem()->read(guestDataPtr, &command, guestDataSize)) {
        getWarningsStream(state) << "could not read transmitted data\n";
        return;
    }

    HighLevelInstruction instruction = {};
    switch (command.Command) {
        // TODO: Impl
        case START_CHEF:
            if(!isAtState(Inactive, state)) break;
            startSession(state);
            break;
        case END_CHEF:
            if(!isAtState(Active, state)) break;
            endSession(state, command.data.end_chef.error_happened);
            break;
        case TRACE_UPDATE:
            if(!isAtState(Active, state, false)) break;
            instruction = {
                command.data.trace.op_code,
                command.data.trace.pc,
                command.data.trace.line
            };
            strncpy((char*)instruction.function, (const char*)command.data.trace.function, 60);
            strncpy((char*)instruction.filename, (const char*)command.data.trace.filename, 60);
            doUpdateHLPC(state, instruction);
        break;
    default:
        getWarningsStream(state) << "Unknown command " << command.Command << "\n";
        break;
    }
}

bool Chef::isAtState(ChefStatus targetStatus, S2EExecutionState * state, bool warn) {
    DECLARE_PLUGINSTATE(ChefState, state);
    if(plgState->currentStatus != targetStatus) {
        if (warn)
            getWarningsStream(state) << "Chef was supposed to be in state " << targetStatus << ", but is not.\n";
        return false;
    }
    return true;
}

void Chef::startSession(S2EExecutionState *state) {
    DECLARE_PLUGINSTATE(ChefState, state);
    plgState->currentStatus = Active;

    getWarningsStream(state) << "Chef state " << state->getID() << " switched to ACTIVE.\n";

    getInfoStream(state) << "Chef started\n";

    start_time_stamp = chrono_clock::now();

    // TODO: fork callback
    on_state_kill = s2e()->getCorePlugin()->onStateKill.connect(
        sigc::mem_fun(*this, &Chef::onStateKill));
}

void Chef::endSession(S2EExecutionState *state, bool error_happened) {
    DECLARE_PLUGINSTATE(ChefState, state);
    plgState->currentStatus = Inactive;

    getWarningsStream(state) << "Chef state " << state->getID() << " switched to INACTIVE.\n";

    if (error_happened)
        getInfoStream(state) << "Chef ended with error\n";
    else
        getInfoStream(state) << "Chef ended\n";

    if (error_happened) {
        dumpTestCase(state, *error_tc_stream);
    }

    dumpTestCase(state, *all_tc_stream);
}

void Chef::dumpTestCase(S2EExecutionState *state, llvm::raw_ostream &out) {
    DECLARE_PLUGINSTATE(ChefState, state);

    out << (chrono_clock::now() - start_time_stamp).count() << " ";
    out << hexval((uint64_t)state->pc) << " ";
    if (plgState->lastInstructionExecuted) {
        out << " " << plgState->lastInstructionExecuted->filename << ":" <<
            plgState->lastInstructionExecuted->function << ":" << plgState->lastInstructionExecuted->line;
    }

    ConcreteInputs inputs;
    bool success = state->getSymbolicSolution(inputs);

    if (!success) {
        getWarningsStream(state) << "Could not get symbolic solutions" << '\n';
        return;
    }

    writeSimpleTestCase(out, inputs);

    out << '\n';
    out.flush();
}

void Chef::writeSimpleTestCase(llvm::raw_ostream &os, const ConcreteInputs &inputs) {
    std::stringstream ss;
    ConcreteInputs::const_iterator it;
    for (it = inputs.begin(); it != inputs.end(); ++it) {
        const VarValuePair &vp = *it;
        ss << std::setw(20) << vp.first << " = {";

        for (unsigned i = 0; i < vp.second.size(); ++i) {
            if (i != 0)
                ss << ", ";
            ss << std::setw(2) << std::setfill('0') << "0x" << std::hex << (unsigned) vp.second[i] << std::dec;
        }
        ss << "}" << std::setfill(' ') << "; ";

        if (vp.second.size() == sizeof(int32_t)) {
            int32_t valueAsInt = vp.second[0] | ((int32_t) vp.second[1] << 8) | ((int32_t) vp.second[2] << 16) |
                                 ((int32_t) vp.second[3] << 24);
            ss << "(int32_t) " << valueAsInt << ", ";
        }
        if (vp.second.size() == sizeof(int64_t)) {
            int64_t valueAsInt = vp.second[0] | ((int64_t) vp.second[1] << 8) | ((int64_t) vp.second[2] << 16) |
                                 ((int64_t) vp.second[3] << 24) | ((int64_t) vp.second[4] << 32) |
                                 ((int64_t) vp.second[5] << 40) | ((int64_t) vp.second[6] << 48) |
                                 ((int64_t) vp.second[7] << 56);
            ss << "(int64_t) " << valueAsInt << ", ";
        }

        ss << "(string) \"";
        for (unsigned i = 0; i < vp.second.size(); ++i) {
            ss << (char) (std::isprint(vp.second[i]) ? vp.second[i] : '.');
        }
        ss << "\"\n";
    }

    os << ss.str();
}

void Chef::doUpdateHLPC(S2EExecutionState *state, HighLevelInstruction instruction) {
    DECLARE_PLUGINSTATE(ChefState, state);
    plgState->lastInstructionExecuted.emplace(instruction);

    on_hlpc_update.emit(state, instruction);
}
void Chef::onStateKill(S2EExecutionState *state) {
    if (isAtState(Active, state, false)) {
        endSession(state, false);
    }
}
} // namespace plugins
} // namespace s2e
