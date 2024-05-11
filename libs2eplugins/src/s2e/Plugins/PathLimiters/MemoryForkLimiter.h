///
/// Copyright (C) 2024, Petr Stastny
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

#ifndef S2E_PLUGINS_MEMORYFORKLIMITER_H
#define S2E_PLUGINS_MEMORYFORKLIMITER_H

#include <optional>

#include <s2e/Plugin.h>

#include <s2e/Plugins/Core/BaseInstructions.h>
#include <s2e/Plugins/OSMonitors/Support/ModuleExecutionDetector.h>


namespace s2e {
namespace plugins {

class MemoryForkLimiter : public Plugin {
    S2E_PLUGIN

    /// When memory use of S2E is over given amount of bytes, no new states nor processes are allowed to be created.
    std::optional<int64_t> maxMemoryUseBytes;

    /// When global memory use exceeds this amount (thousands, so 1000 = 100%, 850 = 85%, etc), no new states nor processes are allowed to be created.
    std::optional<int64_t> maxGlobalMemoryUseThousands;

    int64_t currentMemoryUseBytes = 0;
    int64_t currentGlobalMemoryUseBytes = 0;
    int64_t memorySizeBytes = 0;

    bool warnedAboutMemUsage = false;
    bool warnedAboutGlobalMemUsage = false;

public:
    MemoryForkLimiter(S2E *s2e) : Plugin(s2e) {
    }

    void initialize();

private:
    void onTimer();
    void onStateForkDecide(S2EExecutionState *state, const klee::ref<klee::Expr> &condition, bool &allowForking);
    void onProcessForkDecide(bool *proceed);

    bool canCreateNewStates();

    int64_t getTotalSystemMemory();
    int64_t getSelfMemoryUsage();
    int64_t getGlobalMemoryUsage();
};
} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_MEMORYFORKLIMITER_H