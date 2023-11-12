#ifndef S2E_PLUGINS_CHEF_H
#define S2E_PLUGINS_CHEF_H

#include <vector>

#include <s2e/Plugin.h>
#include <s2e/Plugins/Core/BaseInstructions.h>

#include "Utils.hpp"

namespace s2e {
namespace plugins {


enum S2E_CHEF_COMMANDS {
    START_CHEF,
    END_CHEF,
    TRACE_UPDATE
};

struct S2E_CHEF_COMMAND {
    S2E_CHEF_COMMANDS Command;
    union {
        struct {
        } start_chef;
        struct {
            uint8_t error_happened;
        } end_chef;
        struct {
            uint32_t op_code;
            uint32_t pc;
            uint32_t line;
            uint8_t function[61];
            uint8_t filename[61];
        } trace;
    } data;
};

enum ChefStatus {
    Inactive,
    Active
};

class Chef : public Plugin, public IPluginInvoker {

    S2E_PLUGIN

private:
    typedef std::pair<std::string, std::vector<unsigned char>> VarValuePair;
    typedef std::vector<VarValuePair> ConcreteInputs;

    // test case output files
    // TODO: cfg, path-specific tests
    // llvm::raw_ostream *cfg_tc_stream;
    // llvm::raw_ostream *paths_tc_stream;
    llvm::raw_ostream *error_tc_stream = nullptr;
    llvm::raw_ostream *all_tc_stream = nullptr;

    using chrono_clock = std::chrono::system_clock;
    using chrono_time_point = std::chrono::time_point<chrono_clock>;
    using chrono_duration = std::chrono::duration<double>;
    chrono_time_point start_time_stamp;

    sigc::connection on_state_kill;

public:
    Chef(S2E *s2e) : Plugin(s2e) {
    }

    virtual ~Chef() {
        delete error_tc_stream;
        delete all_tc_stream;
    }

    void initialize();

    sigc::signal<void, S2EExecutionState *, HighLevelInstruction> on_hlpc_update;

private:
    void startSession(S2EExecutionState *state);
    void endSession(S2EExecutionState *state, bool error_happened);

    // Allow the guest to communicate with this plugin using s2e_invoke_plugin
    virtual void handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize);

    bool isAtState(ChefStatus targetStatus, S2EExecutionState *state, bool warn = true);

    void writeSimpleTestCase(llvm::raw_ostream &os, const ConcreteInputs &inputs);

    void dumpTestCase(S2EExecutionState *state, llvm::raw_ostream &out);

    void doUpdateHLPC(S2EExecutionState *state, HighLevelInstruction instruction);

    void onStateKill(S2EExecutionState *state);
};
} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_CHEF_H