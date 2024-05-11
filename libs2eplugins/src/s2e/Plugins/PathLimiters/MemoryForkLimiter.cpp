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

#include <unistd.h>
#include "sys/types.h"
#include "sys/sysinfo.h"

#include <s2e/S2E.h>
#include <s2e/ConfigFile.h>

#include "MemoryForkLimiter.h"

namespace s2e {
namespace plugins {

S2E_DEFINE_PLUGIN(MemoryForkLimiter, "Limits process and state forking when under high memory pressure", "", "ModuleExecutionDetector");

void MemoryForkLimiter::initialize() {
    ModuleExecutionDetector* detector = s2e()->getPlugin<ModuleExecutionDetector>();

    s2e()->getCorePlugin()->onTimer.connect(sigc::mem_fun(*this, &MemoryForkLimiter::onTimer));

    s2e()->getCorePlugin()->onProcessForkDecide.connect(sigc::mem_fun(*this, &MemoryForkLimiter::onProcessForkDecide));

    // Limit new state and process spawning, -1 == dont care
    bool ok = false;

    maxMemoryUseBytes = s2e()->getConfig()->getInt(getConfigKey() + ".maxMemoryUseBytes", -1, &ok);
    if (!ok || maxMemoryUseBytes < 0) {
        maxMemoryUseBytes = {};
    }

    ok = false;
    maxGlobalMemoryUseThousands = s2e()->getConfig()->getInt(getConfigKey() + ".maxGlobalMemUse", -1, &ok);
    if (!ok || maxGlobalMemoryUseThousands < 0) {
        maxGlobalMemoryUseThousands = {};
    }

    if (!maxMemoryUseBytes && !maxGlobalMemoryUseThousands) {
        getWarningsStream() << "No memory limits configured\n";
        exit(-1);
    }

    if (detector) {
        s2e()->getCorePlugin()->onStateForkDecide.connect(sigc::mem_fun(*this, &MemoryForkLimiter::onStateForkDecide));
    } else {
        getWarningsStream() << "MemoryForkLimiter requires ModuleExecutionDetector\n";
        exit(-1);
    }

    memorySizeBytes = getTotalSystemMemory();
}

void MemoryForkLimiter::onTimer() {
    currentMemoryUseBytes = getSelfMemoryUsage();
    currentGlobalMemoryUseBytes = getGlobalMemoryUsage();

    s2e()->getDebugStream() << "Memory usage: " << currentMemoryUseBytes << " bytes\n";
    s2e()->getDebugStream() << "Global memory usage: " << currentGlobalMemoryUseBytes << " bytes\n";
    s2e()->getDebugStream() << "Max memory usage: " << memorySizeBytes << " bytes\n";
}

void MemoryForkLimiter::onStateForkDecide(S2EExecutionState *state, const klee::ref<klee::Expr> &condition, bool &allowForking) {
    if (!canCreateNewStates()) allowForking = false;
}
void MemoryForkLimiter::onProcessForkDecide(bool *proceed) {
    if (!canCreateNewStates()) *proceed = false;
}

int64_t MemoryForkLimiter::getTotalSystemMemory() {
    // Source: https://stackoverflow.com/a/64166/6792871
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    int64_t totalPhysMem = memInfo.totalram;
    totalPhysMem *= memInfo.mem_unit;

    return totalPhysMem;
}

int64_t MemoryForkLimiter::getSelfMemoryUsage() {
    // Source: https://github.com/lemire/Code-used-on-Daniel-Lemire-s-blog/blob/master/2022/12/12/memu.cpp#L82
    long rss = 0L;
    FILE *fp = NULL;
    if ((fp = fopen("/proc/self/statm", "r")) == NULL)
        return (size_t)0L; /* Can't open? */
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return (size_t)0L; /* Can't read? */
    }
    fclose(fp);
    return (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
}

int64_t s2e::plugins::MemoryForkLimiter::getGlobalMemoryUsage() {
    // Source: https://stackoverflow.com/a/64166/6792871
    struct sysinfo memInfo;

    sysinfo (&memInfo);
    int64_t availablePhysMem = memInfo.totalram - memInfo.freeram;
    availablePhysMem *= memInfo.mem_unit;

    return availablePhysMem;

}
bool MemoryForkLimiter::canCreateNewStates() {
    if (maxMemoryUseBytes && currentMemoryUseBytes > *maxMemoryUseBytes) {
        if (!warnedAboutMemUsage) {
            s2e()->getWarningsStream() << "Memory limit exceeded, will not create new states\n";
            warnedAboutMemUsage = true;
        }
        return false;
    }

    int64_t currentMemUseThousands = (currentGlobalMemoryUseBytes * 1000) / memorySizeBytes;
    if (maxGlobalMemoryUseThousands && currentMemUseThousands > *maxGlobalMemoryUseThousands) {
        if (!warnedAboutGlobalMemUsage) {
            s2e()->getWarningsStream() << "Global memory limit exceeded, will not create new states\n";
            warnedAboutGlobalMemUsage = true;
        }
        return false;
    }

    return true;
}

} // namespace plugins
} // namespace s2e