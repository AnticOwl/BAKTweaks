#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstddef>

namespace {
using RequestPoseProbeFn = BOOL(WINAPI*)();
HMODULE g_module = nullptr;

void WriteResult(const wchar_t* message) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"BAKPoseProbeLauncher.log");
    FILE* file{};
    if (_wfopen_s(&file, path, L"w") != 0 || !file) return;
    fwprintf(file, L"%s\n", message);
    fclose(file);
}

DWORD WINAPI RequestAndUnload(void*) {
    HMODULE tweaks = GetModuleHandleW(L"BAKTweaks.dll");
    auto request = tweaks
        ? reinterpret_cast<RequestPoseProbeFn>(GetProcAddress(tweaks, "BAKTweaks_RequestPoseProbe"))
        : nullptr;
    if (!request) {
        WriteResult(L"FAILED: BAKTweaks_RequestPoseProbe export unavailable; load BAKTweaks v5.32 or newer.");
    } else if (!request()) {
        WriteResult(L"FAILED: BAKTweaks rejected the pose probe request.");
    } else {
        WriteResult(L"REQUESTED: probe queued in BAKTweaks; see BAKTweaks.log for the game-thread result.");
    }

    // This launcher installs no hooks and owns no long-lived callbacks. Once
    // the request has been queued in BAKTweaks, it can safely unload itself.
    FreeLibraryAndExitThread(g_module, 0);
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE worker = CreateThread(nullptr, 0, RequestAndUnload, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
