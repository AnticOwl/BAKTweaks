#include <windows.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr int kGameLabel = 1001;
constexpr int kDllCombo = 1002;
constexpr int kInjectButton = 1003;
constexpr int kRefreshButton = 1004;
constexpr int kStatusLabel = 1005;

struct GameInfo {
    DWORD pid{};
    std::wstring path;
};
struct DllChoice {
    std::wstring label;
    std::wstring path;
    std::wstring moduleName;
    bool selfUnloading{};
};

HINSTANCE g_instance{};
HWND g_gameLabel{}, g_dllCombo{}, g_statusLabel{};
HFONT g_font{};
std::vector<GameInfo> g_games;
std::vector<DllChoice> g_choices;
std::wstring g_exeDirectory;

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

std::wstring GetExecutableDirectory() {
    wchar_t path[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!n || n >= std::size(path)) return {};
    std::wstring result(path, n);
    const size_t slash = result.find_last_of(L"\\/");
    if (slash != std::wstring::npos) result.resize(slash);
    return result;
}

bool IsArkhamKnightExe(const std::wstring& path) {
    const std::wstring lower = Lower(path);
    if (lower.size() < 12 || lower.substr(lower.size() - 12) != L"batmanak.exe") return false;
    return lower.find(L"\\binaries\\win64\\") != std::wstring::npos &&
        (lower.find(L"arkham knight") != std::wstring::npos || lower.find(L"arkhamknight") != std::wstring::npos);
}

std::vector<GameInfo> FindGames() {
    std::vector<GameInfo> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"BatmanAK.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            wchar_t path[32768]{};
            DWORD size = static_cast<DWORD>(std::size(path));
            const BOOL ok = QueryFullProcessImageNameW(process, 0, path, &size);
            CloseHandle(process);
            if (ok && IsArkhamKnightExe(std::wstring(path, size)))
                found.push_back({entry.th32ProcessID, std::wstring(path, size)});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void SetStatus(const std::wstring& message) {
    if (g_statusLabel) SetWindowTextW(g_statusLabel, message.c_str());
}

void UpdateGameList() {
    g_games = FindGames();
    if (g_games.empty()) {
        SetWindowTextW(g_gameLabel, L"Jeu : Batman: Arkham Knight introuvable. Lance le jeu et actualise.");
        return;
    }
    if (g_games.size() > 1) {
        SetWindowTextW(g_gameLabel, L"Jeu : plusieurs instances détectées. Ferme les doublons avant injection.");
        return;
    }
    const std::wstring text = L"Jeu prêt — PID " + std::to_wstring(g_games[0].pid) + L"\r\n" + g_games[0].path;
    SetWindowTextW(g_gameLabel, text.c_str());
}

bool IsAllowedChoiceName(const std::wstring& name) {
    return _wcsicmp(name.c_str(), L"BAKTweaks.dll") == 0 ||
        _wcsicmp(name.c_str(), L"BAKPoseProbeLauncher.dll") == 0;
}

void LoadChoices() {
    g_choices.clear();
    const std::wstring mainPath = g_exeDirectory + L"\\BAKTweaks.dll";
    const std::wstring probePath = g_exeDirectory + L"\\BAKPoseProbeLauncher.dll";
    if (GetFileAttributesW(mainPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        g_choices.push_back({L"BAK Tweaks — pose tooling et fonctionnalités", mainPath, L"BAKTweaks.dll", false});
    if (GetFileAttributesW(probePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        g_choices.push_back({L"Pose probe — lecture ponctuelle, se décharge seul", probePath, L"BAKPoseProbeLauncher.dll", true});

    SendMessageW(g_dllCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& choice : g_choices)
        SendMessageW(g_dllCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.c_str()));
    if (!g_choices.empty()) {
        const int mainIndex = 0;
        SendMessageW(g_dllCombo, CB_SETCURSEL, mainIndex, 0);
    }
    EnableWindow(g_dllCombo, !g_choices.empty());
    EnableWindow(GetDlgItem(GetParent(g_dllCombo), kInjectButton), !g_choices.empty());
    if (g_choices.empty()) SetStatus(L"Place BAKInjector.exe beside BAKTweaks.dll (and optionally BAKPoseProbeLauncher.dll).");
    else SetStatus(L"Sélectionne une DLL puis injecte-la dans le jeu détecté.");
}

bool ModuleNameAlreadyLoaded(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    bool loaded = false;
    if (Module32FirstW(snapshot, &module)) {
        do {
            if (_wcsicmp(module.szModule, moduleName.c_str()) == 0) { loaded = true; break; }
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return loaded;
}

bool ResolveRemoteLoadLibrary(HANDLE process, DWORD pid, LPTHREAD_START_ROUTINE& remoteFunction) {
    remoteFunction = nullptr;
    HMODULE localKernel = GetModuleHandleW(L"kernel32.dll");
    FARPROC localAddress = localKernel ? GetProcAddress(localKernel, "LoadLibraryW") : nullptr;
    if (!localAddress) return false;

    HANDLE localSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    HANDLE remoteSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (localSnapshot == INVALID_HANDLE_VALUE || remoteSnapshot == INVALID_HANDLE_VALUE) {
        if (localSnapshot != INVALID_HANDLE_VALUE) CloseHandle(localSnapshot);
        if (remoteSnapshot != INVALID_HANDLE_VALUE) CloseHandle(remoteSnapshot);
        return false;
    }
    MODULEENTRY32W localModule{}; localModule.dwSize = sizeof(localModule);
    MODULEENTRY32W remoteModule{}; remoteModule.dwSize = sizeof(remoteModule);
    uintptr_t ownerBase = 0; DWORD ownerSize = 0; std::wstring ownerName;
    const uintptr_t address = reinterpret_cast<uintptr_t>(localAddress);
    if (Module32FirstW(localSnapshot, &localModule)) {
        do {
            const uintptr_t base = reinterpret_cast<uintptr_t>(localModule.modBaseAddr);
            if (address >= base && address < base + localModule.modBaseSize) {
                ownerBase = base; ownerSize = localModule.modBaseSize; ownerName = localModule.szModule; break;
            }
        } while (Module32NextW(localSnapshot, &localModule));
    }
    uintptr_t remoteBase = 0;
    if (ownerBase && Module32FirstW(remoteSnapshot, &remoteModule)) {
        do {
            if (_wcsicmp(remoteModule.szModule, ownerName.c_str()) == 0) {
                remoteBase = reinterpret_cast<uintptr_t>(remoteModule.modBaseAddr);
                if (remoteModule.modBaseSize != ownerSize) remoteBase = 0;
                break;
            }
        } while (Module32NextW(remoteSnapshot, &remoteModule));
    }
    CloseHandle(localSnapshot);
    CloseHandle(remoteSnapshot);
    if (!remoteBase) return false;
    const uintptr_t offset = address - ownerBase;
    remoteFunction = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase + offset);
    return true;
}

void InjectSelected() {
    if (g_games.size() != 1) { SetStatus(L"Un seul BatmanAK.exe doit être détecté avant l’injection."); return; }
    const LRESULT index = SendMessageW(g_dllCombo, CB_GETCURSEL, 0, 0);
    if (index < 0 || static_cast<size_t>(index) >= g_choices.size()) { SetStatus(L"Aucune DLL valide n’est sélectionnée."); return; }
    const DllChoice choice = g_choices[static_cast<size_t>(index)];
    if (!IsAllowedChoiceName(choice.moduleName)) {
        SetStatus(L"Cette DLL n’est pas autorisée par cet injecteur.");
        return;
    }
    if (ModuleNameAlreadyLoaded(g_games[0].pid, choice.moduleName)) {
        SetStatus(L"Cette DLL est déjà chargée. Redémarre le jeu avant de la remplacer.");
        return;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | SYNCHRONIZE, FALSE, g_games[0].pid);
    if (!process) { SetStatus(L"OpenProcess a échoué. Relance BAKInjector.exe et accepte l’élévation UAC."); return; }

    const DWORD attributes = GetFileAttributesW(choice.path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(process); SetStatus(L"DLL introuvable dans le dossier de l’injecteur."); return;
    }
    LPTHREAD_START_ROUTINE remoteLoad = nullptr;
    if (!ResolveRemoteLoadLibrary(process, g_games[0].pid, remoteLoad)) {
        CloseHandle(process); SetStatus(L"Impossible de résoudre LoadLibraryW dans le jeu."); return;
    }

    const SIZE_T bytes = (choice.path.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) { CloseHandle(process); SetStatus(L"Allocation du chemin distant impossible."); return; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remotePath, choice.path.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE); CloseHandle(process);
        SetStatus(L"Écriture du chemin distant impossible."); return;
    }
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, remoteLoad, remotePath, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, remotePath, 0, MEM_RELEASE); CloseHandle(process);
        SetStatus(L"CreateRemoteThread a échoué."); return;
    }
    const DWORD wait = WaitForSingleObject(thread, 10000);
    DWORD moduleResult = 0;
    if (wait == WAIT_OBJECT_0) GetExitCodeThread(thread, &moduleResult);
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(process);

    if (wait == WAIT_TIMEOUT) {
        SetStatus(L"Le thread de chargement n’a pas terminé en 10 s. N’essaie pas une seconde injection; vérifie le jeu.");
    } else if (wait != WAIT_OBJECT_0 || moduleResult == 0) {
        SetStatus(L"Le chargement de la DLL a échoué. Consulte BAKTweaks.log.");
    } else if (choice.selfUnloading) {
        SetStatus(L"Pose probe lancé. Il se décharge automatiquement après sa requête; consulte les journaux.");
    } else {
        SetStatus(L"BAK Tweaks chargé. Le journal est dans le dossier Release.");
    }
}

void ApplyFont(HWND control) {
    if (control && g_font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_gameLabel = CreateWindowExW(0, L"STATIC", L"Jeu : recherche...", WS_CHILD | WS_VISIBLE,
            16, 16, 584, 48, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGameLabel)), g_instance, nullptr);
        CreateWindowExW(0, L"STATIC", L"DLL du projet", WS_CHILD | WS_VISIBLE,
            16, 78, 584, 22, window, nullptr, g_instance, nullptr);
        g_dllCombo = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
            16, 102, 584, 140, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDllCombo)), g_instance, nullptr);
        HWND inject = CreateWindowExW(0, L"BUTTON", L"Injecter", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            16, 150, 150, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInjectButton)), g_instance, nullptr);
        HWND refresh = CreateWindowExW(0, L"BUTTON", L"Actualiser le jeu", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            174, 150, 150, 32, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRefreshButton)), g_instance, nullptr);
        g_statusLabel = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            16, 198, 584, 58, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabel)), g_instance, nullptr);
        ApplyFont(g_gameLabel); ApplyFont(g_dllCombo); ApplyFont(inject); ApplyFont(refresh); ApplyFont(g_statusLabel);
        for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ApplyFont(child);
        LoadChoices();
        UpdateGameList();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kInjectButton: InjectSelected(); return 0;
        case kRefreshButton: UpdateGameList(); return 0;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        if (g_font) { DeleteObject(g_font); g_font = nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    g_exeDirectory = GetExecutableDirectory();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    SetProcessDPIAware();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc); wc.hInstance = instance; wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"BAKInjectorWindow"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc)) return 1;
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"BAK Tweaks Injector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        636, 308, nullptr, nullptr, instance, nullptr);
    if (!window) return 2;
    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}
