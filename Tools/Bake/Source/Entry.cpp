#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Core/Config.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" void app_main(void);

namespace pip3D
{
    namespace Bake
    {
        void progressBarPanic() noexcept;
    }
}

#if defined(_WIN32)
static void initConsole()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE)
    {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode))
        {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
}

static LONG WINAPI vectoredHandler(EXCEPTION_POINTERS *ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006u || code == 0x4001000Au || code == 0xE06D7363u)
        return EXCEPTION_CONTINUE_SEARCH;

    pip3D::Bake::progressBarPanic();
    void *frames[62];
    const USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
    const HMODULE exeBase = GetModuleHandleW(nullptr);
    std::fprintf(stderr, "\n\033[91m[-] Crash detected! code=0x%08lX addr=%p frames=%u exeBase=%p\033[0m\n",
                 static_cast<unsigned long>(code),
                 ep->ExceptionRecord->ExceptionAddress,
                 static_cast<unsigned>(n), exeBase);
    for (USHORT i = 0; i < n && i < 24; ++i)
        std::fprintf(stderr, "  [%02u] %p (rva 0x%llx)\n", static_cast<unsigned>(i),
                     frames[i],
                     static_cast<unsigned long long>(
                         reinterpret_cast<uintptr_t>(frames[i]) -
                         reinterpret_cast<uintptr_t>(exeBase)));
    std::fflush(stderr);
    std::fputs("\033[?25h", stdout);
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#if defined(_WIN32)
    initConsole();
    void *veh = AddVectoredExceptionHandler(1, vectoredHandler);
#endif

    if (!std::getenv("PIP3D_BAKE_LOG"))
    {
#if defined(_WIN32)
        std::freopen("NUL", "w", stdout);
#else
        std::freopen("/dev/null", "w", stdout);
#endif
    }

    app_main();
#if defined(_WIN32)
    if (veh)
        RemoveVectoredExceptionHandler(veh);

    std::fputs("\033[?25h", stdout);
    std::fflush(stdout);
#endif
    return pip3D::Bake::g_bakeResult;
}