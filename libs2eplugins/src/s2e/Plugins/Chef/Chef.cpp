#include <optional>
#include <cstring>

#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>

#include "Chef.h"

namespace s2e {
namespace plugins {

namespace {

using namespace std;

class ChefState: public PluginState {
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

S2E_DEFINE_PLUGIN(Chef, "Chef provides support for symbolically executing interpreters. This is a version modified by Petr Stastny. See 'Prototyping symbolic execution engines for interpreted languages' by Bucur et al. for more info.", "", );

void Chef::initialize() {
    error_tc_stream = s2e()->openOutputFile("err_test_cases.json");
    success_tc_stream = s2e()->openOutputFile("successful_test_cases.json");

    // Begin JSON array
    *success_tc_stream << "[\n";
    *error_tc_stream << "[\n";

    // Subscribe to signals from other plugins
    on_state_kill = s2e()->getCorePlugin()->onStateKill.connect(
        sigc::mem_fun(*this, &Chef::onStateKill));

    LinuxMonitor *linux = s2e()->getPlugin<LinuxMonitor>();
    if (linux) {
        on_linux_segfault = linux->onSegFault.connect(sigc::mem_fun(*this, &Chef::onSegFault));
        getInfoStream() << "Connected to LinuxMonitor. Segfaults will generate an error test case.\n";
    } else {
        getInfoStream() << "LinuxMonitor not found. Segfaults will not generate an error test case.\n";
    }
}


Chef::~Chef() {
    // End JSON array
    *success_tc_stream << "]\n";
    *error_tc_stream << "]\n";

    delete error_tc_stream;
    delete success_tc_stream;

    on_state_kill.disconnect();
    on_linux_segfault.disconnect();
}


/// Interpreter emited a message for us.
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
        case START_CHEF:
            if(!isAtState(Inactive, state)) break;

            startSession(state);
            break;
        case END_CHEF:
            if(!isAtState(Active, state)) break;

            // We capture whether an error happened (so the state can be written to the error test cases file)
            endSession(state, command.data.end_chef.error_happened);
            break;
        case TRACE_UPDATE:
            // R tends to execute a lot of stuff before user code is started, there is no point in warning
            // the user if Chef is not turned on yet. Disable warn.
            if(!isAtState(Active, state, false)) break;

            instruction = {
                command.data.trace.op_code,
                command.data.trace.pc,
                command.data.trace.line
            };
            // Record function and filename, up to 60 characters.
            // SAFETY: we are copying 60 bytes to 61-bytes-long buffer, which was null-set before.
            strncpy((char*)instruction.function, (const char*)command.data.trace.function, 60);
            strncpy((char*)instruction.filename, (const char*)command.data.trace.filename, 60);
            // Record instruction update
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

/// Mark chef as started and record time of start.
void Chef::startSession(S2EExecutionState *state) {
    DECLARE_PLUGINSTATE(ChefState, state);
    plgState->currentStatus = Active;

    getWarningsStream(state) << "Chef state " << state->getID() << " switched to ACTIVE.\n";

    getInfoStream(state) << "Chef started\n";

    start_time_stamp = chrono_clock::now();
}

/// Stop receiving instruction updates and if error happened, dump way how to get to current state into a file.
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
    } else {
        dumpTestCase(state, *success_tc_stream);
    }
}

/// Dump info about current state into a file, including time from session start.
void Chef::dumpTestCase(S2EExecutionState *state, llvm::raw_ostream &out) {
    DECLARE_PLUGINSTATE(ChefState, state);

    ConcreteInputs inputs;
    bool success = state->getSymbolicSolution(inputs);

    if (!success) {
        getWarningsStream(state) << "Could not get symbolic solutions" << '\n';
        return;
    }

    // Write the variables themselves
    bool writeComma = streams_with_a_testcase.count(&out) >= 1;
    auto timestamp = (chrono_clock::now() - start_time_stamp) / 1s;
    writeSimpleTestCase(out, inputs, timestamp, plgState->lastInstructionExecuted->pc, plgState->lastInstructionExecuted->filename,
                    plgState->lastInstructionExecuted->function, plgState->lastInstructionExecuted->line, state->getID(), writeComma);

    streams_with_a_testcase.insert(&out);

    out.flush();
}

/// Output concrete values of variables into given stream
void Chef::writeSimpleTestCase(llvm::raw_ostream &os, const ConcreteInputs &inputs, long timestamp, uint64_t pc, const unsigned char * filename, const unsigned char * function, uint32_t line, uint32_t stateId, bool prefixComma) {
    std::stringstream ss;

    if (prefixComma) ss << ",";
    ss << "{\n";

    // Output metadata
    ss << "\t\"timestamp\" : " << timestamp << ",\n";
    ss << "\t\"pc\" : " << pc << ",\n";
    ss << "\t\"filename\" : \"" << filename << "\",\n";
    ss << "\t\"function\" : \"" << function << "\",\n";
    ss << "\t\"line\" : " << line << ",\n";
    ss << "\t\"stateId\" : " << stateId << ",\n";

    // Output input variables
    ss << "\t\"inputs\": [\n";

    bool isFirst = true;
    for (const VarValuePair & vp : inputs) {
        // vp containas name and value of the variable

        ss << "\t";
        if (!isFirst)
            ss << ",";

        isFirst = false;

        ss << "{ \"name\" : \"" << vp.first << "\",\n\t\t";

        // We do not know the type of the variable, so we will guess.
        // We will output raw bytes and value of the data as int32, int64 and a string.
        // If it is either of those, user will be able to easily pick it up.
        // We want to print those hexes as a string, so user can recognize it.
        // Automated tools will be able to parse it as well, but they are secondary concern.
        ss << "\"bytes\" : \"";
        for (unsigned i = 0; i < vp.second.size(); ++i) {
            if (i != 0)
                ss << " ";
            ss << std::setw(2) << std::setfill('0') << "0x" << std::hex << (unsigned) vp.second[i] << std::dec;
        }
        ss << "\"" << std::setfill(' ') << ",\n\t\t";

        if (vp.second.size() == sizeof(int32_t)) {
            int32_t valueAsInt = vp.second[0] | ((int32_t) vp.second[1] << 8) | ((int32_t) vp.second[2] << 16) |
                                 ((int32_t) vp.second[3] << 24);
            ss << "\"i32\" : " << valueAsInt << ",\n\t\t";
        }
        if (vp.second.size() == sizeof(int64_t)) {
            int64_t valueAsInt = vp.second[0] | ((int64_t) vp.second[1] << 8) | ((int64_t) vp.second[2] << 16) |
                                 ((int64_t) vp.second[3] << 24) | ((int64_t) vp.second[4] << 32) |
                                 ((int64_t) vp.second[5] << 40) | ((int64_t) vp.second[6] << 48) |
                                 ((int64_t) vp.second[7] << 56);
            ss << "\"i64\" : " << valueAsInt << ",\n\t\t";
        }

        ss << "\"string\" : \"";
        // Replace nonprintable chracters with an escape code; escape \ and "
        for (unsigned i = 0; i < vp.second.size(); ++i) {
            if (vp.second[i] == '\\' || vp.second[i] == '\"') {
                ss << "\\" << (char)vp.second[i];
            } else if (std::isprint(vp.second[i])) {
                ss << (char)vp.second[i];
            } else {
                // nonprintable -> escape
                ss << "\\\\x" << std::hex << (unsigned) vp.second[i] << std::dec;
            }
        }
        ss << "\"}\n";
    }

    ss << "\t]\n}\n";
    os << ss.str();
}

/// A high level instruction was executed. Record it.
void Chef::doUpdateHLPC(S2EExecutionState *state, HighLevelInstruction instruction) {
    DECLARE_PLUGINSTATE(ChefState, state);
    plgState->lastInstructionExecuted.emplace(instruction);

    on_hlpc_update.emit(state, instruction);
}

/// The state was killed, end the session if it didn't end already.
void Chef::onStateKill(S2EExecutionState *state) {
    if (isAtState(Active, state, false)) {
        endSession(state, false);
    }
}
void Chef::onSegFault(S2EExecutionState *state, uint64_t pid, uint64_t pc) {
    if (isAtState(Active, state, false)) {
        endSession(state, true);
    }
}
} // namespace plugins
} // namespace s2e
