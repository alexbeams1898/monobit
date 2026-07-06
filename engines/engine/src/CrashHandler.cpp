#include "CrashHandler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// DbgHelp must come AFTER windows.h. Pragma comment links dbghelp.lib
// automatically under MSVC; under MinGW the link comes from
// CMakeLists.txt's target_link_libraries(... dbghelp).
#include <dbghelp.h>
#endif

namespace engine
{

namespace
{

// Module-state. All pre-allocated to be signal/SEH-safe -- inside a
// crash handler, heap allocation is unreliable (heap may be the thing
// that crashed). Buffers are sized generously; truncation is logged
// in the dump itself.
constexpr std::size_t kContextBufferSize = 512;
constexpr std::size_t kDumpBufferSize = 64 * 1024;
constexpr std::size_t kStackFrameCount = 64;

std::atomic<bool> sInstalled{false};
std::atomic<bool> sInHandler{false}; // re-entrancy guard

SaveOnCrashFn sSaveCb;
TeardownOnCrashFn sTeardownCb;
std::mutex sCallbackMutex; // protects sSaveCb / sTeardownCb swaps

std::string sSaveDir = "./crashes";
char sContextBuffer[kContextBufferSize] = {0};
std::mutex sContextMutex;

std::string sLastCrashSummary;
bool sHadPreviousCrash = false;

// Pre-allocated dump buffer reused across crash dumps. snprintf'd
// into; flushed to disk in one write. Avoids any heap activity from
// inside the handler.
char sDumpBuffer[kDumpBufferSize];

void buildTimestampString(char* out, std::size_t out_size)
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::strftime(out, out_size, "%Y%m%d-%H%M%S", &tm_buf);
}

#ifdef _WIN32

// Snapshot the current stack into the dump buffer. Best-effort: in
// release builds without PDB symbols we still get module + offset,
// which is enough to map back via the build artifacts. Symbol lookup
// uses DbgHelp; calls are NOT thread-safe per the DbgHelp docs but
// we serialize all crash handling via sInHandler.
int captureStackTrace(char* out, int out_size, CONTEXT* ctx)
{
    void* frames[kStackFrameCount] = {nullptr};
    const USHORT count = CaptureStackBackTrace(0, kStackFrameCount, frames, nullptr);

    HANDLE process = GetCurrentProcess();
    static bool s_syminit = false;
    if (!s_syminit)
    {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(process, nullptr, TRUE);
        s_syminit = true;
    }
    (void)ctx; // unused -- CaptureStackBackTrace gives us frames directly

    // SYMBOL_INFO requires extra trailing space for the name.
    constexpr DWORD kMaxNameLen = 256;
    alignas(SYMBOL_INFO) char sym_buf[sizeof(SYMBOL_INFO) + kMaxNameLen];
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = kMaxNameLen;

    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    int written = 0;
    for (USHORT i = 0; i < count && written < out_size - 256; ++i)
    {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 displ_sym = 0;
        DWORD displ_line = 0;
        const BOOL got_sym = SymFromAddr(process, addr, &displ_sym, sym);
        const BOOL got_line = SymGetLineFromAddr64(process, addr, &displ_line, &line);
        if (got_sym && got_line)
        {
            written += std::snprintf(out + written, out_size - written,
                                     "  #%-2d 0x%016llx %s + 0x%llx  (%s:%lu)\n", i, addr,
                                     sym->Name, displ_sym, line.FileName, line.LineNumber);
        }
        else if (got_sym)
        {
            written +=
                std::snprintf(out + written, out_size - written, "  #%-2d 0x%016llx %s + 0x%llx\n",
                              i, addr, sym->Name, displ_sym);
        }
        else
        {
            HMODULE mod = nullptr;
            char modname[MAX_PATH] = {0};
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(addr), &mod))
            {
                GetModuleFileNameA(mod, modname, sizeof(modname));
            }
            written += std::snprintf(out + written, out_size - written, "  #%-2d 0x%016llx  (%s)\n",
                                     i, addr, modname[0] ? modname : "?");
        }
    }
    return written;
}

const char* sehCodeToString(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "ACCESS_VIOLATION";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_BREAKPOINT:
        return "BREAKPOINT";
    case EXCEPTION_SINGLE_STEP:
        return "SINGLE_STEP";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_FLT_DENORMAL_OPERAND:
        return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:
        return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:
        return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:
        return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:
        return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:
        return "FLT_UNDERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
        return "INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_STACK_OVERFLOW:
        return "STACK_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:
        return "INVALID_DISPOSITION";
    case EXCEPTION_GUARD_PAGE:
        return "GUARD_PAGE";
    case EXCEPTION_INVALID_HANDLE:
        return "INVALID_HANDLE";
    default:
        return "UNKNOWN";
    }
}

#endif // _WIN32

// Write the dump buffer to a timestamped file under sSaveDir.
// Returns the path written (or empty on failure). Best-effort -- if
// the disk is full / dir doesn't exist / write fails, we proceed
// without the file but still attempt save + teardown.
std::string flushDumpToDisk(const char* dump_text, std::size_t len, const char* timestamp)
{
    std::error_code ec;
    std::filesystem::create_directories(sSaveDir, ec);
    if (ec)
        return {};
    char path_buf[512];
    std::snprintf(path_buf, sizeof(path_buf), "%s/crash-%s.log", sSaveDir.c_str(), timestamp);
    std::FILE* f = std::fopen(path_buf, "wb");
    if (f == nullptr)
        return {};
    std::fwrite(dump_text, 1, len, f);
    std::fclose(f);
    return std::string(path_buf);
}

void writeLastCrashSummary(const char* summary)
{
    std::error_code ec;
    std::filesystem::create_directories(sSaveDir, ec);
    if (ec)
        return;
    const std::string path = sSaveDir + "/last_crash_summary.txt";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    std::fwrite(summary, 1, std::strlen(summary), f);
    std::fclose(f);
}

void loadLastCrashSummary()
{
    const std::string path = sSaveDir + "/last_crash_summary.txt";
    std::ifstream f(path);
    if (!f.is_open())
    {
        sHadPreviousCrash = false;
        sLastCrashSummary.clear();
        return;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    sLastCrashSummary = std::move(content);
    sHadPreviousCrash = !sLastCrashSummary.empty();
}

// Core crash-handling work: build dump, write to disk, log to stderr,
// invoke emergency-save + teardown callbacks. Reentrancy-guarded: a
// crash INSIDE this function logs a double-fault line and aborts to
// avoid infinite recursion.
//
// Returns true if the handler ran to completion (caller may safely
// continue execution -- typically into the next main-loop iteration).
// Returns false on reentrancy / abort path (caller should not be
// reached; abort fires inside).
//
// Replaces the older longjmp-based handleCrash -- longjmp from SEH
// proved unreliable on MinGW (corrupted SEH dispatcher state, no
// destructor unwinding, RAII leaks). The new model: __try/__except
// in the main-loop body catches the SEH cleanly; we just do the
// dump+save+teardown work and return; the caller's __except block
// hands control back to the loop's `continue`.
bool runCrashHandlerCore(const char* reason, void* seh_context)
{
    // Reentrancy guard -- a crash inside the handler means abort.
    bool expected = false;
    if (!sInHandler.compare_exchange_strong(expected, true))
    {
        std::fprintf(stderr,
                     "\n[crash-handler] DOUBLE-FAULT: crash inside crash handler "
                     "(secondary reason: %s) -- aborting to avoid infinite recursion\n",
                     reason ? reason : "(unknown)");
        std::fflush(stderr);
        std::abort();
    }

    char ts[32];
    buildTimestampString(ts, sizeof(ts));

    std::string context_copy;
    {
        std::lock_guard<std::mutex> lock(sContextMutex);
        context_copy = sContextBuffer;
    }

    int written = 0;
    written += std::snprintf(sDumpBuffer + written, kDumpBufferSize - written,
                             "=== Selva Oscura crash dump ===\n"
                             "Time:    %s\n"
                             "Reason:  %s\n"
                             "Context: %s\n"
                             "\n--- Stack trace ---\n",
                             ts, reason ? reason : "(none)",
                             context_copy.empty() ? "(none)" : context_copy.c_str());

#ifdef _WIN32
    written += captureStackTrace(sDumpBuffer + written, kDumpBufferSize - written,
                                 static_cast<CONTEXT*>(seh_context));
#else
    (void)seh_context;
    written += std::snprintf(sDumpBuffer + written, kDumpBufferSize - written,
                             "  (stack trace not implemented on this platform)\n");
#endif

    written +=
        std::snprintf(sDumpBuffer + written, kDumpBufferSize - written, "\n=== end dump ===\n");

    const std::string dump_path = flushDumpToDisk(sDumpBuffer, written, ts);

    // One-line summary the next boot displays on the main menu.
    char summary[256];
    std::snprintf(summary, sizeof(summary), "Last run ended unexpectedly (%s). Dump: %s",
                  reason ? reason : "unknown",
                  dump_path.empty() ? "(write failed)" : dump_path.c_str());
    writeLastCrashSummary(summary);
    // Also stash in the in-memory copy so the same run's main menu
    // can show the banner without restarting.
    sLastCrashSummary = summary;
    sHadPreviousCrash = true;

    // Also log to stderr so the dev sees something immediately.
    std::fprintf(stderr, "\n[crash-handler] %s\n", summary);
    std::fprintf(stderr, "%s", sDumpBuffer);
    std::fflush(stderr);

    // Emergency save. Wrapped in its own try/catch -- a crash here
    // must NOT mask the original crash.
    SaveOnCrashFn save_cb;
    TeardownOnCrashFn teardown_cb;
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        save_cb = sSaveCb;
        teardown_cb = sTeardownCb;
    }
    if (save_cb)
    {
        try
        {
            save_cb();
        }
        catch (...)
        {
            std::fprintf(stderr, "[crash-handler] emergency save threw; skipping\n");
        }
    }
    if (teardown_cb)
    {
        try
        {
            teardown_cb();
        }
        catch (...)
        {
            std::fprintf(stderr, "[crash-handler] teardown threw; skipping\n");
        }
    }

    sInHandler.store(false);
    return true;
}

#ifdef _WIN32

// SetUnhandledExceptionFilter callback -- LAST-RESORT catch for SEH
// faults that escape every other handler in the chain (faults from
// non-main threads, init-time faults before the main loop, etc.).
// The main-loop body is wrapped in __try/__except (see Engine::run),
// which catches per-frame faults FIRST. If a fault reaches this
// filter, recovery is not possible (we don't know what frame we're
// in), so dump + exit.
LONG WINAPI sehFilter(EXCEPTION_POINTERS* info)
{
    char reason[128];
    std::snprintf(reason, sizeof(reason), "SEH %s (0x%08lx) at 0x%p (unhandled)",
                  sehCodeToString(info->ExceptionRecord->ExceptionCode),
                  info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    runCrashHandlerCore(reason, info->ContextRecord);
    // Return EXCEPTION_EXECUTE_HANDLER so the OS unwinds + exits
    // cleanly (rather than continuing the faulting instruction or
    // popping up Windows Error Reporting). Process dies; no
    // recovery from this path.
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // _WIN32

void terminateHandler()
{
    // Uncaught C++ exception -> std::terminate. Treated as unhandled:
    // dump and exit. _Exit (not exit) so we don't try to run
    // destructors from a state that already failed exception
    // safety -- destructors then could re-throw or deadlock.
    runCrashHandlerCore("std::terminate (uncaught C++ exception)", nullptr);
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#endif
    std::_Exit(1);
}

} // namespace

namespace
{

// Re-(install) our SEH + terminate handlers. Idempotent + cheap --
// the Win32 + std::set_terminate APIs are simple atomic stores at
// the OS / runtime level. Used both at boot and periodically from
// armCrashRecovery() so any third-party library that installs its
// own handler after us gets overridden back to ours within one
// frame.
//
// We DO disable Tracy's competing crash handler at compile time
// (TRACY_NO_CRASH_HANDLER=ON, see cmake/FetchDependencies.cmake)
// so Tracy specifically can't race us; the periodic re-install is
// defense-in-depth against future dependencies whose crash-handler
// installation we haven't audited.
void reinstallOsHandlers()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(sehFilter);
#endif
    std::set_terminate(terminateHandler);
}

} // namespace

void installCrashHandler(SaveOnCrashFn save_cb, TeardownOnCrashFn teardown_cb)
{
    bool expected = false;
    if (!sInstalled.compare_exchange_strong(expected, true))
    {
        std::fprintf(stderr, "[crash-handler] installCrashHandler called twice; ignoring\n");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(sCallbackMutex);
        sSaveCb = std::move(save_cb);
        sTeardownCb = std::move(teardown_cb);
    }
    reinstallOsHandlers();
    loadLastCrashSummary();
}

void setCrashRecoveryCallbacks(SaveOnCrashFn save_cb, TeardownOnCrashFn teardown_cb)
{
    std::lock_guard<std::mutex> lock(sCallbackMutex);
    sSaveCb = std::move(save_cb);
    sTeardownCb = std::move(teardown_cb);
}

void armCrashRecovery()
{
    // Re-install our OS handlers every frame as defense-in-depth.
    // Any third-party library that calls
    // SetUnhandledExceptionFilter / std::set_terminate after our
    // boot-time install would win OS-handler-chain priority and
    // route crashes around us; this one-line periodic refresh
    // reverts any such override within one frame. Cost is one
    // atomic store per frame at the OS level -- effectively free.
    reinstallOsHandlers();
}

void handleSehCrash(void* exception_pointers)
{
#ifdef _WIN32
    EXCEPTION_POINTERS* info = static_cast<EXCEPTION_POINTERS*>(exception_pointers);
    char reason[128];
    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        std::snprintf(reason, sizeof(reason), "SEH %s (0x%08lx) at 0x%p",
                      sehCodeToString(info->ExceptionRecord->ExceptionCode),
                      info->ExceptionRecord->ExceptionCode,
                      info->ExceptionRecord->ExceptionAddress);
        runCrashHandlerCore(reason, info->ContextRecord);
    }
    else
    {
        runCrashHandlerCore("SEH (no exception info)", nullptr);
    }
#else
    (void)exception_pointers;
    runCrashHandlerCore("non-Windows synthetic crash", nullptr);
#endif
}

bool hadPreviousCrash()
{
    return sHadPreviousCrash;
}

const std::string& lastCrashSummary()
{
    return sLastCrashSummary;
}

void clearLastCrashSummary()
{
    sLastCrashSummary.clear();
    sHadPreviousCrash = false;
    std::error_code ec;
    std::filesystem::remove(sSaveDir + "/last_crash_summary.txt", ec);
}

void setCrashSaveDirectory(const std::string& dir)
{
    sSaveDir = dir;
    // Re-load summary from the new dir (the initial load used the
    // default "./crashes").
    loadLastCrashSummary();
}

void setCrashContext(const std::string& context)
{
    std::lock_guard<std::mutex> lock(sContextMutex);
    const std::size_t n = std::min(context.size(), kContextBufferSize - 1);
    std::memcpy(sContextBuffer, context.data(), n);
    sContextBuffer[n] = '\0';
}

CrashContextScope::CrashContextScope(const char* context)
{
    {
        std::lock_guard<std::mutex> lock(sContextMutex);
        prev_ = sContextBuffer;
        if (context != nullptr)
        {
            const std::size_t n = std::min(std::strlen(context), kContextBufferSize - 1);
            std::memcpy(sContextBuffer, context, n);
            sContextBuffer[n] = '\0';
        }
    }
}

CrashContextScope::~CrashContextScope()
{
    std::lock_guard<std::mutex> lock(sContextMutex);
    const std::size_t n = std::min(prev_.size(), kContextBufferSize - 1);
    std::memcpy(sContextBuffer, prev_.data(), n);
    sContextBuffer[n] = '\0';
}

} // namespace engine
