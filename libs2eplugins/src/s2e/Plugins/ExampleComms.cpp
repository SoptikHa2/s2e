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

#include "ExampleComms.h"

namespace s2e {
namespace plugins {

namespace {

//
// This class can optionally be used to store per-state plugin data.
//
// Use it as follows:
// void ExampleComms::onEvent(S2EExecutionState *state, ...) {
//     DECLARE_PLUGINSTATE(ExampleCommsState, state);
//     plgState->...
// }
//
class ExampleCommsState: public PluginState {
    // Declare any methods and fields you need here

public:
    static PluginState *factory(Plugin *p, S2EExecutionState *s) {
        return new ExampleCommsState();
    }

    virtual ~ExampleCommsState() {
        // Destroy any object if needed
    }

    virtual ExampleCommsState *clone() const {
        return new ExampleCommsState(*this);
    }
};

}

S2E_DEFINE_PLUGIN(ExampleComms, "Describe what the plugin does here", "", );

void ExampleComms::initialize() {

}



void ExampleComms::handleOpcodeInvocation(S2EExecutionState *state, uint64_t guestDataPtr, uint64_t guestDataSize)
{
    S2E_EXAMPLECOMMS_COMMAND command;

    if (guestDataSize != sizeof(command)) {
        getWarningsStream(state) << "mismatched S2E_EXAMPLECOMMS_COMMAND size\n";
        return;
    }

    if (!state->mem()->read(guestDataPtr, &command, guestDataSize)) {
        getWarningsStream(state) << "could not read transmitted data\n";
        return;
    }

    switch (command.Command) {
        // TODO: add custom commands here
        case COMMAND_1:
            getInfoStream(state) << "Received command: value " << command.param << "\n";
            break;
        default:
            getWarningsStream(state) << "Unknown command " << command.Command << "\n";
            break;
    }
}



} // namespace plugins
} // namespace s2e