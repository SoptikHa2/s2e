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


namespace s2e {
namespace plugins {


enum S2E_CONCOLICSESSION_COMMANDS {
    // TODO: customize list of commands here
    COMMAND_1
};

struct S2E_CONCOLICSESSION_COMMAND {
    S2E_CONCOLICSESSION_COMMANDS Command;
    union {
        // Command parameters go here
        uint64_t param;
    };
};



class ConcolicSession : public Plugin, public IPluginInvoker {

    S2E_PLUGIN
public:
    ConcolicSession(S2E *s2e) : Plugin(s2e) {
    }

    void initialize();

private:


    // Allow the guest to communicate with this plugin using s2e_invoke_plugin
    virtual void handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize);

};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_CONCOLICSESSION_H