#include "utils/CrashHandler.h"

#include "log/Log.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#ifdef _WIN32
#include <windows.h>
// dbghelp after windows.h, which it depends on.
#include <dbghelp.h>
#endif

namespace engine::crash
{
namespace
{
std::string sChannel;

// Written to stderr as well as the log, because the log's own machinery is a
// suspect at the moment of a crash and stderr is not.
void report(const char* what, const std::string& detail)
{
    const std::string trace = stackTrace(/*skip_frames=*/2);
    std::fprintf(stderr, "\n=== CRASH: %s%s ===\n%s\n", what, detail.c_str(), trace.c_str());
    std::fflush(stderr);
    if (log::Channel* channel = log::Channel::find(sChannel))
    {
        channel->error("CRASH: {}{}", what, detail);
        channel->error("{}", trace);
    }
}

#ifdef _WIN32
const char* faultName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation -- a pointer into nothing";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "index past the end of an array";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow -- runaway recursion";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    default:
        return "fatal exception";
    }
}

LONG WINAPI onFault(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    char where[64] = {};
    std::snprintf(where, sizeof(where), " (code 0x%08lX at %p)", static_cast<unsigned long>(code),
                  info->ExceptionRecord->ExceptionAddress);
    report(faultName(code), where);
    return EXCEPTION_EXECUTE_HANDLER; // let it die, but die having said so
}
#endif

void onSignal(int sig)
{
    const char* what = sig == SIGSEGV   ? "segmentation fault"
                       : sig == SIGABRT ? "abort -- a failed assert or an uncaught throw"
                       : sig == SIGFPE  ? "arithmetic fault"
                       : sig == SIGILL  ? "illegal instruction"
                                        : "fatal signal";
    report(what, {});
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

// The end of an exception nobody caught: the type and message are still
// reachable here, and they are usually the whole answer.
void onTerminate()
{
    std::string detail;
    if (const std::exception_ptr live = std::current_exception())
    {
        try
        {
            std::rethrow_exception(live);
        }
        catch (const std::exception& e)
        {
            detail = std::string(" -- ") + e.what();
        }
        catch (...)
        {
            detail = " -- a thrown thing that is not a std::exception";
        }
    }
    report("terminate", detail);
    std::abort();
}
} // namespace

std::string stackTrace(int skip_frames)
{
#ifdef _WIN32
    constexpr int kMaxFrames = 48;
    void* frames[kMaxFrames] = {};
    const HANDLE process = GetCurrentProcess();
    static bool sSymbols = false;
    if (!sSymbols)
    {
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        // FALSE, not TRUE: invading the process walks every loaded module up front, which
        // costs seconds and is paid at the exact moment the process is already dying. Names
        // resolve per-address instead, and the module+offset a trace actually needs comes from
        // the loader regardless.
        sSymbols = SymInitialize(process, nullptr, FALSE) != FALSE;
    }
    const USHORT captured =
        CaptureStackBackTrace(static_cast<DWORD>(skip_frames + 1), kMaxFrames, frames, nullptr);

    // A symbol record carries its name inline past the end of the struct, so it
    // is allocated with room rather than declared.
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;

    std::string out;
    for (USHORT i = 0; i < captured; ++i)
    {
        const auto address = reinterpret_cast<DWORD64>(frames[i]);
        char line[768] = {};
        DWORD64 offset = 0;
        const bool named = sSymbols && SymFromAddr(process, address, &offset, symbol) != FALSE;

        // The MODULE-RELATIVE offset is the part of a frame that always
        // survives. This toolchain emits DWARF, which the platform symbol
        // server cannot read, so a NAME only appears for exported frames --
        // the offset is what turns back into file:line afterwards. It comes
        // from the LOADER rather than the symbol server for the same reason:
        // where a module sits in memory is known even when nothing about its
        // contents is.
        HMODULE module = nullptr;
        const char* module_name = "?";
        char module_path[MAX_PATH] = {};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]), &module) != FALSE &&
            module != nullptr && GetModuleFileNameA(module, module_path, MAX_PATH) > 0)
        {
            const char* slash = std::strrchr(module_path, '\\');
            module_name = slash != nullptr ? slash + 1 : module_path;
        }
        if (module != nullptr)
            std::snprintf(
                line, sizeof(line), "  #%02d %-28s %s+0x%llx\n", i, named ? symbol->Name : "??",
                module_name,
                static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
        else
            std::snprintf(line, sizeof(line), "  #%02d %-28s [%p]\n", i,
                          named ? symbol->Name : "??", frames[i]);
        out += line;
    }
    return out.empty() ? std::string("  (no frames -- the stack itself is gone)") : out;
#else
    (void)skip_frames;
    return "  (no stack walker on this platform)";
#endif
}

void install(const std::string& log_channel)
{
    sChannel = log_channel;
#ifdef _WIN32
    SetUnhandledExceptionFilter(&onFault);
#endif
    std::set_terminate(&onTerminate);
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL})
        std::signal(sig, &onSignal);
    if (log::Channel* channel = log::Channel::find(sChannel))
        channel->info("crash handler armed -- an offset in a trace resolves with: "
                      "addr2line -e <exe> -f -C -i +0x<offset>");
}

} // namespace engine::crash
