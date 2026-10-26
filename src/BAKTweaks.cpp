#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <Xinput.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <cmath>
#include <array>
#include <cstddef>
#include <limits>
#include <new>
#include <cctype>
#include <unordered_map>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"
#include "MinHook.h"
#include "resource.h"
#include "PoseBoneNameMap.h"

static void HelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

extern "C" {
    std::atomic<uintptr_t> g_bakExposureLevelTarget{0};
    std::atomic<uintptr_t> g_bakExposureCurrentTarget{0};
    std::atomic<float> g_bakExposureDesired{1.0f};
    uintptr_t g_bakExposureWriterContinue{};
    void BakExposureWriterHook();
}
static_assert(sizeof(std::atomic<uintptr_t>) == 8 && sizeof(std::atomic<float>) == 4);

namespace {
using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

HMODULE g_module = nullptr;
std::atomic<bool> g_running{true};
std::atomic<bool> g_uiOpen{true};

// User-configurable shortcuts.
// Defaults preserve the current BAK Tweaks mapping.
int g_hotkeyToggleUI = VK_F10;
int g_hotkeySmallLightning = VK_F6;
int g_hotkeyVerticalLightning = VK_F7;
std::wstring g_configPath;

std::atomic<uintptr_t> g_capeBase{0};
std::atomic<uintptr_t> g_rainConsole{0};
std::atomic<uintptr_t> g_rainEngine{0};

// Exposure override (active LocalPlayer LevelPP and CurrentPP caches).
std::atomic<bool> g_exposureWanted{false};
std::atomic<bool> g_exposureApplied{false};
std::atomic<float> g_exposureValue{1.0f};
std::atomic<uintptr_t> g_exposureLocalPlayer{0};

PresentFn g_originalPresent = nullptr;
ResizeBuffersFn g_originalResizeBuffers = nullptr;

// Hard game-input block while BAK Tweaks is open.
// WndProc/WM_INPUT blocking handles the normal UE3 path; these API hooks cover
// polling paths (keyboard/mouse buttons via User32 and controller via XInput).
using GetAsyncKeyStateFn = SHORT (WINAPI*)(int);
using GetKeyStateFn = SHORT (WINAPI*)(int);
using GetKeyboardStateFn = BOOL (WINAPI*)(PBYTE);
using GetRawInputDataFn = UINT (WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
using GetRawInputBufferFn = UINT (WINAPI*)(PRAWINPUT, PUINT, UINT);
using GetCursorPosFn = BOOL (WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL (WINAPI*)(int, int);
using ClipCursorFn = BOOL (WINAPI*)(const RECT*);
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
using XInputGetKeystrokeFn = DWORD (WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);

GetAsyncKeyStateFn g_originalGetAsyncKeyState = nullptr;
GetKeyStateFn g_originalGetKeyState = nullptr;
GetKeyboardStateFn g_originalGetKeyboardState = nullptr;
GetRawInputDataFn g_originalGetRawInputData = nullptr;
GetRawInputBufferFn g_originalGetRawInputBuffer = nullptr;
GetCursorPosFn g_originalGetCursorPos = nullptr;
SetCursorPosFn g_originalSetCursorPos = nullptr;
ClipCursorFn g_originalClipCursor = nullptr;
XInputGetStateFn g_originalXInputGetState = nullptr;
XInputGetKeystrokeFn g_originalXInputGetKeystroke = nullptr;
HWND g_hwnd = nullptr;
WNDPROC g_originalWndProc = nullptr;
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11ShaderResourceView* g_backgroundSrv = nullptr;
bool g_imguiReady = false;
float g_uiScale = 1.0f;
float g_uiScaleDraft = 1.0f;
float g_lastUiScale = -1.0f;
ImGuiStyle g_baseStyle{};
bool g_baseStyleCaptured = false;
std::atomic<bool> g_presentSeen{false};
std::wstring g_logPath;

// BatLit viewport gizmo - intentionally lightweight and read-only.
bool g_showLightGizmo = true;
bool g_showGizmoRadius = true;
bool g_showGizmoAxis = true;
bool g_showGizmoCone = true;
uint32_t g_gizmoLightId = 0;

// Forward declaration: input hooks are installed before the logger definition.
void Log(const wchar_t* text);

RECT g_savedClipRect{};
bool g_savedClipValid = false;
bool g_inputCaptureActive = false;
POINT g_gameFrozenCursor{};
bool g_gameFrozenCursorValid = false;

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

SHORT WINAPI HookGetAsyncKeyState(int vKey) {
    // The configured UI-toggle key must remain available so BAK Tweaks can always be closed.
    if (g_uiOpen.load() && vKey != g_hotkeyToggleUI) return 0;
    return g_originalGetAsyncKeyState ? g_originalGetAsyncKeyState(vKey) : 0;
}

SHORT WINAPI HookGetKeyState(int vKey) {
    if (g_uiOpen.load() && vKey != g_hotkeyToggleUI) return 0;
    return g_originalGetKeyState ? g_originalGetKeyState(vKey) : 0;
}

BOOL WINAPI HookGetKeyboardState(PBYTE state) {
    if (!g_originalGetKeyboardState) return FALSE;
    const BOOL ok = g_originalGetKeyboardState(state);
    if (ok && g_uiOpen.load() && state) {
        const BYTE toggleKey = state[g_hotkeyToggleUI];
        memset(state, 0, 256);
        state[g_hotkeyToggleUI] = toggleKey;
    }
    return ok;
}

bool IsCallerInsideBatmanAK(void* caller) {
    static uintptr_t base = 0;
    static uintptr_t end = 0;
    if (!base) {
        HMODULE mod = GetModuleHandleW(L"BatmanAK.exe");
        if (mod) {
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(mod);
            if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
                auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(reinterpret_cast<uintptr_t>(mod) + dos->e_lfanew);
                if (nt && nt->Signature == IMAGE_NT_SIGNATURE) {
                    base = reinterpret_cast<uintptr_t>(mod);
                    end = base + nt->OptionalHeader.SizeOfImage;
                }
            }
        }
    }
    const uintptr_t p = reinterpret_cast<uintptr_t>(caller);
    return base && p >= base && p < end;
}

void ZeroRawInputPayload(RAWINPUT* raw) {
    if (!raw) return;
    if (raw->header.dwType == RIM_TYPEMOUSE) {
        raw->data.mouse.usFlags = 0;
        raw->data.mouse.ulButtons = 0;
        raw->data.mouse.ulRawButtons = 0;
        raw->data.mouse.lLastX = 0;
        raw->data.mouse.lLastY = 0;
        raw->data.mouse.ulExtraInformation = 0;
    } else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        memset(&raw->data.keyboard, 0, sizeof(raw->data.keyboard));
    }
}

UINT WINAPI HookGetRawInputData(HRAWINPUT hRawInput, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    if (!g_originalGetRawInputData) return static_cast<UINT>(-1);
    const UINT result = g_originalGetRawInputData(hRawInput, command, data, size, headerSize);
    if (g_uiOpen.load() && command == RID_INPUT && data && result != static_cast<UINT>(-1) &&
        result >= sizeof(RAWINPUTHEADER)) {
        ZeroRawInputPayload(reinterpret_cast<RAWINPUT*>(data));
    }
    return result;
}

UINT WINAPI HookGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT headerSize) {
    if (!g_originalGetRawInputBuffer) return static_cast<UINT>(-1);
    const UINT count = g_originalGetRawInputBuffer(data, size, headerSize);
    if (g_uiOpen.load() && data && count != static_cast<UINT>(-1)) {
        PRAWINPUT cur = data;
        for (UINT i = 0; i < count; ++i) {
            ZeroRawInputPayload(cur);
            ULONG_PTR next = reinterpret_cast<ULONG_PTR>(cur) + cur->header.dwSize;
            next = (next + sizeof(ULONG_PTR) - 1) & ~(static_cast<ULONG_PTR>(sizeof(ULONG_PTR) - 1));
            cur = reinterpret_cast<PRAWINPUT>(next);
        }
    }
    return count;
}

BOOL WINAPI HookGetCursorPos(LPPOINT point) {
    if (!g_originalGetCursorPos) return FALSE;
    if (g_uiOpen.load() && point && g_gameFrozenCursorValid && IsCallerInsideBatmanAK(_ReturnAddress())) {
        *point = g_gameFrozenCursor;
        return TRUE;
    }
    return g_originalGetCursorPos(point);
}

BOOL WINAPI HookSetCursorPos(int x, int y) {
    if (g_uiOpen.load() && IsCallerInsideBatmanAK(_ReturnAddress())) return TRUE;
    return g_originalSetCursorPos ? g_originalSetCursorPos(x, y) : FALSE;
}

BOOL WINAPI HookClipCursor(const RECT* rect) {
    if (g_uiOpen.load() && rect && IsCallerInsideBatmanAK(_ReturnAddress())) return TRUE;
    return g_originalClipCursor ? g_originalClipCursor(rect) : FALSE;
}

DWORD WINAPI HookXInputGetState(DWORD userIndex, XINPUT_STATE* state) {
    if (!g_originalXInputGetState) return ERROR_DEVICE_NOT_CONNECTED;
    const DWORD result = g_originalXInputGetState(userIndex, state);
    if (result == ERROR_SUCCESS && g_uiOpen.load() && state) {
        const DWORD packet = state->dwPacketNumber;
        memset(&state->Gamepad, 0, sizeof(state->Gamepad));
        state->dwPacketNumber = packet;
    }
    return result;
}

DWORD WINAPI HookXInputGetKeystroke(DWORD userIndex, DWORD reserved, PXINPUT_KEYSTROKE keystroke) {
    if (g_uiOpen.load()) {
        if (keystroke) memset(keystroke, 0, sizeof(*keystroke));
        return ERROR_EMPTY;
    }
    return g_originalXInputGetKeystroke
        ? g_originalXInputGetKeystroke(userIndex, reserved, keystroke)
        : ERROR_DEVICE_NOT_CONNECTED;
}

bool CreateAndEnableInputHook(void* target, void* detour, void** original) {
    if (!target) return false;
    const MH_STATUS create = MH_CreateHook(target, detour, original);
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) return false;
    const MH_STATUS enable = MH_EnableHook(target);
    return enable == MH_OK || enable == MH_ERROR_ENABLED;
}

void InstallHardInputBlockHooks() {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log(L"Input block: MinHook init failed");
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    bool asyncOk = false, keyOk = false, keyboardOk = false;
    bool rawDataOk = false, rawBufferOk = false, getCursorOk = false, setCursorOk = false, clipCursorOk = false;
    if (user32) {
        asyncOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetAsyncKeyState")),
            reinterpret_cast<void*>(&HookGetAsyncKeyState),
            reinterpret_cast<void**>(&g_originalGetAsyncKeyState));
        keyOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyState")),
            reinterpret_cast<void*>(&HookGetKeyState),
            reinterpret_cast<void**>(&g_originalGetKeyState));
        keyboardOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetKeyboardState")),
            reinterpret_cast<void*>(&HookGetKeyboardState),
            reinterpret_cast<void**>(&g_originalGetKeyboardState));
        rawDataOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputData")),
            reinterpret_cast<void*>(&HookGetRawInputData),
            reinterpret_cast<void**>(&g_originalGetRawInputData));
        rawBufferOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetRawInputBuffer")),
            reinterpret_cast<void*>(&HookGetRawInputBuffer),
            reinterpret_cast<void**>(&g_originalGetRawInputBuffer));
        getCursorOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetCursorPos")),
            reinterpret_cast<void*>(&HookGetCursorPos),
            reinterpret_cast<void**>(&g_originalGetCursorPos));
        setCursorOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos")),
            reinterpret_cast<void*>(&HookSetCursorPos),
            reinterpret_cast<void**>(&g_originalSetCursorPos));
        clipCursorOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")),
            reinterpret_cast<void*>(&HookClipCursor),
            reinterpret_cast<void**>(&g_originalClipCursor));
    }

    // Arkham Knight normally uses xinput1_3, but accept the common Windows variants.
    HMODULE xinput = nullptr;
    const wchar_t* xinputNames[] = { L"xinput1_3.dll", L"xinput1_4.dll", L"xinput9_1_0.dll" };
    for (const wchar_t* name : xinputNames) {
        xinput = GetModuleHandleW(name);
        if (xinput) break;
    }

    bool xiStateOk = false, xiKeyOk = false;
    if (xinput) {
        xiStateOk = CreateAndEnableInputHook(
            reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetState")),
            reinterpret_cast<void*>(&HookXInputGetState),
            reinterpret_cast<void**>(&g_originalXInputGetState));
        void* keyProc = reinterpret_cast<void*>(GetProcAddress(xinput, "XInputGetKeystroke"));
        if (keyProc) {
            xiKeyOk = CreateAndEnableInputHook(
                keyProc,
                reinterpret_cast<void*>(&HookXInputGetKeystroke),
                reinterpret_cast<void**>(&g_originalXInputGetKeystroke));
        }
    }

    wchar_t msg[384]{};
    swprintf_s(msg, L"Input hooks: Async=%d Key=%d Keyboard=%d RawData=%d RawBuffer=%d GetCursor=%d SetCursor=%d ClipCursor=%d XInputState=%d XInputKey=%d",
               asyncOk ? 1 : 0, keyOk ? 1 : 0, keyboardOk ? 1 : 0,
               rawDataOk ? 1 : 0, rawBufferOk ? 1 : 0, getCursorOk ? 1 : 0, setCursorOk ? 1 : 0, clipCursorOk ? 1 : 0,
               xiStateOk ? 1 : 0, xiKeyOk ? 1 : 0);
    Log(msg);
}

void EnsureLogPath() {
    if (!g_logPath.empty()) return;
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring p(path);
    const size_t slash = p.find_last_of(L"\\/");
    g_logPath = (slash == std::wstring::npos ? L"BAKTweaks.log" : p.substr(0, slash + 1) + L"BAKTweaks.log");
}

void EnsureConfigPath() {
    if (!g_configPath.empty()) return;

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring p(path);
    const size_t slash = p.find_last_of(L"\\/");

    g_configPath =
        (slash == std::wstring::npos)
        ? L"BAKTweaks.ini"
        : p.substr(0, slash + 1) + L"BAKTweaks.ini";
}

bool IsKeyboardHotkey(int vk) {
    if (vk <= 0 || vk > 0xFE)
        return false;

    switch (vk) {
        case VK_LBUTTON:
        case VK_RBUTTON:
        case VK_MBUTTON:
        case VK_XBUTTON1:
        case VK_XBUTTON2:
            return false;
        default:
            break;
    }

    if (vk == VK_PACKET || vk == VK_PROCESSKEY)
        return false;

    return true;
}

std::string VirtualKeyName(int vk) {
    switch (vk) {
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "Caps Lock";
        case VK_ESCAPE: return "Escape";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left Arrow";
        case VK_UP: return "Up Arrow";
        case VK_RIGHT: return "Right Arrow";
        case VK_DOWN: return "Down Arrow";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_LWIN: return "Left Windows";
        case VK_RWIN: return "Right Windows";
        case VK_NUMLOCK: return "Num Lock";
        case VK_SCROLL: return "Scroll Lock";
        case VK_LSHIFT: return "Left Shift";
        case VK_RSHIFT: return "Right Shift";
        case VK_LCONTROL: return "Left Ctrl";
        case VK_RCONTROL: return "Right Ctrl";
        case VK_LMENU: return "Left Alt";
        case VK_RMENU: return "Right Alt";
        case VK_OEM_1: return ";";
        case VK_OEM_PLUS: return "=";
        case VK_OEM_COMMA: return ",";
        case VK_OEM_MINUS: return "-";
        case VK_OEM_PERIOD: return ".";
        case VK_OEM_2: return "/";
        case VK_OEM_3: return "`";
        case VK_OEM_4: return "[";
        case VK_OEM_5: return "\\";
        case VK_OEM_6: return "]";
        case VK_OEM_7: return "'";
        default:
            break;
    }

    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return "Numpad " + std::to_string(vk - VK_NUMPAD0);
    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);

    switch (vk) {
        case VK_MULTIPLY: return "Numpad *";
        case VK_ADD: return "Numpad +";
        case VK_SEPARATOR: return "Numpad Separator";
        case VK_SUBTRACT: return "Numpad -";
        case VK_DECIMAL: return "Numpad .";
        case VK_DIVIDE: return "Numpad /";
        default:
            break;
    }

    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG lParam = static_cast<LONG>(scan << 16);

    if (vk == VK_LEFT || vk == VK_UP || vk == VK_RIGHT || vk == VK_DOWN ||
        vk == VK_PRIOR || vk == VK_NEXT || vk == VK_END || vk == VK_HOME ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_DIVIDE ||
        vk == VK_NUMLOCK || vk == VK_RCONTROL || vk == VK_RMENU) {
        lParam |= (1 << 24);
    }

    wchar_t name[128]{};
    if (GetKeyNameTextW(lParam, name, 128) > 0) {
        char utf8[256]{};
        const int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (len > 0)
            return utf8;
    }

    char fallback[32]{};
    snprintf(fallback, sizeof(fallback), "VK 0x%02X", vk);
    return fallback;
}

int g_hotkeyCaptureTarget = 0; // 0=none, 1=UI, 2=Small, 3=Vertical
bool g_hotkeyCaptureArmed = false;

bool IsRawKeyDown(int vk) {
    if (g_originalGetAsyncKeyState)
        return (g_originalGetAsyncKeyState(vk) & 0x8000) != 0;

    return false;
}

bool DrawHotkeyCapture(const char* label, int target, int& vk) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(180.0f);

    const std::string keyName = VirtualKeyName(vk);
    ImGui::TextDisabled("%s", keyName.c_str());
    ImGui::SameLine(300.0f);

    ImGui::PushID(target);
    bool changed = false;

    if (g_hotkeyCaptureTarget == target) {
        if (ImGui::Button("Press any key...")) {
            g_hotkeyCaptureTarget = 0;
            g_hotkeyCaptureArmed = false;
        }

        bool anyDown = false;
        for (int key = 1; key <= 0xFE; ++key) {
            if (!IsKeyboardHotkey(key)) continue;
            if (IsRawKeyDown(key)) {
                anyDown = true;
                break;
            }
        }

        if (!g_hotkeyCaptureArmed) {
            if (!anyDown)
                g_hotkeyCaptureArmed = true;
        } else {
            for (int key = 1; key <= 0xFE; ++key) {
                if (!IsKeyboardHotkey(key)) continue;
                if (IsRawKeyDown(key)) {
                    vk = key;
                    g_hotkeyCaptureTarget = 0;
                    g_hotkeyCaptureArmed = false;
                    changed = true;
                    break;
                }
            }
        }
    } else {
        if (ImGui::Button("Change")) {
            g_hotkeyCaptureTarget = target;
            g_hotkeyCaptureArmed = false;
        }
    }

    ImGui::PopID();
    return changed;
}

void SaveHotkeyConfig() {
    EnsureConfigPath();
    if (g_configPath.empty()) return;

    wchar_t value[32]{};

    swprintf_s(value, L"%d", g_hotkeyToggleUI);
    WritePrivateProfileStringW(L"Shortcuts", L"ToggleUI", value, g_configPath.c_str());

    swprintf_s(value, L"%d", g_hotkeySmallLightning);
    WritePrivateProfileStringW(L"Shortcuts", L"SmallLightning", value, g_configPath.c_str());

    swprintf_s(value, L"%d", g_hotkeyVerticalLightning);
    WritePrivateProfileStringW(L"Shortcuts", L"VerticalLightning", value, g_configPath.c_str());
}

void LoadHotkeyConfig() {
    EnsureConfigPath();
    if (g_configPath.empty()) return;

    const int ui = GetPrivateProfileIntW(L"Shortcuts", L"ToggleUI", VK_F10, g_configPath.c_str());
    const int smallKey = GetPrivateProfileIntW(L"Shortcuts", L"SmallLightning", VK_F6, g_configPath.c_str());
    const int vertical = GetPrivateProfileIntW(L"Shortcuts", L"VerticalLightning", VK_F7, g_configPath.c_str());

    g_hotkeyToggleUI = IsKeyboardHotkey(ui) ? ui : VK_F10;
    g_hotkeySmallLightning = IsKeyboardHotkey(smallKey) ? smallKey : VK_F6;
    g_hotkeyVerticalLightning = IsKeyboardHotkey(vertical) ? vertical : VK_F7;
}

void ResetSessionLog() {
    EnsureLogPath();
    FILE* f = nullptr;
    _wfopen_s(&f, g_logPath.c_str(), L"w, ccs=UTF-8");
    if (f) fclose(f);
}

void Log(const wchar_t* text) {
    EnsureLogPath();
    FILE* f = nullptr;
    _wfopen_s(&f, g_logPath.c_str(), L"a+, ccs=UTF-8");
    if (f) { fwprintf(f, L"%s\n", text); fclose(f); }
    OutputDebugStringW(text);
    OutputDebugStringW(L"\n");
}


// -----------------------------------------------------------------------------
// BatLit - native UE3 light manager (no BmSDK script / no companion file)
// Offsets below mirror Team-BmSDK/BmSDK-AK Steam definitions for BatmanAK.exe.
// Engine work is executed only from the top-level FEngineLoop::Tick game-thread hook; never from D3D Present or nested ProcessInternal.
// -----------------------------------------------------------------------------
static constexpr const char* kBAKTweaksVersion = "v5.72";

namespace BatLitNative {
struct BuildProfile {
    const char* name;
    uint32_t timestamp, imageSize;
    uintptr_t findObject, processEvent, spawnActor, defaultObject, engineTick;
    uintptr_t objects, objectCount, exposureWriter;
};
constexpr BuildProfile kBuildProfiles[] = {
    {"Steam", 0x5F8AA2F9, 0x04398000, 0xF3B7F0, 0xF90CD0, 0xACBC70,
     0xF92870, 0x5B1DA0, 0x340CBE4, 0x3A09F28, 0x9F3779},
    {"Epic", 0x6508B1CE, 0x0440B000, 0xF66230, 0xFBBCC0, 0xAF45A0,
     0xFBD860, 0x5D9850, 0x347ACA4, 0x3A77FE8, 0xA1C499}
};
const BuildProfile* g_buildProfile = nullptr;
uintptr_t kStaticFindObject{}, kProcessEvent{}, kSpawnActor{}, kGetDefaultObject{};
uintptr_t kEngineTick{}, kGObjects{}, kGObjectsNumOff{}, kExposureLevelPPWriter{};
constexpr int       kGObjectsCapacity = 785000;

// Exposure: confirmed LevelPP writer and value location for this BatmanAK.exe build.
constexpr ptrdiff_t kLocalPlayerLevelPPExposureBaseOffset = 0x474;
constexpr ptrdiff_t kLocalPlayerCurrentPPExposureBaseOffset = 0x258;
constexpr uint8_t kExposureWriterOriginal[8] = { 0xF3,0x0F,0x11,0x8B,0x64,0x01,0x00,0x00 };

constexpr ptrdiff_t kUObjectFlags = 0x08;
constexpr ptrdiff_t kUObjectOuter = 0x34;
constexpr ptrdiff_t kUObjectClass = 0x44;
constexpr ptrdiff_t kUStructSuper = 0x5C;
constexpr uint32_t  kRF_ClassDefaultObject = 0x80;
constexpr ptrdiff_t kUFunctionFlags = 0xA4; // +164
constexpr ptrdiff_t kUFunctionNativeIndex = 0xA8; // +168
constexpr uint32_t  kFUNC_Native = 0x00000400u;
constexpr uint32_t  kFUNC_Defined = 0x00000002u;

constexpr ptrdiff_t kLightActorComponent = 0x29C; // Engine.Light.LightComponent
constexpr ptrdiff_t kPlayerControllerPlayerCamera = 0x478; // +1144
constexpr ptrdiff_t kCameraCache = 0x570; // +1392
constexpr ptrdiff_t kCameraCachePOV = 0x04;
constexpr ptrdiff_t kActorLocation = 0x0C4; // Engine.Actor.Location +196
constexpr ptrdiff_t kActorRotation = 0x0D0; // Engine.Actor.Rotation +208

struct Vec3 { float x{}, y{}, z{}; };
struct RotatorRaw { int32_t pitch{}, yaw{}, roll{}; };
struct FNameRaw { int32_t index{}, number{}; };
std::unordered_map<int32_t, std::string> g_poseGlobalFNameNames;
std::unordered_map<uint64_t, std::string> g_poseNumberedFNameNames;
std::atomic<int> g_poseNameDumpState{0}; // 0 loading, 1 loaded, -1 unavailable/invalid

bool LoadPoseGlobalFNameDump() {
    wchar_t dumpPath[MAX_PATH]{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, dumpPath, MAX_PATH);
    if (!pathLength || pathLength >= MAX_PATH) {
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: could not resolve game executable path");
        return false;
    }
    wchar_t* separator = wcsrchr(dumpPath, L'\\');
    if (!separator || static_cast<size_t>(dumpPath + MAX_PATH - separator) <= 15) {
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: invalid game executable path");
        return false;
    }
    wcscpy_s(separator + 1, static_cast<size_t>(dumpPath + MAX_PATH - (separator + 1)), L"NamesDump.txt");

    HANDLE file = CreateFileW(dumpPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: NamesDump.txt not found beside BatmanAK.exe; using bundled FName map");
        return false;
    }
    LARGE_INTEGER fileSize{};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(file);
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: NamesDump.txt has an invalid size");
        return false;
    }
    std::string contents(static_cast<size_t>(fileSize.QuadPart), '\0');
    size_t totalRead = 0;
    while (totalRead < contents.size()) {
        DWORD chunkRead = 0;
        const DWORD chunkSize = static_cast<DWORD>((std::min)(contents.size() - totalRead,
            static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
        if (!ReadFile(file, contents.data() + totalRead, chunkSize, &chunkRead, nullptr) || !chunkRead) break;
        totalRead += chunkRead;
    }
    CloseHandle(file);
    if (totalRead != contents.size()) {
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: could not read complete NamesDump.txt");
        return false;
    }

    std::unordered_map<int32_t, std::string> names;
    const char* cursor = contents.data();
    const char* const end = cursor + contents.size();
    while (cursor < end) {
        const char* lineEnd = static_cast<const char*>(memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
        if (!lineEnd) lineEnd = end;
        const char* p = cursor;
        if (p < lineEnd && *p == '[') {
            ++p;
            int32_t index = 0;
            bool hasDigits = false;
            while (p < lineEnd && *p >= '0' && *p <= '9') {
                hasDigits = true;
                index = index * 10 + (*p++ - '0');
            }
            if (hasDigits && p < lineEnd && *p++ == ']') {
                while (p < lineEnd && (*p == ' ' || *p == '\t')) ++p;
                if (p < lineEnd) names.emplace(index, std::string(p, lineEnd));
            }
        }
        cursor = lineEnd < end ? lineEnd + 1 : end;
    }
    if (names.size() < 10000) {
        g_poseNameDumpState.store(-1, std::memory_order_release);
        Log(L"POSE names: NamesDump.txt did not contain a valid UE3 name list; using bundled FName map");
        return false;
    }
    const size_t nameCount = names.size();
    g_poseGlobalFNameNames = std::move(names);
    g_poseNameDumpState.store(1, std::memory_order_release);
    wchar_t message[160]{};
    swprintf_s(message, L"POSE names: loaded %zu global FNames from NamesDump.txt", nameCount);
    Log(message);
    return true;
}

const char* ResolveKnownPoseBoneName(FNameRaw name) {
    for (const auto& entry : AKPoseBoneNames::kEntries) {
        if (entry.index == name.index && entry.number == name.number) return entry.name;
    }
    if (g_poseNameDumpState.load(std::memory_order_acquire) == 1) {
        const auto it = g_poseGlobalFNameNames.find(name.index);
        if (it != g_poseGlobalFNameNames.end()) {
            if (name.number == 0) return it->second.c_str();
            if (name.number > 0) {
                const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(name.index)) << 32) |
                    static_cast<uint32_t>(name.number);
                auto numbered = g_poseNumberedFNameNames.find(key);
                if (numbered == g_poseNumberedFNameNames.end())
                    numbered = g_poseNumberedFNameNames.emplace(key,
                        it->second + "_" + std::to_string(name.number - 1)).first;
                return numbered->second.c_str();
            }
        }
    }
    return nullptr;
}
const char* CategorizePoseBoneName(const char* name) {
    if (!name) return "Unmapped";
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto has = [&](const char* token) { return lower.find(token) != std::string::npos; };
    if (has("eye") || has("eyld")) return "Head / Eyes";
    if (has("eybrw") || has("brow") || has("frhead")) return "Head / Brows";
    if (has("mouth") || has("jaw") || has("tongue") || has("chin")) return "Head / Mouth";
    if (has("nose")) return "Head / Nose";
    if (has("cheek") || has("face") || has("pinch")) return "Head / Face";
    if (has("head") || has("neck")) return "Head / Neck";
    const bool left = has("_l_") || has("left") || lower.rfind("l_", 0) == 0;
    const bool right = has("_r_") || has("right") || lower.rfind("r_", 0) == 0;
    if (has("finger") || has("thumb"))
        return left && !right ? "Fingers L" : right && !left ? "Fingers R" : "Fingers";
    if (has("hand") || has("wrist") || has("palm"))
        return left && !right ? "Hand L" : right && !left ? "Hand R" : "Hands";
    if (has("arm") || has("gauntlet") || has("clavicle") || has("forearm") || has("foretwist") ||
        has("elbow") || has("shoulder") || has("twist"))
        return left && !right ? "Arm L" : right && !left ? "Arm R" : "Arms";
    if (has("thigh") || has("calf") || has("knee") || has("foot") || has("toe") || has("boot") || has("leg"))
        return left && !right ? "Leg L" : right && !left ? "Leg R" : "Legs";
    if (has("whip")) return "Whip";
    if (has("belt")) return "Belt";
    if (has("spine") || has("pelvis") || has("chest") || has("pec") || has("armour") || has("symbol"))
        return "Body / Core";
    return "Other";
}
struct ColorBGRA { uint8_t b{}, g{}, r{}, a{255}; };

using StaticFindObjectFn = void* (__cdecl*)(void*, void*, const wchar_t*, int);
using ProcessEventFn     = void  (__fastcall*)(void*, void*, void*, void*);
using SpawnActorFn       = void* (__fastcall*)(void*, void*, FNameRaw, Vec3*, RotatorRaw*, void*, int, int, void*, void*, int, void*);
using GetDefaultObjectFn = void* (__fastcall*)(void*, int);
using EngineTickFn       = intptr_t (__fastcall*)(void*);

StaticFindObjectFn g_staticFindObject = nullptr;
ProcessEventFn g_processEvent = nullptr;
SpawnActorFn g_spawnActor = nullptr;
GetDefaultObjectFn g_getDefaultObject = nullptr;
EngineTickFn g_originalEngineTick = nullptr;
std::atomic<bool> g_poseEditDesired{false};
std::atomic<bool> g_poseFreezeDesired{false};
std::atomic<bool> g_poseEditActive{false};
std::atomic<bool> g_poseWriterWasRequested{false};
std::atomic<float> g_poseTranslationOffset[3]{};
std::atomic<float> g_poseRotationOffsetDegrees[3]{}; // Pitch, Yaw, Roll
std::atomic<float> g_poseAbsolutePosition[3]{};
std::atomic<float> g_poseAbsoluteRotationDegrees[3]{}; // Pitch, Yaw, Roll
std::atomic<bool> g_poseAbsoluteTargetEnabled{false};
std::atomic<int> g_poseAbsoluteTargetBone{-1};
std::atomic<uint64_t> g_poseEditAppliedCount{0};
std::atomic<int> g_poseMappedChildBoneCount{0};
std::atomic<int> g_poseMappedChildComponentCount{0};
std::atomic<uint64_t> g_poseMappedChildLogCount{0};
std::atomic<uint64_t> g_poseFinalizeCallCount{0};
std::atomic<uint64_t> g_poseFinalizeMatchCount{0};
std::atomic<uint64_t> g_poseArrayReadyCount{0};
std::atomic<uint64_t> g_poseFreezeWriteCount{0};
std::atomic<uint64_t> g_poseSpaceBasesCombineCalls{0};
std::atomic<uint64_t> g_poseSpaceBasesCombineMatches{0};
std::atomic<uint64_t> g_poseSpaceBasesCombineWrites{0};
std::atomic<uint64_t> g_poseLocalAtomsUpdateCalls{0};
std::atomic<uint64_t> g_poseLocalAtomsUpdateMatches{0};
std::atomic<uint64_t> g_poseLocalAtomsUpdateWrites{0};
std::atomic<uint64_t> g_poseSpaceBasesReaderCalls{0};
std::atomic<uint64_t> g_poseSpaceBasesReaderMatches{0};
std::atomic<bool> g_poseSpaceBasesReaderTraceLogged{false};
std::atomic<uintptr_t> g_poseSpaceBasesReaderLastInput{0};
std::atomic<uintptr_t> g_poseSpaceBasesReaderLastOutput{0};
std::atomic<uintptr_t> g_poseSpaceBasesReaderLastCaller{0};
std::array<std::atomic<uintptr_t>, 5> g_poseSpaceBasesReaderStack{};
std::atomic<int> g_poseFrozenComponentCount{0};
std::atomic<uint64_t> g_poseFinalizerVariantCalls[4]{};
std::atomic<uint64_t> g_poseDispatcherCallerCalls[5]{};
std::atomic<uint64_t> g_poseDispatcherModeCalls[7]{};
std::atomic<uint64_t> g_poseSelectedDispatcherModeCalls[7]{};
std::atomic<uint64_t> g_poseControlEvaluatorCalls[2]{};
std::atomic<uint64_t> g_poseSelectedControlEvaluatorCalls[2]{};
std::atomic<uint64_t> g_poseSelectedControlEvaluatorChangedCalls[2]{};
std::atomic<uint64_t> g_poseSelectedDispatcherOriginalChanges{0};
std::atomic<uint64_t> g_poseSelectedDispatcherWriterChanges{0};
std::atomic<uint64_t> g_poseSelectedDispatcherPostWriterChanges{0};
std::atomic<uint64_t> g_poseSelectedDispatcherLastPostWriterHash{0};
std::atomic<uintptr_t> g_poseSelectedDispatcherLastPostWriterMesh{0};
struct PoseDispatcherCallerStat {
    std::atomic<uintptr_t> address{0};
    std::atomic<uint64_t> calls{0};
};
PoseDispatcherCallerStat g_poseDispatcherCallers[8]{};
std::atomic<uint64_t> g_poseDispatcherCallerOverflow{0};
std::atomic<uint64_t> g_poseFinalizerMismatchDiagnostics[4]{};
std::atomic<int> g_poseFinalizeFailure{0};
std::atomic<int> g_poseFrozenBoneCount{0};
std::atomic<int> g_poseFinalizerHookMask{0};
std::atomic<bool> g_poseFinalizerHookInstalled[3]{};
std::atomic<bool> g_poseFinalizerHookEnabled[3]{};
std::atomic<bool> g_poseFinalizerHookAttempted[3]{};
using PoseFinalizeFn = void(__fastcall*)(void*);
using PoseControlEvaluatorFn = void(__fastcall*)(void*, int);
using PoseSpaceBasesCombineFn = void(__fastcall*)(void*, void*, void*, void*);
using PoseSpaceBasesToLocalFn = void(__fastcall*)(void*, void*, void*);
using PoseLocalAtomsUpdateFn = void(__fastcall*)(void*, void*);
PoseFinalizeFn g_originalPoseFinalizeType1 = nullptr;
PoseFinalizeFn g_originalPoseFinalizeType2 = nullptr;
PoseFinalizeFn g_originalPoseFinalizeType5 = nullptr;
PoseFinalizeFn g_originalPoseFinalizeDispatcher = nullptr;
PoseControlEvaluatorFn g_originalPoseControlEvaluator = nullptr;
PoseSpaceBasesCombineFn g_originalPoseSpaceBasesCombine = nullptr;
PoseSpaceBasesToLocalFn g_originalPoseSpaceBasesToLocal = nullptr;
PoseLocalAtomsUpdateFn g_originalPoseLocalAtomsUpdate = nullptr;
std::atomic<bool> g_poseDispatcherHookInstalled{false};
std::atomic<bool> g_poseDispatcherHookEnabled{false};
std::atomic<bool> g_poseDispatcherHookAttempted{false};
std::atomic<bool> g_poseControlEvaluatorHookInstalled{false};
std::atomic<bool> g_poseControlEvaluatorHookAttempted{false};
std::atomic<bool> g_poseControlEvaluatorHookEnabled{false};
std::atomic<bool> g_poseSpaceBasesCombineHookInstalled{false};
std::atomic<bool> g_poseSpaceBasesCombineHookEnabled{false};
std::atomic<bool> g_poseSpaceBasesCombineHookAttempted{false};
std::atomic<bool> g_poseSpaceBasesToLocalHookInstalled{false};
std::atomic<bool> g_poseSpaceBasesToLocalHookEnabled{false};
std::atomic<bool> g_poseSpaceBasesToLocalHookAttempted{false};
std::atomic<bool> g_poseLocalAtomsUpdateHookInstalled{false};
std::atomic<bool> g_poseLocalAtomsUpdateHookEnabled{false};
std::atomic<bool> g_poseLocalAtomsUpdateHookAttempted{false};
std::mutex g_poseWriterMutex;
std::vector<uint8_t> g_poseFrozenSnapshot;
void* g_poseFrozenMesh{};
int g_poseFrozenCount{};
bool g_poseFrozenSnapshotReady{};
struct PoseFrozenComponentSnapshot {
    void* mesh{};
    void* data{};
    int boneCount{};
    std::vector<uint8_t> bytes;
    void* localAtomsData{};
    int localAtomsBoneCount{};
    std::vector<uint8_t> localAtomsBytes;
};
std::vector<PoseFrozenComponentSnapshot> g_poseFrozenComponents;
static constexpr size_t kPoseFastFrozenBufferCount = 16;
std::array<std::atomic<uintptr_t>, kPoseFastFrozenBufferCount> g_poseFastFrozenSpaceBases{};
std::array<std::atomic<int>, kPoseFastFrozenBufferCount> g_poseFastFrozenBoneCounts{};

void ClearPoseFastFrozenBuffers() {
    for (size_t i = 0; i < kPoseFastFrozenBufferCount; ++i) {
        g_poseFastFrozenSpaceBases[i].store(0, std::memory_order_release);
        g_poseFastFrozenBoneCounts[i].store(0, std::memory_order_relaxed);
    }
}

void PublishPoseFastFrozenBuffers() {
    ClearPoseFastFrozenBuffers();
    const size_t count = (std::min)(g_poseFrozenComponents.size(), kPoseFastFrozenBufferCount);
    for (size_t i = 0; i < count; ++i) {
        const auto& snapshot = g_poseFrozenComponents[i];
        g_poseFastFrozenBoneCounts[i].store(snapshot.boneCount, std::memory_order_relaxed);
        g_poseFastFrozenSpaceBases[i].store(reinterpret_cast<uintptr_t>(snapshot.data), std::memory_order_release);
    }
}
std::atomic<DWORD> g_gameThreadId{0};
thread_local bool g_inCommandDispatch = false;
uintptr_t g_gameBase = 0;
void* g_classFunction = nullptr;
void* g_classPointLightMovable = nullptr;
void* g_classSpotLightMovable = nullptr;
void* g_classPlayerController = nullptr;
void* g_classWorldInfo = nullptr;
void* g_classEngine = nullptr;
void* g_classRRainComponent = nullptr;
void* g_classRPollenComponent = nullptr;
void* g_classRDustComponent = nullptr;
void* g_classRSnowComponent = nullptr;
void* g_classExponentialHeightFogComponent = nullptr;
void* g_classRPawnPlayerBm = nullptr;
void* g_classSkeletalMeshComponent = nullptr;
void* g_fnGetBoneNames = nullptr;
void* g_fnGetParentBone = nullptr;
void* g_fnGetBoneLocation = nullptr;
void* g_fnGetBoneQuaternion = nullptr;
void* g_fnGetBoneMatrix = nullptr;
void* g_fnMatrixGetRotator = nullptr;
void* g_coreObjectClass = nullptr;
void* g_coreObjectCDO = nullptr;
int g_controllerPawnOffset = -1;
int g_pawnMeshOffset = -1;
int g_worldPawnListOffset = -1;
int g_pawnNextPawnOffset = -1;
int g_actorComponentsOffset = -1;
int g_actorAllComponentsOffset = -1;
int g_poseSpaceBasesOffset = -1;
int g_poseLocalAtomsOffset = -1;
int g_poseCachedSpaceBasesOffset = -1;
int g_poseCachedLocalAtomsOffset = -1;
int g_poseParentAnimComponentOffset = -1;
int g_poseParentAnimComponentModeOffset = -1;
int g_poseParentBoneMapOffset = -1;
int g_poseAttachedToSkelComponentOffset = -1;
int g_poseAnimTreeTemplateOffset = -1;
int g_poseAnimationsOffset = -1;
int g_poseAnimNodePoseOffset = -1;
int g_poseAnimTickArrayOffset = -1;
int g_poseAnimAlwaysTickArrayOffset = -1;
int g_poseAnimTickWeightsArrayOffset = -1;
int g_poseSkelControlInstancesOffset = -1;
int g_posePostFixerUpdateBonesOffset = -1;
int g_enginePauseOnLossOfFocusOffset = -1;
int g_posePauseAnimsOffset = -1;
uint8_t g_posePauseAnimsMask = 0;
void* g_poseAnimPauseMesh = nullptr;
bool g_poseAnimPauseOriginal = false;
std::atomic<bool> g_poseAnimPauseApplied{false};
std::atomic<int> g_poseAnimPauseWriteReadback{-1};
std::atomic<int> g_poseAnimPauseReadback{-1};
void RestorePoseAnimationPause();
constexpr uint32_t kEnginePauseOnLossOfFocusMask = 0x00800000u;
std::atomic<bool> g_poseKeepRunningUnfocused{true};
bool g_poseFocusOriginalValid = false;
bool g_poseFocusOriginalValue = false;
void* g_poseFocusEngine = nullptr;
void* g_classPawn = nullptr;
struct PoseTarget {
    void* pawn{};
    void* mesh{};
    bool isPlayer{};
};
struct PoseBufferDiagnostic {
    int boneCount{};
    uint64_t hash{};
    uint64_t changedSamples{};
    uint64_t samples{};
};
struct PoseMeshDiagnostic {
    void* mesh{};
    int sourceIndex{-1};
    int sourceList{-1};
    int boneCount{};
    PoseBufferDiagnostic buffers[4]{}; // SpaceBases, LocalAtoms, CachedSpaceBases, CachedLocalAtoms
    void* parentAnimComponent{};
    void* attachedToSkelComponent{};
    void* animTreeTemplate{};
    void* animations{};
    void* animNodePose{};
    int parentAnimComponentMode{-1};
    int parentBoneMapCount{-1};
    int animTickCount{-1};
    int animAlwaysTickCount{-1};
    int animTickWeightsCount{-1};
    int skelControlCount{-1};
    int postFixerCount{-1};
    int pauseBit{-1};
};
struct PoseBoneInfo {
    FNameRaw name{};
    FNameRaw parent{};
    int parentIndex{-1};
    const char* resolvedName{};
    const char* category{"Unmapped"};
};
struct PoseArraySnapshot {
    uintptr_t data{};
    int32_t count{};
    int32_t capacity{};
    float values[16]{};
    bool valid{};
};
struct PoseBoneTransform {
    Vec3 location{};
    float quaternion[4]{};
    RotatorRaw rotation{};
    Vec3 locationBySpace[3]{};
    float quaternionBySpace[3][4]{};
    bool validBySpace[3]{};
    PoseArraySnapshot spaceBases{};
    PoseArraySnapshot localAtoms{};
    bool valid{};
    bool quaternionValid{};
    bool rotationValid{};
    int selectedBone{-1};
};
std::mutex g_poseMutex;
std::vector<PoseTarget> g_poseTargets;
std::vector<PoseMeshDiagnostic> g_poseMeshDiagnostics;
void* g_poseDiagnosticPawn{};
std::wstring g_poseDiagnosticStatus = L"Refresh to inspect attached SkeletalMeshComponents.";
std::vector<PoseBoneInfo> g_poseBones;
std::array<int, 512> g_poseSelectedSubtreeIndices{};
int g_poseSelectedSubtreeCount{};
int g_poseSelectedSubtreeRoot{-1};
std::wstring g_poseStatus = L"Press Refresh to inspect the character roster.";
std::atomic<bool> g_poseRefreshRequested{false};
std::atomic<int> g_poseParentCursor{-1};
std::atomic<int> g_poseSelectedTarget{0};
std::atomic<int> g_poseSelectedBone{-1};
std::atomic<bool> g_poseTransformRequested{false};
std::atomic<bool> g_poseProbeRequested{false};
PoseBoneTransform g_poseSelectedTransform{};
void* g_posePawn{};
void* g_poseMesh{};
std::atomic<uintptr_t> g_poseTraceSelectedMesh{0};
std::atomic<bool> g_poseApplyToSubtreeDesired{true};

void RebuildPoseSelectedSubtreeIndices() {
    const int selected = g_poseSelectedBone.load(std::memory_order_relaxed);
    std::array<int, 512> subtree{};
    int subtreeCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        const int boneCount = (std::min)(512, static_cast<int>(g_poseBones.size()));
        if (selected >= 0 && selected < boneCount) {
            for (int candidate = 0; candidate < boneCount; ++candidate) {
                int ancestor = candidate;
                for (int depth = 0; depth < boneCount && ancestor >= 0 && ancestor < boneCount; ++depth) {
                    if (ancestor == selected) {
                        subtree[static_cast<size_t>(subtreeCount++)] = candidate;
                        break;
                    }
                    ancestor = g_poseBones[static_cast<size_t>(ancestor)].parentIndex;
                }
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        g_poseSelectedSubtreeIndices = subtree;
        g_poseSelectedSubtreeCount = subtreeCount;
        g_poseSelectedSubtreeRoot = selected;
    }
}

int g_uPropertyOffsetField = -1;
int g_rainParticleCountOffset = -1;
int g_pollenParticleCountOffset = -1;
int g_dustParticleCountOffset = -1;
int g_snowParticleCountOffset = -1;
int g_fogStartDistanceOffset = -1;
int g_fogMaxOpacityOffset = -1;
int g_fogHeightFalloffOffset = -1;
int g_fogDensityOffset = -1;
int g_fogHeightOffset = -1;
int g_radioModeEyesOffset = -1;
uint32_t g_radioModeEyesMask = 0;
void* g_lastPlayerController = nullptr;
void* g_fnGetPlayerViewPoint = nullptr;
void* g_fnWorldInfoGetWorldInfo = nullptr;
void* g_fnEngineGetEngine = nullptr;
void* g_fnFindPlayerByControllerId = nullptr;
void* g_fnSetEnabled = nullptr;
void* g_fnLightColor = nullptr;
void* g_fnBrightness = nullptr;
void* g_fnUpdateColorBrightness = nullptr;
void* g_fnUpdateShafts = nullptr;
void* g_fnSetTranslation = nullptr;
void* g_fnSetAngleRadius = nullptr;
void* g_fnSetRotation = nullptr;
void* g_fnSetAbsoluteRotation = nullptr;
void* g_fnActorSetLocation = nullptr;
void* g_fnActorSetRotation = nullptr;
void* g_fnActorDestroy = nullptr;
void* g_fnIgnoreMoveInput = nullptr;
void* g_fnIgnoreLookInput = nullptr;
void* g_fnIgnoreButtonInput = nullptr;
void* g_fnIsMoveInputIgnored = nullptr;
void* g_fnIsLookInputIgnored = nullptr;
void* g_fnIsButtonInputIgnored = nullptr;
void* g_fnResetPlayerMovementInput = nullptr;
void* g_fnFogSetEnabled = nullptr;
void* g_fnRadioModeEyesAndEffects = nullptr;
void* g_fnConsoleCommand = nullptr;
std::atomic<int> g_radioEyesRequest{-1}; // -1 none, 0 off, 1 on
std::mutex g_consoleCommandMutex;
std::vector<std::wstring> g_consoleCommandQueue;
std::atomic<bool> g_forceInputRelease{false};

// Lightning VFX (validated on this exact v5.23 BatmanAK build).
// Re-use the two natural PSCs owned by the live RLightningActor because these
// are already registered with the renderer (SceneInfo != 0). Newly pooled PSCs
// were created correctly but remained unregistered/invisible.
void* g_classRLightningActor = nullptr;
void* g_fnParticleActivateSystem = nullptr;
void* g_fnParticleDeactivateSystem = nullptr;
void* g_fnPrimitiveSetTranslation = nullptr;
void* g_normalLightningLifetimeModule = nullptr;
void* g_verticalLightningLifetimeModule = nullptr;
void* g_normalLightningSpawnModule = nullptr;
void* g_verticalLightningSpawnModule = nullptr;
constexpr ptrdiff_t kRLightningNormalPSC = 0x2B4;
constexpr ptrdiff_t kRLightningVerticalPSC = 0x2AC;
constexpr ptrdiff_t kParticleLifetimeRawDistribution = 0x60;
std::mutex g_lightningRequestMutex;
int g_lightningRequestType = 0; // 0 none, 1 small/normal, 2 vertical
float g_lightningRequestDuration = 1.0f;
float g_lightningRequestDistance = 5000.0f;
void* g_lightningActorCache = nullptr;
void* g_normalLightningPSCCache = nullptr;
void* g_verticalLightningPSCCache = nullptr;
std::atomic<bool> g_lightningCacheReady{false};
std::atomic<bool> g_lightningCEBusy{false};

// Lightning settings are shared by the UI and the F6/F7 shortcuts.
// This guarantees that a shortcut fired with the UI hidden uses the
// exact values last configured in the Lightning panel.
float g_smallLightningDuration = 1.0f;
float g_smallLightningDistance = 5000.0f;
int g_smallLightningInstances = 1;
int g_smallLightningSalvoCount = 1;
float g_smallLightningSalvoInterval = 0.08f;
int g_smallLightningClusters = 1;
float g_smallLightningClusterSpread = 0.08f;

float g_verticalLightningDuration = 1.0f;
float g_verticalLightningDistance = 5000.0f;
int g_verticalLightningInstances = 1;
int g_verticalLightningSalvoCount = 1;
float g_verticalLightningSalvoInterval = 0.08f;
int g_verticalLightningClusters = 1;
float g_verticalLightningClusterSpread = 0.08f;

bool g_lightningRandomizePosition = true;
float g_lightningSpreadX = 0.80f;
float g_lightningSpreadY = 0.65f;
float g_lightningDistanceJitter = 0.15f;
// Exact GObject indices from the validated CE session / current ObjectsDump.
constexpr int kGObj_RLightningActorClass = 1791;
constexpr int kGObj_SmallLifetimeModule = 209911;
constexpr int kGObj_VerticalLifetimeModule = 209923;
constexpr int kGObj_SmallSpawnModule = 209910;
constexpr int kGObj_VerticalSpawnModule = 209922;
constexpr int kGObj_ParticleModuleSpawn_BurstList = 32501;
constexpr int kGObj_ParticleBurst_Time = 31865;
constexpr int kGObj_ParticleBurst_CountLow = 31866;
constexpr int kGObj_ParticleBurst_Count = 31867;

struct LightningPreparedRequest {
    void* psc{};
    void* lifetimeModule{};
    float seconds{1.0f};
    float distance{5000.0f};
    int instances{1};
    int salvoCount{1};
    float salvoInterval{0.08f};
    int clusterCount{1};
    float clusterSpread{0.08f};
    bool vertical{};
    bool randomizePosition{};
    float spreadX{0.80f};
    float spreadY{0.65f};
    float distanceJitter{0.15f};
};
std::mutex g_lightningPreparedMutex;
LightningPreparedRequest g_lightningPrepared{};
std::atomic<bool> g_lightningPreparedReady{false};
std::mutex g_lightningStatusMutex;
std::wstring g_lightningStatus = L"Idle";

// UE3-level input block state. This is the authoritative blocker for Batman's
// own PlayerController input; Win32/XInput hooks below are only secondary guards.
void* g_inputBlockPC = nullptr;
bool g_inputSnapshotValid = false;
bool g_prevMoveIgnored = false;
bool g_prevLookIgnored = false;
bool g_prevButtonIgnored = false;

bool IsReadableAddress(const void* ptr, size_t bytes = 1) {
    if (!ptr || bytes == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t z = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return a + bytes <= z;
}

bool IsWritableAddress(const void* ptr, size_t bytes = 1) {
    if (!ptr || bytes == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const DWORD p = mbi.Protect & 0xff;
    if (p != PAGE_READWRITE && p != PAGE_WRITECOPY &&
        p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t z = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return a + bytes <= z;
}

bool IsExecutableAddress(const void* ptr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!ptr || !VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    const DWORD p = mbi.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

template <typename T> T Read(void* base, ptrdiff_t off) {
    if (!IsReadableAddress(reinterpret_cast<uint8_t*>(base) + off, sizeof(T))) return T{};
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off);
}
template <typename T> void Write(void* base, ptrdiff_t off, const T& value) {
    if (!IsReadableAddress(reinterpret_cast<uint8_t*>(base) + off, sizeof(T))) return;
    *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off) = value;
}

int32_t DegreesToRuu(float degrees) {
    return static_cast<int32_t>(std::lround(degrees * (65536.0f / 360.0f)));
}
float RuuToDegrees(int32_t ruu) {
    return static_cast<float>(ruu) * (360.0f / 65536.0f);
}

void* FindObject(const wchar_t* path, bool exact = false) {
    if (!g_staticFindObject) return nullptr;
    return g_staticFindObject(nullptr, reinterpret_cast<void*>(static_cast<intptr_t>(-1)), path, exact ? 1 : 0);
}
void* FindFunction(const wchar_t* path) {
    if (!g_staticFindObject || !g_classFunction) return nullptr;
    return g_staticFindObject(g_classFunction, reinterpret_cast<void*>(static_cast<intptr_t>(-1)), path, 1);
}

bool IsA(void* object, void* targetClass) {
    if (!object || !targetClass || !IsReadableAddress(reinterpret_cast<uint8_t*>(object) + kUObjectClass, sizeof(void*))) return false;
    void* cls = Read<void*>(object, kUObjectClass);
    for (int depth = 0; cls && depth < 128; ++depth) {
        if (cls == targetClass) return true;
        if (!IsReadableAddress(reinterpret_cast<uint8_t*>(cls) + kUStructSuper, sizeof(void*))) break;
        cls = Read<void*>(cls, kUStructSuper);
    }
    return false;
}

uint64_t HashPoseBytes(const void* data, size_t bytes) {
    if (!data || !bytes || !IsReadableAddress(data, bytes)) return 0;
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < bytes; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

void SamplePoseMeshDiagnosticsAfterTick() {
    const int pauseOffset = g_posePauseAnimsOffset;
    const uint8_t pauseMask = g_posePauseAnimsMask;
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
        for (auto& diag : g_poseMeshDiagnostics) {
            if (!diag.mesh || !g_classSkeletalMeshComponent || !IsA(diag.mesh, g_classSkeletalMeshComponent)) {
                diag.boneCount = 0;
                diag.pauseBit = -1;
                continue;
            }
            struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
            const int arrayOffsets[] = { g_poseSpaceBasesOffset, g_poseLocalAtomsOffset,
                g_poseCachedSpaceBasesOffset, g_poseCachedLocalAtomsOffset };
            for (int arrayIndex = 0; arrayIndex < 4; ++arrayIndex) {
                auto& buffer = diag.buffers[arrayIndex];
                buffer.boneCount = 0;
                const int arrayOffset = arrayOffsets[arrayIndex];
                if (arrayOffset < 0) continue;
                auto* address = reinterpret_cast<uint8_t*>(diag.mesh) + arrayOffset;
                if (!IsReadableAddress(address, sizeof(header))) continue;
                memcpy(&header, address, sizeof(header));
                if (header.count < 0 || header.count > 4096 || header.capacity < header.count ||
                    (header.count > 0 && (!header.data || !IsReadableAddress(header.data, static_cast<size_t>(header.count) * 0x20)))) continue;
                buffer.boneCount = header.count;
                if (header.count == 0) continue;
                const uint64_t hash = HashPoseBytes(header.data, static_cast<size_t>(header.count) * 0x20);
                if (hash) {
                    if (buffer.samples && buffer.hash != hash) ++buffer.changedSamples;
                    buffer.hash = hash;
                    ++buffer.samples;
                }
            }
            diag.boneCount = diag.buffers[0].boneCount;
            auto readPointer = [&](int offset) -> void* {
                void* value = nullptr;
                if (offset < 0) return value;
                auto* address = reinterpret_cast<uint8_t*>(diag.mesh) + offset;
                if (IsReadableAddress(address, sizeof(value))) memcpy(&value, address, sizeof(value));
                return value;
            };
            auto readInt = [&](int offset, size_t size) -> int {
                int value = -1;
                if (offset < 0) return value;
                auto* address = reinterpret_cast<uint8_t*>(diag.mesh) + offset;
                if (!IsReadableAddress(address, size)) return value;
                if (size == 1) { uint8_t byteValue{}; memcpy(&byteValue, address, sizeof(byteValue)); value = byteValue; }
                else memcpy(&value, address, sizeof(value));
                return value;
            };
            auto readArrayCount = [&](int offset) -> int {
                if (offset < 0 || !IsReadableAddress(reinterpret_cast<uint8_t*>(diag.mesh) + offset, sizeof(header))) return -1;
                memcpy(&header, reinterpret_cast<uint8_t*>(diag.mesh) + offset, sizeof(header));
                return header.count >= 0 && header.count <= 4096 && header.capacity >= header.count ? header.count : -1;
            };
            diag.parentAnimComponent = readPointer(g_poseParentAnimComponentOffset);
            diag.attachedToSkelComponent = readPointer(g_poseAttachedToSkelComponentOffset);
            diag.animTreeTemplate = readPointer(g_poseAnimTreeTemplateOffset);
            diag.animations = readPointer(g_poseAnimationsOffset);
            diag.animNodePose = readPointer(g_poseAnimNodePoseOffset);
            diag.parentAnimComponentMode = readInt(g_poseParentAnimComponentModeOffset, sizeof(uint8_t));
            diag.parentBoneMapCount = readArrayCount(g_poseParentBoneMapOffset);
            diag.animTickCount = readArrayCount(g_poseAnimTickArrayOffset);
            diag.animAlwaysTickCount = readArrayCount(g_poseAnimAlwaysTickArrayOffset);
            diag.animTickWeightsCount = readArrayCount(g_poseAnimTickWeightsArrayOffset);
            diag.skelControlCount = readArrayCount(g_poseSkelControlInstancesOffset);
            diag.postFixerCount = readArrayCount(g_posePostFixerUpdateBonesOffset);
            if (pauseOffset >= 0 && pauseMask &&
                IsReadableAddress(reinterpret_cast<uint8_t*>(diag.mesh) + pauseOffset, sizeof(uint8_t))) {
                auto* flags = reinterpret_cast<uint8_t*>(diag.mesh) + pauseOffset;
                diag.pauseBit = (*flags & pauseMask) != 0;
            } else {
                diag.pauseBit = -1;
            }
        }
    }
    if (selectedMesh && pauseOffset >= 0 && pauseMask &&
        IsReadableAddress(reinterpret_cast<uint8_t*>(selectedMesh) + pauseOffset, sizeof(uint8_t))) {
        const uint8_t value = *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(selectedMesh) + pauseOffset);
        g_poseAnimPauseReadback.store((value & pauseMask) ? 1 : 0, std::memory_order_release);
    } else {
        g_poseAnimPauseReadback.store(-1, std::memory_order_release);
    }
}

std::vector<PoseMeshDiagnostic> CollectPoseMeshDiagnostics(void* pawn, void* pawnMesh, std::wstring& status) {
    std::vector<PoseMeshDiagnostic> diagnostics;
    struct TArrayHeader { void* data; int32_t count; int32_t capacity; };
    bool componentsValid[2]{};
    const int offsets[] = {g_actorComponentsOffset, g_actorAllComponentsOffset};
    for (int list = 0; list < 2; ++list) {
        TArrayHeader components{};
        const int offset = offsets[list];
        if (pawn && offset >= 0 && IsReadableAddress(reinterpret_cast<uint8_t*>(pawn) + offset, sizeof(components))) {
            memcpy(&components, reinterpret_cast<uint8_t*>(pawn) + offset, sizeof(components));
            componentsValid[list] = components.count >= 0 && components.count <= 512 &&
                components.capacity >= components.count && components.capacity <= 512 &&
                (!components.count || (components.data && IsReadableAddress(components.data, static_cast<size_t>(components.count) * sizeof(void*))));
        }
        if (!componentsValid[list] || !components.data || !g_classSkeletalMeshComponent) continue;
        for (int i = 0; i < components.count && diagnostics.size() < 32; ++i) {
            void* component = nullptr;
            memcpy(&component, reinterpret_cast<uint8_t*>(components.data) + static_cast<size_t>(i) * sizeof(void*), sizeof(component));
            if (!component || !IsA(component, g_classSkeletalMeshComponent)) continue;
            const bool duplicate = std::any_of(diagnostics.begin(), diagnostics.end(), [component](const auto& item) { return item.mesh == component; });
            if (!duplicate) {
                PoseMeshDiagnostic diag{};
                diag.mesh = component;
                diag.sourceIndex = i;
                diag.sourceList = list;
                diagnostics.push_back(diag);
            }
        }
    }
    if (pawnMesh && g_classSkeletalMeshComponent && IsA(pawnMesh, g_classSkeletalMeshComponent)) {
        const bool found = std::any_of(diagnostics.begin(), diagnostics.end(), [pawnMesh](const auto& item) { return item.mesh == pawnMesh; });
        if (!found && diagnostics.size() < 32) {
            PoseMeshDiagnostic diag{};
            diag.mesh = pawnMesh;
            diagnostics.push_back(diag);
        }
    }
    if (!componentsValid[0] && !componentsValid[1]) status = L"Actor component arrays could not be validated; Pawn.Mesh is checked separately.";
    else {
        wchar_t text[160]{};
        swprintf_s(text, L"Actor.Components valid=%d; AllComponents valid=%d; tracked SkeletalMeshComponents=%d",
            componentsValid[0] ? 1 : 0, componentsValid[1] ? 1 : 0,
            static_cast<int>(diagnostics.size()));
        status = text;
    }
    return diagnostics;
}

int GObjectCount() {
    if (!g_gameBase) return 0;
    auto* p = reinterpret_cast<int*>(g_gameBase + kGObjectsNumOff);
    if (!IsReadableAddress(p, sizeof(int))) return 0;
    const int n = *p;
    return (n > 0 && n <= kGObjectsCapacity) ? n : kGObjectsCapacity;
}

void* SafeGObjectAt(void** objects, int index) {
    if (!objects || index < 0) return nullptr;
    __try { return objects[index]; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* GObjectAtFixedIndex(int index) {
    // Asset indices were recorded in a Steam session, not established for Epic.
    // Do not interpret an unrelated Epic object as a particle module/property.
    if (!g_buildProfile || g_buildProfile != &kBuildProfiles[0]) return nullptr;
    if (!g_gameBase || index < 0 || index >= GObjectCount()) return nullptr;
    auto** objects = reinterpret_cast<void**>(g_gameBase + kGObjects);
    return SafeGObjectAt(objects, index);
}

void SetLightningStatus(const wchar_t* status) {
    std::lock_guard<std::mutex> lock(g_lightningStatusMutex);
    g_lightningStatus = status ? status : L"";
}

std::wstring GetLightningStatus() {
    std::lock_guard<std::mutex> lock(g_lightningStatusMutex);
    return g_lightningStatus;
}

void* FindLiveObjectOfClass(void* cls) {
    if (!g_gameBase || !cls) return nullptr;
    auto** objects = reinterpret_cast<void**>(g_gameBase + kGObjects);
    const int n = GObjectCount();
    for (int i = 0; i < n; ++i) {
        void* obj = SafeGObjectAt(objects, i);
        if (!obj || !IsReadableAddress(obj, 0x4C)) continue;
        const uint32_t flags = Read<uint32_t>(obj, kUObjectFlags);
        if (flags & kRF_ClassDefaultObject) continue;
        if (IsA(obj, cls)) return obj;
    }
    return nullptr;
}

// Fast exact-class GObjects scan used only by the lightning resolver.
// This deliberately mirrors the CE test: raw GObjects pointer walk + direct
// UObject.Class comparison. No VirtualQuery and no superclass walk per object.
void* FindLiveExactClassRaw(void* exactClass) {
    if (!g_gameBase || !exactClass) return nullptr;
    auto** objects = reinterpret_cast<void**>(g_gameBase + kGObjects);
    int n = 0;
    __try { n = *reinterpret_cast<int*>(g_gameBase + kGObjectsNumOff); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if (n <= 0 || n > kGObjectsCapacity) n = kGObjectsCapacity;

    for (int i = 0; i < n; ++i) {
        __try {
            void* obj = objects[i];
            if (!obj) continue;
            if (*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + kUObjectClass) != exactClass) continue;
            const uint32_t flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(obj) + kUObjectFlags);
            if (flags & kRF_ClassDefaultObject) continue;
            return obj;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            // Sparse / stale GObjects entries are simply skipped, exactly as CE does.
        }
    }
    return nullptr;
}

bool IsExactClassRaw(void* obj, void* exactClass) {
    if (!obj || !exactClass) return false;
    __try {
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + kUObjectClass) == exactClass &&
               ((*reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(obj) + kUObjectFlags) & kRF_ClassDefaultObject) == 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProcessEvent(void* object, void* fn, void* params) {
    if (!object || !fn || !g_processEvent) return false;
    __try { g_processEvent(object, fn, params, nullptr); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ProcessScriptEvent(void* object, void* fn, void* params, DWORD* exceptionCode = nullptr) {
    if (exceptionCode) *exceptionCode = 0;
    if (!object || !fn || !g_processEvent || !IsReadableAddress(reinterpret_cast<uint8_t*>(fn) + 168, 2)) return false;
    auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(fn) + 164);
    auto* nativeIndex = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(fn) + 168);
    uint32_t oldFlags = 0;
    uint16_t oldNative = 0;
    __try {
        oldFlags = *flags;
        oldNative = *nativeIndex;
        *flags = (oldFlags & ~0x400u) | 0x2u; // !FUNC_Native, FUNC_Defined
        *nativeIndex = 0;
        g_processEvent(object, fn, params, nullptr);
        *nativeIndex = oldNative;
        *flags = oldFlags;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (exceptionCode) *exceptionCode = GetExceptionCode();
        __try { *nativeIndex = oldNative; *flags = oldFlags; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}


void* GetPlayerController_BmSDKPath();

bool GetIgnoredState(void* pc, void* fn, bool& out) {
    if (!pc || !fn) return false;
    alignas(8) uint8_t params[8]{};
    if (!ProcessEvent(pc, fn, params)) return false;
    uint32_t value = 0;
    memcpy(&value, params, sizeof(value));
    out = value != 0;
    return true;
}

bool SetIgnoredState(void* pc, void* fn, bool value) {
    if (!pc || !fn) return false;
    alignas(8) uint8_t params[8]{};
    const uint32_t v = value ? 1u : 0u;
    memcpy(params, &v, sizeof(v));
    return ProcessEvent(pc, fn, params);
}

void ClearInputBlockSnapshot() {
    g_inputBlockPC = nullptr;
    g_inputSnapshotValid = false;
    g_prevMoveIgnored = false;
    g_prevLookIgnored = false;
    g_prevButtonIgnored = false;
}

void UpdateGameInputBlockOnGameThread() {
    if (!g_fnIgnoreMoveInput || !g_fnIgnoreLookInput || !g_fnIgnoreButtonInput ||
        !g_fnIsMoveInputIgnored || !g_fnIsLookInputIgnored || !g_fnIsButtonInputIgnored) return;
    const bool open = g_uiOpen.load();

    const bool forceRelease = g_forceInputRelease.exchange(false);
    if (!open || forceRelease) {
        // IMPORTANT FPS FIX:
        // Once the UI is closed and our block has already been released, do NOTHING.
        // The previous code re-resolved the PlayerController and fired 4 ProcessEvent
        // calls + a log write on EVERY EngineTick while the UI was hidden.
        // That made the closed-overlay state substantially more expensive than the open one.
        if (!forceRelease && !g_inputBlockPC && !g_inputSnapshotValid)
            return;

        // Closing BAK Tweaks releases OUR block once on the next game tick.
        void* pc = g_inputBlockPC;
        if (!pc || !IsReadableAddress(pc, 0x4C) || !IsA(pc, g_classPlayerController))
            pc = GetPlayerController_BmSDKPath();

        if (pc) {
            SetIgnoredState(pc, g_fnIgnoreMoveInput, false);
            SetIgnoredState(pc, g_fnIgnoreLookInput, false);
            SetIgnoredState(pc, g_fnIgnoreButtonInput, false);
            if (g_fnResetPlayerMovementInput) ProcessEvent(pc, g_fnResetPlayerMovementInput, nullptr);
            Log(L"INPUT UE3: hard release after UI close");
        }
        ClearInputBlockSnapshot();
        return;
    }

    // If the controller changed because of F5/streaming, discard the stale snapshot
    // and capture the replacement controller before blocking it.
    if (!g_inputBlockPC || !IsReadableAddress(g_inputBlockPC, 0x4C) ||
        !IsA(g_inputBlockPC, g_classPlayerController)) {
        ClearInputBlockSnapshot();
        g_inputBlockPC = GetPlayerController_BmSDKPath();
        if (!g_inputBlockPC) return;
    }

    if (!g_inputSnapshotValid) {
        bool move = false, look = false, button = false;
        const bool okMove = GetIgnoredState(g_inputBlockPC, g_fnIsMoveInputIgnored, move);
        const bool okLook = GetIgnoredState(g_inputBlockPC, g_fnIsLookInputIgnored, look);
        const bool okButton = GetIgnoredState(g_inputBlockPC, g_fnIsButtonInputIgnored, button);
        // Only restore states we actually captured. All three functions are expected
        // on this build; if any lookup/call fails, use false as the conservative default.
        g_prevMoveIgnored = okMove ? move : false;
        g_prevLookIgnored = okLook ? look : false;
        g_prevButtonIgnored = okButton ? button : false;
        g_inputSnapshotValid = true;
        Log(L"INPUT UE3: PlayerController captured; hard block active");
    }

    // Re-assert every game tick: Arkham Knight can rewrite these flags during state
    // transitions, gadget changes and camera changes. This keeps BAK Tweaks exclusive.
    SetIgnoredState(g_inputBlockPC, g_fnIgnoreMoveInput, true);
    SetIgnoredState(g_inputBlockPC, g_fnIgnoreLookInput, true);
    SetIgnoredState(g_inputBlockPC, g_fnIgnoreButtonInput, true);
    // Arkham Knight can accumulate a mouse-look delta before/around the ignore flags.
    // Clear it while the overlay is open so dragging/clicking ImGui never nudges the game camera.
    if (g_fnResetPlayerMovementInput) ProcessEvent(g_inputBlockPC, g_fnResetPlayerMovementInput, nullptr);
}

void* GetEngine_BmSDKPath() {
    if (!g_classEngine || !g_fnEngineGetEngine || !g_getDefaultObject || !g_processEvent)
        return nullptr;

    void* cdo = nullptr;
    __try {
        cdo = g_getDefaultObject(g_classEngine, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log(L"TRACE 12: GetDefaultObject(Engine.Engine) exception");
        return nullptr;
    }
    if (!cdo) {
        Log(L"TRACE 12: Engine.Engine CDO null");
        return nullptr;
    }

    alignas(8) uint8_t params[8]{};

    auto* flags = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(g_fnEngineGetEngine) + kUFunctionFlags);
    auto* nativeIndex = reinterpret_cast<uint16_t*>(
        reinterpret_cast<uint8_t*>(g_fnEngineGetEngine) + kUFunctionNativeIndex);

    uint32_t oldFlags{};
    uint16_t oldNative{};

    __try {
        oldFlags = *flags;
        oldNative = *nativeIndex;

        *flags = (oldFlags & ~kFUNC_Native) | kFUNC_Defined;
        *nativeIndex = 0;

        g_processEvent(cdo, g_fnEngineGetEngine, params, nullptr);

        *nativeIndex = oldNative;
        *flags = oldFlags;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {
            *nativeIndex = oldNative;
            *flags = oldFlags;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        Log(L"TRACE 12: Engine.GetEngine ProcessEvent exception");
        return nullptr;
    }

    void* engine = nullptr;
    memcpy(&engine, params + 0, sizeof(void*));
    return engine;
}

void* GetLocalPlayer_BmSDKPath() {
    void* engine = GetEngine_BmSDKPath();
    if (!engine) return nullptr;

    // BmSDK Generated/Engine/_Engine.g.cs: ObjectProperty GameViewport @ +700.
    void* viewport = Read<void*>(engine, 700);
    if (!viewport || !g_fnFindPlayerByControllerId) return nullptr;

    // BmSDK wrapper: params size 16, ControllerId @ +0, return LocalPlayer @ +4.
    alignas(8) uint8_t params[16]{};
    const int controllerId = 0;
    memcpy(params + 0, &controllerId, sizeof(controllerId));
    if (!ProcessEvent(viewport, g_fnFindPlayerByControllerId, params)) return nullptr;

    void* localPlayer = nullptr;
    memcpy(&localPlayer, params + 4, sizeof(void*));
    return localPlayer;
}

bool PatchExposureLevelPPWriter(bool enableOverride) {
    if (!g_gameBase) return false;
    auto* site = reinterpret_cast<uint8_t*>(g_gameBase + kExposureLevelPPWriter);
    static bool hookCreated = false;
    if (!hookCreated) {
        if (!enableOverride) return true;
        if (!IsReadableAddress(site, sizeof(kExposureWriterOriginal)) ||
            memcmp(site, kExposureWriterOriginal, sizeof(kExposureWriterOriginal)) != 0) {
            Log(L"EXPOSURE: unexpected blend-store instruction; override refused");
            return false;
        }
        g_bakExposureWriterContinue = reinterpret_cast<uintptr_t>(site + sizeof(kExposureWriterOriginal));
        void* unusedTrampoline{};
        if (MH_CreateHook(site, &BakExposureWriterHook, &unusedTrampoline) != MH_OK) return false;
        hookCreated = true;
    }
    const MH_STATUS status = enableOverride ? MH_EnableHook(site) : MH_DisableHook(site);
    return status == MH_OK || status == (enableOverride ? MH_ERROR_ENABLED : MH_ERROR_DISABLED);
}

void UpdateExposureOverrideOnGameThread() {
    static float savedLevel{}, savedCurrent{};
    auto readableCaches = [](uintptr_t lp) {
        return lp && IsReadableAddress(reinterpret_cast<void*>(lp + kLocalPlayerLevelPPExposureBaseOffset), sizeof(float)) &&
            IsReadableAddress(reinterpret_cast<void*>(lp + kLocalPlayerCurrentPPExposureBaseOffset), sizeof(float));
    };
    const bool wanted = g_exposureWanted.load();
    bool applied = g_exposureApplied.load();

    if (!wanted) {
        if (applied) {
            if (!PatchExposureLevelPPWriter(false)) return;
            const uintptr_t lp = g_exposureLocalPlayer.load();
            g_bakExposureLevelTarget.store(0);
            g_bakExposureCurrentTarget.store(0);
            if (readableCaches(lp)) {
                *reinterpret_cast<float*>(lp + kLocalPlayerLevelPPExposureBaseOffset) = savedLevel;
                *reinterpret_cast<float*>(lp + kLocalPlayerCurrentPPExposureBaseOffset) = savedCurrent;
            }
            g_exposureApplied.store(false);
            g_exposureLocalPlayer.store(0);
            Log(L"EXPOSURE: LevelPP override disabled; game writer restored");
        }
        return;
    }

    uintptr_t localPlayer = g_exposureLocalPlayer.load();
    if (!readableCaches(localPlayer)) {
        void* lp = GetLocalPlayer_BmSDKPath();
        localPlayer = reinterpret_cast<uintptr_t>(lp);
        if (!readableCaches(localPlayer))
            return;
        g_exposureLocalPlayer.store(localPlayer);
    }

    if (!applied) {
        // Cross-check the SDK cache layout against this executable's reflection.
        void* levelProp = FindObject(L"Engine.LocalPlayer.LevelPPInfo");
        void* currentProp = FindObject(L"Engine.LocalPlayer.CurrentPPInfo");
        if (g_uPropertyOffsetField < 0 || !levelProp || !currentProp ||
            Read<uint16_t>(levelProp, g_uPropertyOffsetField) != 0x310 ||
            Read<uint16_t>(currentProp, g_uPropertyOffsetField) != 0xF4) {
            Log(L"EXPOSURE: LocalPlayer cache layout mismatch; override refused");
            g_exposureWanted.store(false);
            return;
        }
        const float current = *reinterpret_cast<float*>(localPlayer + kLocalPlayerLevelPPExposureBaseOffset);
        savedLevel = current;
        savedCurrent = *reinterpret_cast<float*>(localPlayer + kLocalPlayerCurrentPPExposureBaseOffset);
        if (std::isfinite(current)) g_exposureValue.store(current);
        g_bakExposureDesired.store(g_exposureValue.load());
        g_bakExposureLevelTarget.store(localPlayer + kLocalPlayerLevelPPExposureBaseOffset);
        g_bakExposureCurrentTarget.store(localPlayer + kLocalPlayerCurrentPPExposureBaseOffset);
        if (!PatchExposureLevelPPWriter(true)) {
            g_bakExposureLevelTarget.store(0);
            g_bakExposureCurrentTarget.store(0);
            g_exposureWanted.store(false);
            return;
        }
        g_exposureApplied.store(true);
        applied = true;
        wchar_t msg[192]{};
        swprintf_s(msg, L"EXPOSURE: LevelPP override enabled LP=0x%llX value=%.3f",
                   (unsigned long long)localPlayer, (double)g_exposureValue.load());
        Log(msg);
    }

    if (applied) {
        const float value = g_exposureValue.load();
        if (std::isfinite(value)) {
            g_bakExposureDesired.store(value);
            *reinterpret_cast<float*>(localPlayer + kLocalPlayerLevelPPExposureBaseOffset) = value;
            *reinterpret_cast<float*>(localPlayer + kLocalPlayerCurrentPPExposureBaseOffset) = value;
        }
    }
}

void* GetPlayerController_BmSDKPath() {
    Log(L"TRACE 11: GetEngine begin");

    void* engine = GetEngine_BmSDKPath();
    if (!engine) {
        Log(L"TRACE 11: GetEngine failed");
        return nullptr;
    }

    // BmSDK Generated/Engine/_Engine.g.cs:
    // ObjectProperty GameViewport @ +700.
    void* viewport = Read<void*>(engine, 700);
    if (!viewport) {
        Log(L"TRACE 13: Engine.GameViewport null");
        return nullptr;
    }

    Log(L"TRACE 13: FindPlayerByControllerId begin");

    // BmSDK generated wrapper:
    // params size 16, ControllerId @ +0, return LocalPlayer @ +4.
    alignas(8) uint8_t params[16]{};
    const int controllerId = 0;
    memcpy(params + 0, &controllerId, sizeof(controllerId));

    if (!ProcessEvent(viewport, g_fnFindPlayerByControllerId, params)) {
        Log(L"TRACE 13: FindPlayerByControllerId failed");
        return nullptr;
    }

    void* localPlayer = nullptr;
    memcpy(&localPlayer, params + 4, sizeof(void*));
    if (!localPlayer) {
        Log(L"TRACE 14: LocalPlayer null");
        return nullptr;
    }

    // BmSDK Generated/Engine/Player.g.cs:
    // ObjectProperty Actor @ +92.
    void* pc = Read<void*>(localPlayer, 92);
    if (!pc) {
        Log(L"TRACE 14: LocalPlayer.Actor null");
        return nullptr;
    }

    wchar_t msg[256]{};
    swprintf_s(msg,
        L"TRACE 14: Engine=0x%llX Viewport=0x%llX LocalPlayer=0x%llX PC=0x%llX",
        (unsigned long long)engine,
        (unsigned long long)viewport,
        (unsigned long long)localPlayer,
        (unsigned long long)pc);
    Log(msg);

    return pc;
}


bool GetRenderedCameraFromCache(Vec3& location, RotatorRaw& rotation, float& fov) {
    Log(L"CAM 00: CameraCache read begin");
    void* pc = GetPlayerController_BmSDKPath();
    if (!pc) { Log(L"CAM 01 FAIL: no PlayerController"); return false; }

    void* camera = Read<void*>(pc, kPlayerControllerPlayerCamera);
    if (!camera || !IsReadableAddress(reinterpret_cast<uint8_t*>(camera)+kCameraCache+kCameraCachePOV, 28)) {
        Log(L"CAM 02 FAIL: PlayerCamera/CameraCache invalid");
        return false;
    }

    const ptrdiff_t pov = kCameraCache + kCameraCachePOV;
    location = Read<Vec3>(camera, pov + 0x00);
    rotation = Read<RotatorRaw>(camera, pov + 0x0C);
    fov = Read<float>(camera, pov + 0x18);

    wchar_t msg[256]{};
    swprintf_s(msg,L"CAM 03: PlayerCamera=0x%llX XYZ %.2f %.2f %.2f rot %d %d %d FOV %.2f",
        (unsigned long long)camera, location.x, location.y, location.z,
        rotation.pitch, rotation.yaw, rotation.roll, fov);
    Log(msg);

    return std::isfinite(location.x)&&std::isfinite(location.y)&&std::isfinite(location.z)&&std::isfinite(fov);
}

bool GetPlayerView(Vec3& location, RotatorRaw& rotation) {
    Log(L"TRACE 10: GetPlayerView begin");

    // Do NOT scan the 785000-entry GObjObjects array.
    // Match BmSDK exactly:
    // GetEngine -> GameViewport -> FindPlayerByControllerId(0) -> LocalPlayer.Actor.
    void* pc = GetPlayerController_BmSDKPath();
    if (!pc || !g_fnGetPlayerViewPoint) {
        Log(L"TRACE 15: PlayerController/GetPlayerViewPoint unavailable");
        return false;
    }

    g_lastPlayerController = pc;

    Log(L"TRACE 15: GetPlayerViewPoint ProcessEvent begin");
    alignas(8) uint8_t params[32]{};

    if (!ProcessEvent(pc, g_fnGetPlayerViewPoint, params)) {
        Log(L"TRACE 15: GetPlayerViewPoint failed");
        return false;
    }

    memcpy(&location, params + 0, sizeof(location));
    memcpy(&rotation, params + 12, sizeof(rotation));

    wchar_t msg[192]{};
    swprintf_s(msg, L"TRACE 16: View OK %.2f %.2f %.2f rot %d %d %d",
        location.x, location.y, location.z,
        rotation.pitch, rotation.yaw, rotation.roll);
    Log(msg);

    return std::isfinite(location.x) &&
           std::isfinite(location.y) &&
           std::isfinite(location.z);
}

void* GetWorldInfo_BmSDKPath() {
    // 1:1 transliteration of generated Engine.WorldInfo.GetWorldInfo():
    // - resolve Engine.WorldInfo.GetWorldInfo
    // - get Engine.WorldInfo class default object
    // - temporarily clear FUNC_Native, set FUNC_Defined, iNative = 0
    // - ProcessEvent(CDO,...)
    // - restore UFunction fields
    // - return params + 0
    if (!g_classWorldInfo || !g_fnWorldInfoGetWorldInfo || !g_getDefaultObject || !g_processEvent)
        return nullptr;

    void* cdo = nullptr;
    __try {
        cdo = g_getDefaultObject(g_classWorldInfo, 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log(L"BatLit BmSDKPath: GetDefaultObject(WorldInfo) raised an exception");
        return nullptr;
    }

    if (!cdo) {
        Log(L"BatLit BmSDKPath: WorldInfo CDO is null");
        return nullptr;
    }

    alignas(8) uint8_t params[8]{};

    if (!IsReadableAddress(reinterpret_cast<uint8_t*>(g_fnWorldInfoGetWorldInfo) + kUFunctionNativeIndex, sizeof(uint16_t))) {
        Log(L"BatLit BmSDKPath: WorldInfo.GetWorldInfo UFunction layout unreadable");
        return nullptr;
    }

    auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(g_fnWorldInfoGetWorldInfo) + kUFunctionFlags);
    auto* nativeIndex = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(g_fnWorldInfoGetWorldInfo) + kUFunctionNativeIndex);

    uint32_t oldFlags{};
    uint16_t oldNative{};

    __try {
        oldFlags = *flags;
        oldNative = *nativeIndex;

        *flags = (oldFlags & ~kFUNC_Native) | kFUNC_Defined;
        *nativeIndex = 0;

        g_processEvent(cdo, g_fnWorldInfoGetWorldInfo, params, nullptr);

        *nativeIndex = oldNative;
        *flags = oldFlags;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        __try {
            *nativeIndex = oldNative;
            *flags = oldFlags;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        Log(L"BatLit BmSDKPath: WorldInfo.GetWorldInfo ProcessEvent raised an exception");
        return nullptr;
    }

    void* wi = nullptr;
    memcpy(&wi, params + 0, sizeof(void*));
    return wi;
}

void* GetActiveWorld() {
    // Exact BmSDK Game.SpawnActorInternal world path:
    //   var world = (World)GetWorldInfo().Outer.Outer;
    void* wi = GetWorldInfo_BmSDKPath();
    if (!wi) {
        Log(L"BatLit BmSDKPath: GetWorldInfo() returned null");
        return nullptr;
    }

    void* outer1 = Read<void*>(wi, kUObjectOuter);
    if (!outer1) {
        Log(L"BatLit BmSDKPath: GetWorldInfo().Outer is null");
        return nullptr;
    }

    void* world = Read<void*>(outer1, kUObjectOuter);
    if (!world) {
        Log(L"BatLit BmSDKPath: GetWorldInfo().Outer.Outer is null");
        return nullptr;
    }

    wchar_t msg[160]{};
    swprintf_s(msg, L"BatLit BmSDKPath: WorldInfo=0x%llX World=0x%llX",
               (unsigned long long)wi, (unsigned long long)world);
    Log(msg);
    return world;
}

struct LightState {
    bool enabled{true};
    float color[3]{1.0f,1.0f,1.0f};
    float intensity{1.0f};
    float lightBulbRadius{};
    float radiosity{};
    float radiosityDiffuse{};
    float shadowUpdatePriority{};
    float cullAreaMultiplier{};
    float layer0Multiplier{};
    float layer1Multiplier{};
    float layer01Multiplier{};
    float signalSmoothingHigh{};
    float signalSmoothingLow{};
    float effectorIntensity{};
    float effectorColor[3]{1.0f,1.0f,1.0f};

    bool umbraDoNotCull{};
    bool radiosityOnly{};
    bool castShadows{};
    bool castStaticShadows{};
    bool castDynamicShadows{};
    bool castCompositeShadow{};
    bool affectCompositeShadowDirection{};
    bool nonModulatedSelfShadowing{};
    bool selfShadowOnly{};
    bool allowPreShadow{};
    bool forceDynamicLight{};
    bool useDirectLightMap{};
    bool affectDynamicOutsideChannel{};
    bool renderLightShafts{};
    bool drawAsVolume{};
    bool dontAffectLightVolume{};
    bool dontAffectForwardLitTransparencies{};
    bool forceDynamicShadows{};
    bool cheapLight{};
    bool ambientOnly{};
    bool xray{};
    bool alsoXray{};
    bool notInCinematics{};
    bool lod2Fade{};
    bool explicitlyAssigned{};
    bool allowCompositingIntoDLE{};
    bool cinematicLightType{};

    bool channelInitialized{};
    bool channelBSP{};
    bool channelStatic{};
    bool channelDynamic{};
    bool channelCompositeDynamic{};
    bool channelSkybox{};

    int lightAffectsClassification{};
    int shadowProjectionTechnique{};
    int shadowFilterQuality{};
    int depthPriorityGroup{};
    float coreSize{};
    float omniFadeRadiusFactor{};
    float occlusionDepthRange{};
    float bloomScale{};
    float bloomThreshold{};
    float bloomScreenBlendThreshold{};
    float bloomTint[3]{1.0f,1.0f,1.0f};
    float radialBlurPercent{};
    float occlusionMaskDarkness{};
    float shadowResolutionMultiplier{};

    float shadowRadiusMultiplier{};
    float radius{};
    float falloffExponent{};
    float shadowFalloffExponent{};
    float minShadowFalloffRadius{};
    bool quadraticFalloff{};
    Vec3 translation{};

    float innerConeAngle{};
    float outerConeAngle{};
    float lightShaftConeAngle{};
    float pitch{}, yaw{}, roll{};
    bool absoluteRotation{};
    bool nvidiaLightShafts{};
    bool castOpacityShadows{};
    int qualityLevel{};
    int phaseFunction{};
    float distanceScale{};
    float gelThreshold{};
    float scatter{};
    float absorption{};
    float phase{};
    float shaftIntensity{};
    float opacityShadowNearDepth{};
    float opacityShadowOpacityMultiplier{};
    float opacityShadowAmbientSelf{};
    float opacityShadowAmbientProjected{};
};

enum LiveLightChange : uint32_t {
    LiveEnabled = 1u << 0, LiveColor = 1u << 1, LiveIntensity = 1u << 2,
    LiveRadius = 1u << 3, LiveBulb = 1u << 4, LiveFalloff = 1u << 5,
    LiveQuadratic = 1u << 6, LivePosition = 1u << 7, LiveRotation = 1u << 8,
    LiveCone = 1u << 9, LiveShaftCone = 1u << 10, LiveAbsoluteRotation = 1u << 11
};

struct LightRecord {
    uint32_t id{};
    bool isSpot{};
    int ordinal{};
    void* actor{};
    void* component{};
    void* spawnWorld{};
    void* spawnWorldInfo{};
    std::string name;
    bool editOpen{};
    bool editorLoggedOpen{};
    bool gizmoVisible{true};
    bool valid{};
    bool statePending{};
    int stateRetryTicks{};
    uint32_t liveDirty{};
    bool liveEditFailed{};
    LightState state{};
    LightState defaults{};
};

std::vector<LightRecord> g_lights;
std::mutex g_lightMutex;
uint32_t g_nextLightId = 1;
int g_nextSpotOrdinal = 1;
int g_nextPointOrdinal = 1;

enum class CommandType { SpawnSpot, SpawnPoint, Apply, Reset, MoveToCamera, ToggleEnabled };
struct Command { CommandType type; uint32_t id{}; bool boolValue{}; };
std::vector<Command> g_commands;
std::mutex g_commandMutex;

bool GetActiveWorldSnapshotQuiet(void*& outWorldInfo, void*& outWorld) {
    outWorldInfo = nullptr;
    outWorld = nullptr;

    void* wi = GetWorldInfo_BmSDKPath();
    if (!wi || !IsReadableAddress(reinterpret_cast<uint8_t*>(wi) + kUObjectOuter, sizeof(void*)))
        return false;

    void* outer1 = Read<void*>(wi, kUObjectOuter);
    if (!outer1 || !IsReadableAddress(reinterpret_cast<uint8_t*>(outer1) + kUObjectOuter, sizeof(void*)))
        return false;

    void* world = Read<void*>(outer1, kUObjectOuter);
    if (!world) return false;

    outWorldInfo = wi;
    outWorld = world;
    return true;
}

void* g_trackedWorld = nullptr;
void* g_trackedWorldInfo = nullptr;
uint32_t g_worldCheckCounter = 0;

bool DestroyTrackedLightActor(LightRecord& rec) {
    if (!rec.actor || !IsReadableAddress(rec.actor, 0xE0))
        return false;

    if (!g_fnActorDestroy)
        return false;

    // Engine.Actor.Destroy() returns a bool, but the return value is irrelevant here.
    // A small zeroed buffer is sufficient for the reflected call.
    alignas(8) uint8_t params[8]{};
    return ProcessScriptEvent(rec.actor, g_fnActorDestroy, params);
}

void CheckForLevelRestartAndCleanupLights() {
    // Keep this deliberately cheap: only inspect World/WorldInfo twice per second
    // at ~60 FPS. This avoids undoing the closed-UI FPS fix.
    if (++g_worldCheckCounter < 30)
        return;
    g_worldCheckCounter = 0;

    void* wi = nullptr;
    void* world = nullptr;
    if (!GetActiveWorldSnapshotQuiet(wi, world))
        return;

    if (!g_trackedWorld && !g_trackedWorldInfo) {
        g_trackedWorld = world;
        g_trackedWorldInfo = wi;
        return;
    }

    // RestartLevel / map reload can change WorldInfo even when the surrounding
    // world hierarchy is reused, so compare BOTH pointers.
    if (world == g_trackedWorld && wi == g_trackedWorldInfo)
        return;

    {
        wchar_t msg[256]{};
        swprintf_s(msg,
            L"BatLit: level/world changed (WI 0x%llX -> 0x%llX, World 0x%llX -> 0x%llX). Removing spawned lights.",
            (unsigned long long)g_trackedWorldInfo,
            (unsigned long long)wi,
            (unsigned long long)g_trackedWorld,
            (unsigned long long)world);
        Log(msg);
    }

    int destroyed = 0;
    int tracked = 0;
    {
        std::lock_guard<std::mutex> lock(g_lightMutex);
        tracked = static_cast<int>(g_lights.size());

        // Destroy the spawned actors if UE3 still exposes them. If the old level
        // already destroyed one, simply drop the stale record.
        for (auto& rec : g_lights) {
            if (DestroyTrackedLightActor(rec))
                ++destroyed;
            rec.valid = false;
        }

        g_lights.clear();
        g_nextLightId = 1;
        g_nextSpotOrdinal = 1;
        g_nextPointOrdinal = 1;
    }

    // Never let a gizmo continue referencing a light from the old level.
    g_gizmoLightId = 0;

    // Discard commands that were queued for old light IDs.
    {
        std::lock_guard<std::mutex> lock(g_commandMutex);
        g_commands.clear();
    }

    g_trackedWorld = world;
    g_trackedWorldInfo = wi;

    // The pose hook must never retain a SkeletalMeshComponent across a level reload.
    RestorePoseAnimationPause();
    g_poseEditDesired.store(false, std::memory_order_release);
    g_poseFreezeDesired.store(false, std::memory_order_release);
    g_poseEditActive.store(false, std::memory_order_release);
    g_poseAnimPauseReadback.store(-1, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        g_poseFrozenSnapshot.clear();
        g_poseFrozenMesh = nullptr;
        g_poseFrozenCount = 0;
        g_poseFrozenSnapshotReady = false;
        g_poseFrozenComponents.clear();
        ClearPoseFastFrozenBuffers();
        g_poseFrozenComponentCount.store(0, std::memory_order_relaxed);
        g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        g_posePawn = nullptr;
        g_poseMesh = nullptr;
        g_poseTraceSelectedMesh.store(0, std::memory_order_release);
        g_poseBones.clear();
        g_poseDiagnosticPawn = nullptr;
        g_poseMeshDiagnostics.clear();
        g_poseDiagnosticStatus = L"World changed; refresh the character list.";
        g_poseSelectedTransform = {};
    }

    wchar_t msg[192]{};
    swprintf_s(msg, L"BatLit: level cleanup complete. tracked=%d destroyed=%d", tracked, destroyed);
    Log(msg);
}


void Queue(CommandType type, uint32_t id = 0, bool value = false) {
    std::lock_guard<std::mutex> lock(g_commandMutex);
    g_commands.push_back({type,id,value});
}

void LogLightRecord(const wchar_t* tag, const LightRecord& rec) {
    wchar_t msg[512]{};
    swprintf_s(msg, L"[BatLitDiag] %s id=%u name=%S actor=0x%llX component=0x%llX valid=%d pending=%d retry=%d edit=%d intensity=%.3f radius=%.3f enabled=%d",
        tag ? tag : L"(null)",
        rec.id, rec.name.c_str(),
        (unsigned long long)rec.actor, (unsigned long long)rec.component,
        rec.valid ? 1 : 0, rec.statePending ? 1 : 0, rec.stateRetryTicks, rec.editOpen ? 1 : 0,
        rec.state.intensity, rec.state.radius, rec.state.enabled ? 1 : 0);
    Log(msg);
}

uint8_t FloatToByte(float v) {
    v = (std::max)(0.0f, (std::min)(1.0f, v));
    return static_cast<uint8_t>(std::lround(v * 255.0f));
}

void ReadColor(void* component, ptrdiff_t off, float out[3]) {
    const auto c = Read<ColorBGRA>(component, off);
    out[0] = c.r / 255.0f; out[1] = c.g / 255.0f; out[2] = c.b / 255.0f;
}
void WriteColor(void* component, ptrdiff_t off, const float in[3]) {
    auto c = Read<ColorBGRA>(component, off);
    c.r = FloatToByte(in[0]); c.g = FloatToByte(in[1]); c.b = FloatToByte(in[2]); c.a = 255;
    Write(component, off, c);
}

bool ReadState(LightRecord& rec, LightState& st) {
    void* c = rec.component;
    if (!c || !IsReadableAddress(c, rec.isSpot ? 0x334 : 0x2DC) ||
        !IsReadableAddress(rec.actor, kActorLocation + sizeof(Vec3))) return false;
    const uint32_t f = Read<uint32_t>(c, 0x220);
    st.enabled = (f & 0x4) != 0;
    ReadColor(c, 0x200, st.color);
    st.intensity = Read<float>(c,0x1F0);
    st.lightBulbRadius = Read<float>(c,0x1EC);
    st.radiosity = Read<float>(c,0x1F4);
    st.radiosityDiffuse = Read<float>(c,0x1F8);
    st.shadowUpdatePriority = Read<float>(c,0x1FC);
    st.cullAreaMultiplier = Read<float>(c,0x204);
    st.layer0Multiplier = Read<float>(c,0x104);
    st.layer1Multiplier = Read<float>(c,0x108);
    st.layer01Multiplier = Read<float>(c,0x10C);
    st.signalSmoothingHigh = Read<float>(c,0x1B0);
    st.signalSmoothingLow = Read<float>(c,0x1B4);
    st.effectorIntensity = Read<float>(c,0x1B8);
    ReadColor(c,0x1BC,st.effectorColor);
    st.umbraDoNotCull=(f&0x1)!=0; st.radiosityOnly=(f&0x2)!=0;
    st.castShadows=(f&0x10)!=0; st.castStaticShadows=(f&0x20)!=0; st.castDynamicShadows=(f&0x40)!=0;
    st.castCompositeShadow=(f&0x80)!=0; st.affectCompositeShadowDirection=(f&0x100)!=0;
    st.nonModulatedSelfShadowing=(f&0x200)!=0; st.selfShadowOnly=(f&0x400)!=0; st.allowPreShadow=(f&0x800)!=0;
    st.forceDynamicLight=(f&0x1000)!=0; st.useDirectLightMap=(f&0x2000)!=0;
    st.affectDynamicOutsideChannel=(f&0x8000)!=0; st.renderLightShafts=(f&0x10000)!=0; st.drawAsVolume=(f&0x20000)!=0;
    st.dontAffectLightVolume=(f&0x40000)!=0; st.dontAffectForwardLitTransparencies=(f&0x80000)!=0;
    st.forceDynamicShadows=(f&0x100000)!=0; st.cheapLight=(f&0x200000)!=0; st.ambientOnly=(f&0x400000)!=0;
    st.xray=(f&0x800000)!=0; st.alsoXray=(f&0x1000000)!=0; st.notInCinematics=(f&0x2000000)!=0;
    st.lod2Fade=(f&0x4000000)!=0; st.explicitlyAssigned=(f&0x10000000)!=0; st.allowCompositingIntoDLE=(f&0x20000000)!=0; st.cinematicLightType=(f&0x40000000)!=0;
    const uint32_t ch = Read<uint32_t>(c,0x224);
    st.channelInitialized=(ch&1)!=0; st.channelBSP=(ch&2)!=0; st.channelStatic=(ch&4)!=0; st.channelDynamic=(ch&8)!=0; st.channelCompositeDynamic=(ch&16)!=0; st.channelSkybox=(ch&32)!=0;
    st.lightAffectsClassification=Read<uint8_t>(c,0x228); st.shadowProjectionTechnique=Read<uint8_t>(c,0x230); st.shadowFilterQuality=Read<uint8_t>(c,0x231); st.depthPriorityGroup=Read<uint8_t>(c,0x232);
    st.coreSize=Read<float>(c,0x234); st.omniFadeRadiusFactor=Read<float>(c,0x238); st.occlusionDepthRange=Read<float>(c,0x23C);
    st.bloomScale=Read<float>(c,0x240); st.bloomThreshold=Read<float>(c,0x244); st.bloomScreenBlendThreshold=Read<float>(c,0x248); ReadColor(c,0x24C,st.bloomTint);
    st.radialBlurPercent=Read<float>(c,0x250); st.occlusionMaskDarkness=Read<float>(c,0x254); st.shadowResolutionMultiplier=Read<float>(c,0x260);
    st.shadowRadiusMultiplier=Read<float>(c,0x264); st.radius=Read<float>(c,0x268); st.falloffExponent=Read<float>(c,0x26C); st.shadowFalloffExponent=Read<float>(c,0x270); st.minShadowFalloffRadius=Read<float>(c,0x274); st.quadraticFalloff=(Read<uint32_t>(c,0x278)&1)!=0; st.translation=Read<Vec3>(c,0x2C0);
    // The editor and gizmo use actor WORLD coordinates. Component translation is
    // local to the actor and must not be confused with its world-space origin.
    st.translation = Read<Vec3>(rec.actor, kActorLocation);
    if (rec.isSpot) {
        st.innerConeAngle=Read<float>(c,0x2DC); st.outerConeAngle=Read<float>(c,0x2E0); st.lightShaftConeAngle=Read<float>(c,0x2E4);
        const RotatorRaw rr=Read<RotatorRaw>(c,0x2F8); st.pitch=RuuToDegrees(rr.pitch); st.yaw=RuuToDegrees(rr.yaw); st.roll=RuuToDegrees(rr.roll);
        const uint32_t sf=Read<uint32_t>(c,0x304); st.absoluteRotation=(sf&1)!=0; st.nvidiaLightShafts=(sf&2)!=0; st.castOpacityShadows=(sf&4)!=0;
        st.qualityLevel=Read<uint8_t>(c,0x308); st.phaseFunction=Read<uint8_t>(c,0x309);
        st.distanceScale=Read<float>(c,0x30C); st.gelThreshold=Read<float>(c,0x310); st.scatter=Read<float>(c,0x314); st.absorption=Read<float>(c,0x318); st.phase=Read<float>(c,0x31C); st.shaftIntensity=Read<float>(c,0x320);
        st.opacityShadowNearDepth=Read<float>(c,0x324); st.opacityShadowOpacityMultiplier=Read<float>(c,0x328); st.opacityShadowAmbientSelf=Read<float>(c,0x32C); st.opacityShadowAmbientProjected=Read<float>(c,0x330);
    }
    return true;
}

void SetBit(uint32_t& flags, uint32_t mask, bool value) { flags = value ? (flags|mask) : (flags&~mask); }

bool SetEnabledImmediate(LightRecord& rec, bool enabled) {
    void* c = rec.component;
    if (!c || !IsReadableAddress(c, rec.isSpot ? 0x334 : 0x2DC)) {
        rec.valid = false;
        Log(L"[BatLitEnable] invalid component");
        return false;
    }

    const uint32_t flagsBefore = Read<uint32_t>(c, 0x220);
    const uint32_t compFlagsBefore = Read<uint32_t>(c, 0x40);
    wchar_t before[384]{};
    swprintf_s(before, L"[BatLitEnable] id=%u component=0x%llX request=%s Flags220(before)=%08X Flags40(before)=%08X fn=0x%llX",
        rec.id, (unsigned long long)c, enabled ? L"ON" : L"OFF", flagsBefore, compFlagsBefore,
        (unsigned long long)g_fnSetEnabled);
    Log(before);

    bool peOk = false;
    if (g_fnSetEnabled) {
        // IMPORTANT: do not pre-write bEnabled. SetEnabled() must see the state
        // transition itself so UE3 can update/reattach the renderer-side light.
        struct PEnable { int32_t b{}; } pe{enabled ? 1 : 0};
        peOk = ProcessScriptEvent(c, g_fnSetEnabled, &pe);
    } else {
        // Fallback only if the UFunction could not be resolved.
        uint32_t flags = flagsBefore;
        SetBit(flags, 0x4, enabled);
        Write(c, 0x220, flags);
    }

    const uint32_t flagsAfter = Read<uint32_t>(c, 0x220);
    const uint32_t compFlagsAfter = Read<uint32_t>(c, 0x40);
    wchar_t after[384]{};
    swprintf_s(after, L"[BatLitEnable] ProcessEvent=%d Flags220(after)=%08X Flags40(after)=%08X enabledBit=%d",
        peOk ? 1 : 0, flagsAfter, compFlagsAfter, (flagsAfter & 0x4) ? 1 : 0);
    Log(after);

    rec.state.enabled = enabled;
    return g_fnSetEnabled ? peOk : true;
}

bool SetLightWorldPosition(LightRecord& rec, const Vec3& position) {
    if (!IsReadableAddress(rec.actor, kActorLocation + sizeof(Vec3)) ||
        !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return false;
    struct Params { Vec3 location; uint32_t result{}; } params{position};
    return ProcessScriptEvent(rec.actor, g_fnActorSetLocation, &params) && params.result != 0;
}

void ApplyState(LightRecord& rec, const LightState& st) {
    LogLightRecord(L"ApplyState begin", rec);
    { wchar_t msg[320]{}; swprintf_s(msg,L"[BatLitDiag] Apply target id=%u component=0x%llX desired intensity=%.3f radius=%.3f enabled=%d",rec.id,(unsigned long long)rec.component,st.intensity,st.radius,st.enabled?1:0); Log(msg); }
    void* c=rec.component; if(!c || !IsReadableAddress(c, rec.isSpot?0x334:0x2DC)) { rec.valid=false; Log(L"[BatLitDiag] ApplyState abort: invalid component"); return; }
    uint32_t f=Read<uint32_t>(c,0x220);
    SetBit(f,0x1,st.umbraDoNotCull); SetBit(f,0x2,st.radiosityOnly); /* bEnabled is handled by LightComponent.SetEnabled() below */ SetBit(f,0x10,st.castShadows); SetBit(f,0x20,st.castStaticShadows); SetBit(f,0x40,st.castDynamicShadows); SetBit(f,0x80,st.castCompositeShadow); SetBit(f,0x100,st.affectCompositeShadowDirection); SetBit(f,0x200,st.nonModulatedSelfShadowing); SetBit(f,0x400,st.selfShadowOnly); SetBit(f,0x800,st.allowPreShadow); SetBit(f,0x1000,st.forceDynamicLight); SetBit(f,0x2000,st.useDirectLightMap); SetBit(f,0x8000,st.affectDynamicOutsideChannel); SetBit(f,0x10000,st.renderLightShafts); SetBit(f,0x20000,st.drawAsVolume); SetBit(f,0x40000,st.dontAffectLightVolume); SetBit(f,0x80000,st.dontAffectForwardLitTransparencies); SetBit(f,0x100000,st.forceDynamicShadows); SetBit(f,0x200000,st.cheapLight); SetBit(f,0x400000,st.ambientOnly); SetBit(f,0x800000,st.xray); SetBit(f,0x1000000,st.alsoXray); SetBit(f,0x2000000,st.notInCinematics); SetBit(f,0x4000000,st.lod2Fade); SetBit(f,0x10000000,st.explicitlyAssigned); SetBit(f,0x20000000,st.allowCompositingIntoDLE); SetBit(f,0x40000000,st.cinematicLightType); Write(c,0x220,f);
    WriteColor(c,0x200,st.color); Write(c,0x1F0,st.intensity); Write(c,0x1EC,st.lightBulbRadius); Write(c,0x1F4,st.radiosity); Write(c,0x1F8,st.radiosityDiffuse); Write(c,0x1FC,st.shadowUpdatePriority); Write(c,0x204,st.cullAreaMultiplier);
    Write(c,0x104,st.layer0Multiplier); Write(c,0x108,st.layer1Multiplier); Write(c,0x10C,st.layer01Multiplier); Write(c,0x1B0,st.signalSmoothingHigh); Write(c,0x1B4,st.signalSmoothingLow); Write(c,0x1B8,st.effectorIntensity); WriteColor(c,0x1BC,st.effectorColor);
    uint32_t ch=Read<uint32_t>(c,0x224); SetBit(ch,1,st.channelInitialized); SetBit(ch,2,st.channelBSP); SetBit(ch,4,st.channelStatic); SetBit(ch,8,st.channelDynamic); SetBit(ch,16,st.channelCompositeDynamic); SetBit(ch,32,st.channelSkybox); Write(c,0x224,ch);
    Write<uint8_t>(c,0x228,(uint8_t)std::clamp(st.lightAffectsClassification,0,6)); Write<uint8_t>(c,0x230,(uint8_t)std::clamp(st.shadowProjectionTechnique,0,5)); Write<uint8_t>(c,0x231,(uint8_t)std::clamp(st.shadowFilterQuality,0,2)); Write<uint8_t>(c,0x232,(uint8_t)std::clamp(st.depthPriorityGroup,0,255));
    Write(c,0x234,st.coreSize); Write(c,0x238,st.omniFadeRadiusFactor); Write(c,0x23C,st.occlusionDepthRange); Write(c,0x240,st.bloomScale); Write(c,0x244,st.bloomThreshold); Write(c,0x248,st.bloomScreenBlendThreshold); WriteColor(c,0x24C,st.bloomTint); Write(c,0x250,st.radialBlurPercent); Write(c,0x254,st.occlusionMaskDarkness); Write(c,0x260,st.shadowResolutionMultiplier);
    Write(c,0x264,st.shadowRadiusMultiplier); Write(c,0x268,st.radius); Write(c,0x26C,st.falloffExponent); Write(c,0x270,st.shadowFalloffExponent); Write(c,0x274,st.minShadowFalloffRadius); uint32_t pf=Read<uint32_t>(c,0x278); SetBit(pf,1,st.quadraticFalloff); Write(c,0x278,pf);

    SetEnabledImmediate(rec, st.enabled);
    uint8_t dummy{};
    ProcessEvent(c,g_fnLightColor,&dummy); ProcessEvent(c,g_fnBrightness,&dummy); ProcessScriptEvent(c,g_fnUpdateColorBrightness,&dummy);

    SetLightWorldPosition(rec, st.translation);
    struct PTranslation { Vec3 v; } pt{Read<Vec3>(c, 0x2C0)}; ProcessScriptEvent(c,g_fnSetTranslation,&pt);
    if(rec.isSpot) {
        struct PAngle { float inner,outer,radius; } pa{st.innerConeAngle,st.outerConeAngle,st.radius}; ProcessScriptEvent(c,g_fnSetAngleRadius,&pa);
        Write(c,0x2E4,st.lightShaftConeAngle);
        const RotatorRaw rr{DegreesToRuu(st.pitch),DegreesToRuu(st.yaw),DegreesToRuu(st.roll)};
        struct PRotation { RotatorRaw r; } pr{rr}; ProcessScriptEvent(c,g_fnSetRotation,&pr);
        struct PAbs { int32_t b; } pab{st.absoluteRotation?1:0}; ProcessScriptEvent(c,g_fnSetAbsoluteRotation,&pab);
        uint32_t sf=Read<uint32_t>(c,0x304); SetBit(sf,1,st.absoluteRotation); SetBit(sf,2,st.nvidiaLightShafts); SetBit(sf,4,st.castOpacityShadows); Write(c,0x304,sf);
        Write<uint8_t>(c,0x308,(uint8_t)std::clamp(st.qualityLevel,0,3)); Write<uint8_t>(c,0x309,(uint8_t)std::clamp(st.phaseFunction,0,3));
        Write(c,0x30C,st.distanceScale); Write(c,0x310,st.gelThreshold); Write(c,0x314,st.scatter); Write(c,0x318,st.absorption); Write(c,0x31C,st.phase); Write(c,0x320,st.shaftIntensity); Write(c,0x324,st.opacityShadowNearDepth); Write(c,0x328,st.opacityShadowOpacityMultiplier); Write(c,0x32C,st.opacityShadowAmbientSelf); Write(c,0x330,st.opacityShadowAmbientProjected); ProcessScriptEvent(c,g_fnUpdateShafts,&dummy);
    }
}

LightRecord* FindRecord(uint32_t id) {
    for(auto& r:g_lights) if(r.id==id) return &r; return nullptr;
}

void ApplyPendingLightEdits() {
    // FEngineLoop::Tick keeps running when UE3 world time is paused. Never call
    // ProcessEvent from Present or re-enter the engine through ProcessInternal.
    std::lock_guard<std::mutex> lock(g_lightMutex);
    for (auto& rec : g_lights) {
        if (!rec.liveDirty || rec.statePending || !rec.valid) continue;
        const uint32_t dirty = rec.liveDirty;
        rec.liveDirty = 0; // Coalesce all UI edits into one update per light/tick.
        void* c = rec.component;
        if (!IsReadableAddress(c, rec.isSpot ? 0x334 : 0x2DC) ||
            !IsReadableAddress(rec.actor, kActorLocation + sizeof(Vec3))) {
            rec.valid = false;
            rec.liveEditFailed = true;
            continue;
        }
        const auto& st = rec.state;
        bool ok = true;
        uint8_t dummy{};
        if (dirty & LiveEnabled) ok &= SetEnabledImmediate(rec, st.enabled);
        if (dirty & LiveColor) {
            WriteColor(c, 0x200, st.color);
            ok &= ProcessEvent(c, g_fnLightColor, &dummy);
        }
        if (dirty & LiveIntensity) {
            Write(c, 0x1F0, st.intensity);
            ok &= ProcessEvent(c, g_fnBrightness, &dummy);
        }
        if (dirty & (LiveColor | LiveIntensity))
            ok &= ProcessScriptEvent(c, g_fnUpdateColorBrightness, &dummy);
        if (dirty & LiveRadius) Write(c, 0x268, st.radius);
        if (dirty & LiveBulb) Write(c, 0x1EC, st.lightBulbRadius);
        if (dirty & LiveFalloff) Write(c, 0x26C, st.falloffExponent);
        if (dirty & LiveQuadratic) {
            uint32_t flags = Read<uint32_t>(c, 0x278);
            SetBit(flags, 1, st.quadraticFalloff);
            Write(c, 0x278, flags);
        }
        if (dirty & LivePosition) {
            ok &= SetLightWorldPosition(rec, st.translation);
            // Reflect the accepted position (including any engine constraints).
            rec.state.translation = Read<Vec3>(rec.actor, kActorLocation);
        }
        if (rec.isSpot) {
            if (dirty & (LiveCone | LiveRadius)) {
                struct Params { float inner, outer, radius; } p{st.innerConeAngle, st.outerConeAngle, st.radius};
                ok &= ProcessScriptEvent(c, g_fnSetAngleRadius, &p);
            }
            if (dirty & LiveRotation) {
                struct Params { RotatorRaw rotation; } p{{DegreesToRuu(st.pitch), DegreesToRuu(st.yaw), DegreesToRuu(st.roll)}};
                ok &= ProcessScriptEvent(c, g_fnSetRotation, &p);
            }
            if (dirty & LiveAbsoluteRotation) {
                struct Params { int32_t enabled; } p{st.absoluteRotation ? 1 : 0};
                ok &= ProcessScriptEvent(c, g_fnSetAbsoluteRotation, &p);
            }
            if (dirty & LiveShaftCone) {
                Write(c, 0x2E4, st.lightShaftConeAngle);
                ok &= ProcessScriptEvent(c, g_fnUpdateShafts, &dummy);
            }
        }
        if (dirty & (LiveRadius | LiveBulb | LiveFalloff | LiveQuadratic)) {
            // Retain the existing Apply path's component-refresh operation without
            // overwriting advanced/hidden fields or moving the local component.
            struct Params { Vec3 local; } p{Read<Vec3>(c, 0x2C0)};
            ok &= ProcessScriptEvent(c, g_fnSetTranslation, &p);
        }
        if (!ok && !rec.liveEditFailed) LogLightRecord(L"Live light update failed", rec);
        rec.liveEditFailed = !ok;
    }
}

void* SafeSpawnActor_BmSDKPath(void* world, void* cls, FNameRaw name, Vec3* loc, RotatorRaw* rot) {
    if (!g_spawnActor) return nullptr;

    Log(L"BatLit BmSDKPath: entering UWorld::SpawnActor");
    void* result = nullptr;

    __try {
        // 1:1 with BmSDK Framework.Game.SpawnActorInternal:
        // world.Ptr, Class.Ptr, Name, &Location, &Rotation,
        // Template=0, bNoCollisionFail=1, bRemoteOwned=0,
        // Owner=0, Instigator=0, bNoFail=1, OverrideLevel=0
        result = g_spawnActor(
            world,
            cls,
            name,
            loc,
            rot,
            nullptr,
            1,
            0,
            nullptr,
            nullptr,
            1,
            nullptr
        );
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log(L"BatLit BmSDKPath: UWorld::SpawnActor raised an exception");
        return nullptr;
    }

    Log(result ? L"BatLit BmSDKPath: UWorld::SpawnActor returned actor"
               : L"BatLit BmSDKPath: UWorld::SpawnActor returned null");
    return result;
}

bool Spawn(bool spot) {
    Log(spot ? L"[BatLitDiag] Spawn begin type=Spot" : L"[BatLitDiag] Spawn begin type=Point");
    Log(spot ? L"BatLit BmSDKPath: Create Spot request" : L"BatLit BmSDKPath: Create Point request");

    Vec3 loc{}; RotatorRaw rot{};
    if(!GetPlayerView(loc,rot)) { Log(L"BatLit BmSDKPath: player view not found"); return false; }

    {
        wchar_t msg[192]{};
        swprintf_s(msg, L"BatLit BmSDKPath: camera %.2f %.2f %.2f rot %d %d %d",
                   loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll);
        Log(msg);
    }

    void* world=GetActiveWorld(); void* cls=spot?g_classSpotLightMovable:g_classPointLightMovable;
    if(!world||!cls||!g_spawnActor) { Log(L"BatLit: world/class/spawn unavailable"); return false; }

    if (!g_trackedWorld) {
        g_trackedWorld = world;
        g_trackedWorldInfo = GetWorldInfo_BmSDKPath();
    }
    FNameRaw none{}; void* actor=SafeSpawnActor_BmSDKPath(world,cls,none,&loc,&rot);
    if(!actor||!IsReadableAddress(reinterpret_cast<uint8_t*>(actor)+kLightActorComponent,sizeof(void*))) { Log(L"BatLit: SpawnActor failed"); return false; }
    void* component=Read<void*>(actor,kLightActorComponent);
    { wchar_t msg[256]{}; swprintf_s(msg,L"[BatLitDiag] Spawn result actor=0x%llX component=0x%llX",(unsigned long long)actor,(unsigned long long)component); Log(msg); }
    if(!component) { Log(L"BatLit: spawned actor has no LightComponent"); return false; }
    LightRecord rec{}; rec.id=g_nextLightId++; rec.isSpot=spot; rec.ordinal=spot?g_nextSpotOrdinal++:g_nextPointOrdinal++; rec.actor=actor; rec.component=component; rec.spawnWorld=world; rec.spawnWorldInfo=GetWorldInfo_BmSDKPath(); rec.name=(spot?"Spot ":"Point ")+std::to_string(rec.ordinal); rec.valid=true;

    // The first spawned light can exist before its component is fully ready for
    // our full state snapshot. Do not throw the record away in that case.
    // Add it immediately, then retry ReadState from later engine ticks.
    if (ReadState(rec, rec.defaults)) {
        rec.state = rec.defaults;
        rec.statePending = false;
        rec.stateRetryTicks = 0;
        Log(L"BatLit SAFE: initial state captured on spawn tick");
    } else {
        rec.statePending = true;
        rec.stateRetryTicks = 0;
        Log(L"BatLit SAFE: light spawned; state capture deferred to later ticks");
    }

    g_lights.push_back(rec);
    LogLightRecord(L"record pushed", g_lights.back());
    wchar_t msg[192]{}; swprintf_s(msg,L"BatLit: spawned %s %d actor=0x%llX component=0x%llX%s",spot?L"Spot":L"Point",rec.ordinal,(unsigned long long)actor,(unsigned long long)component,rec.statePending?L" [state pending]":L""); Log(msg);
    return true;
}

void UpdatePendingLightRecords() {
    std::lock_guard<std::mutex> lock(g_lightMutex);
    for (auto& rec : g_lights) {
        if (!rec.valid || !rec.statePending) continue;

        // Give the newly spawned UE3 component a tick before the first retry,
        // then retry for a short bounded window without sleeping the game thread.
        if (rec.stateRetryTicks++ == 0) continue;

        LightState captured{};
        { wchar_t msg[256]{}; swprintf_s(msg,L"[BatLitDiag] retry ReadState id=%u tick=%d component=0x%llX",rec.id,rec.stateRetryTicks,(unsigned long long)rec.component); Log(msg); }
        if (ReadState(rec, captured)) {
            rec.defaults = captured;
            rec.state = captured;
            rec.statePending = false;
            LogLightRecord(L"deferred ReadState ready", rec);

            wchar_t msg[160]{};
            swprintf_s(msg, L"BatLit: deferred state capture ready for %s %d after %d tick(s)",
                       rec.isSpot ? L"Spot" : L"Point", rec.ordinal, rec.stateRetryTicks);
            Log(msg);
            continue;
        }

        if (rec.stateRetryTicks >= 10) {
            // Keep the light record/edit window alive. The user can still move it
            // and a later explicit action can refresh state; just stop hammering it.
            rec.statePending = false;
            Log(L"BatLit: deferred state capture timed out; keeping spawned light record");
        }
    }
}

void MoveRecordToCamera(LightRecord& rec) {
    wchar_t hdr[192]{};
    swprintf_s(hdr, L"MOVE 00: %s %d request actor=0x%llX component=0x%llX",
               rec.isSpot ? L"Spot" : L"Point",
               rec.ordinal,
               (unsigned long long)rec.actor,
               (unsigned long long)rec.component);
    Log(hdr);

    if (!rec.component || !IsReadableAddress(rec.component, rec.isSpot ? 0x334 : 0x2DC)) {
        Log(L"MOVE 01 FAIL: component invalid/unreadable");
        rec.valid = false;
        return;
    }

    Vec3 loc{};
    RotatorRaw rot{};
    float fov = 0.0f;
    bool fromCache = GetRenderedCameraFromCache(loc, rot, fov);
    if (!fromCache) {
        Log(L"MOVE 02: CameraCache unavailable, fallback GetPlayerViewPoint");
        if (!GetPlayerView(loc, rot)) {
            Log(L"MOVE 03 FAIL: no camera transform");
            return;
        }
    }

    {
        wchar_t msg[256]{};
        swprintf_s(msg, L"MOVE 10: SOURCE=%s XYZ %.2f %.2f %.2f rot %d %d %d FOV %.2f",
                   fromCache ? L"CameraCache" : L"GetPlayerViewPoint",
                   loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll, fov);
        Log(msg);
    }

    {
        Vec3 viewLoc{}; RotatorRaw viewRot{};
        if (GetPlayerView(viewLoc, viewRot)) {
            wchar_t msg[320]{};
            swprintf_s(msg, L"CAM CMP: cache-vs-view delta %.2f %.2f %.2f | cache rot %d %d %d | view rot %d %d %d",
                loc.x-viewLoc.x, loc.y-viewLoc.y, loc.z-viewLoc.z,
                rot.pitch,rot.yaw,rot.roll,viewRot.pitch,viewRot.yaw,viewRot.roll);
            Log(msg);
        }
    }

    // IMPORTANT: PointLightComponent.SetTranslation() is component-relative.
    // Move the spawned Light Actor itself to the camera world transform.
    struct PActorLocation {
        Vec3 newLocation;
        uint32_t returnValue;
    } pl{};
    pl.newLocation = loc;

    Log(L"MOVE 20: Actor.SetLocation begin");
    const bool moved = ProcessScriptEvent(rec.actor, g_fnActorSetLocation, &pl);
    Log(moved ? L"MOVE 21: Actor.SetLocation returned"
              : L"MOVE 21 FAIL: Actor.SetLocation ProcessEvent failed");

    struct PActorRotation {
        RotatorRaw newRotation;
        uint32_t returnValue;
    } pr{};
    pr.newRotation = rot;

    Log(L"MOVE 30: Actor.SetRotation begin");
    const bool rotated = ProcessScriptEvent(rec.actor, g_fnActorSetRotation, &pr);
    Log(rotated ? L"MOVE 31: Actor.SetRotation returned"
                : L"MOVE 31 FAIL: Actor.SetRotation ProcessEvent failed");

    const Vec3 actorPos = Read<Vec3>(rec.actor, kActorLocation);
    const RotatorRaw actorRot = Read<RotatorRaw>(rec.actor, kActorRotation);
    {
        wchar_t msg[256]{};
        swprintf_s(msg, L"MOVE 40: actor XYZ after %.2f %.2f %.2f | SetLocation ret=%u",
                   actorPos.x, actorPos.y, actorPos.z, pl.returnValue);
        Log(msg);
    }
    {
        wchar_t msg[256]{};
        swprintf_s(msg, L"MOVE 41: actor rot after %d %d %d | SetRotation ret=%u",
                   actorRot.pitch, actorRot.yaw, actorRot.roll, pr.returnValue);
        Log(msg);
    }

    if (ReadState(rec, rec.state))
        Log(L"MOVE 50: state refreshed");
    else
        Log(L"MOVE 50 FAIL: state refresh failed");

    Log(L"MOVE 60: complete");
}

void ExecuteCommands() {
    std::vector<Command> cmds; { std::lock_guard<std::mutex> lock(g_commandMutex); cmds.swap(g_commands); }
    if(cmds.empty()) return;
    std::lock_guard<std::mutex> lock(g_lightMutex);
    for(const auto& cmd:cmds) {
        if(cmd.type==CommandType::SpawnSpot){Spawn(true);continue;} if(cmd.type==CommandType::SpawnPoint){Spawn(false);continue;}
        LightRecord* rec=FindRecord(cmd.id); if(!rec) continue;
        rec->liveDirty = 0;
        if(cmd.type==CommandType::Apply) ApplyState(*rec,rec->state);
        else if(cmd.type==CommandType::Reset){rec->state=rec->defaults;ApplyState(*rec,rec->state);ReadState(*rec,rec->state);}
        else if(cmd.type==CommandType::MoveToCamera){MoveRecordToCamera(*rec);}
        else if(cmd.type==CommandType::ToggleEnabled){rec->state.enabled=cmd.boolValue;ApplyState(*rec,rec->state);}
    }
}

bool HasPendingCommands() {
    std::lock_guard<std::mutex> lock(g_commandMutex);
    return !g_commands.empty();
}

void QueueConsoleCommand(const wchar_t* cmd) {
    if (!cmd || !*cmd) return;
    std::lock_guard<std::mutex> lock(g_consoleCommandMutex);
    g_consoleCommandQueue.emplace_back(cmd);
}

void ProcessQueuedConsoleCommandsOnGameThread() {
    if (!g_fnConsoleCommand) return;
    std::vector<std::wstring> pending;
    {
        std::lock_guard<std::mutex> lock(g_consoleCommandMutex);
        if (g_consoleCommandQueue.empty()) return;
        pending.swap(g_consoleCommandQueue);
    }
    void* pc = GetPlayerController_BmSDKPath();
    if (!pc) { Log(L"ConsoleCommand queue: no live PlayerController"); return; }
    struct UE3FStringParam { wchar_t* Data{}; int32_t Count{}; int32_t Max{}; };
    struct ConsoleCommandParams { UE3FStringParam Command{}; uint32_t bWriteToLog{}; UE3FStringParam ReturnValue{}; };
    for (auto& command : pending) {
        ConsoleCommandParams params{};
        params.Command.Data = command.data();
        params.Command.Count = static_cast<int32_t>(command.size()) + 1;
        params.Command.Max = params.Command.Count;
        ProcessEvent(pc, g_fnConsoleCommand, &params);
        wchar_t msg[512]{}; swprintf_s(msg, L"ConsoleCommand -> %s", command.c_str()); Log(msg);
    }
}

bool SetLightningLifetimeOnGameThread(void* lifetimeModule, float seconds) {
    using namespace BatLitNative;
    if (!lifetimeModule || !std::isfinite(seconds) || seconds <= 0.0f) return false;

    // ParticleModuleLifetime.Lifetime is a RawDistributionFloat at +0x60 on
    // this pinned build. The baked lookup TArray layout was runtime-validated:
    // +04 Data (ptr), +0C Count, +10 Max. Normal and vertical each contain six
    // floats (originally 0.8/1.0 repeated three times).
    auto* raw = reinterpret_cast<uint8_t*>(lifetimeModule) + kParticleLifetimeRawDistribution;
    if (!IsReadableAddress(raw, 0x1C)) return false;
    float* data = Read<float*>(raw, 0x04);
    const int32_t count = Read<int32_t>(raw, 0x0C);
    if (!data || count <= 0 || count > 64 || !IsReadableAddress(data, static_cast<size_t>(count) * sizeof(float))) return false;

    for (int32_t i = 0; i < count; ++i) data[i] = seconds;
    return true;
}

bool SetLightningInstancesOnGameThread(void* spawnModule, int instances, int* outBurstEntries = nullptr) {
    using namespace BatLitNative;
    if (outBurstEntries) *outBurstEntries = 0;
    if (!spawnModule) return false;
    if (instances < 1) instances = 1;
    if (instances > 32) instances = 32;

    // Instead of trying to create extra ParticleSystemComponents (the pooled PSCs
    // we tested were not renderer-registered), increase the existing lightning
    // emitter's ParticleBurst.Count / CountLow. This creates multiple real
    // lightning particles from the already registered natural PSC in one trigger.
    void* burstListProp = GObjectAtFixedIndex(kGObj_ParticleModuleSpawn_BurstList);
    void* timeProp = GObjectAtFixedIndex(kGObj_ParticleBurst_Time);
    void* countLowProp = GObjectAtFixedIndex(kGObj_ParticleBurst_CountLow);
    void* countProp = GObjectAtFixedIndex(kGObj_ParticleBurst_Count);
    if (!burstListProp || !timeProp || !countLowProp || !countProp) return false;

    const uint16_t burstListOff = Read<uint16_t>(burstListProp, 0x6A);
    const uint16_t timeOff = Read<uint16_t>(timeProp, 0x6A);
    const uint16_t countLowOff = Read<uint16_t>(countLowProp, 0x6A);
    const uint16_t countOff = Read<uint16_t>(countProp, 0x6A);
    if (burstListOff == 0 || countLowOff > 0x40 || countOff > 0x40 || timeOff > 0x40) return false;

    auto* arr = reinterpret_cast<uint8_t*>(spawnModule) + burstListOff;
    if (!IsReadableAddress(arr, 0x10)) return false;
    uint8_t* data = Read<uint8_t*>(arr, 0x00);
    const int32_t count = Read<int32_t>(arr, 0x08);
    const int32_t maxCount = Read<int32_t>(arr, 0x0C);
    if (!data || count <= 0 || count > 64 || maxCount < count) return false;

    // ParticleBurst contains Time(float), CountLow(int), Count(int). Derive its
    // stride from the reflected member offsets rather than pinning a guessed 12.
    size_t stride = 0;
    stride = (std::max)(stride, static_cast<size_t>(timeOff) + sizeof(float));
    stride = (std::max)(stride, static_cast<size_t>(countLowOff) + sizeof(int32_t));
    stride = (std::max)(stride, static_cast<size_t>(countOff) + sizeof(int32_t));
    stride = (stride + 3u) & ~size_t(3u);
    if (stride < 8 || stride > 0x40 || !IsReadableAddress(data, stride * static_cast<size_t>(count))) return false;

    for (int32_t i = 0; i < count; ++i) {
        uint8_t* burst = data + stride * static_cast<size_t>(i);
        *reinterpret_cast<int32_t*>(burst + countLowOff) = instances;
        *reinterpret_cast<int32_t*>(burst + countOff) = instances;
    }
    if (outBurstEntries) *outBurstEntries = count;
    return true;
}

bool ResolveNaturalLightningPSCs() {
    using namespace BatLitNative;

    // IMPORTANT: this is intentionally FAST-ONLY. Never scan GObjects here:
    // this function runs from HookEngineTick on the game thread when the user
    // presses Trigger. Scanning ~650k objects here stalls Arkham Knight.
    if (!g_lightningCacheReady.load(std::memory_order_acquire)) return false;

    auto pscValid = [](void* psc) -> bool {
        if (!psc || !IsReadableAddress(psc, 0x2A0)) return false;
        if (!IsReadableAddress(reinterpret_cast<uint8_t*>(psc) + 0x9C, sizeof(void*))) return false;
        return Read<void*>(psc, 0x9C) != nullptr;
    };

    if (!g_lightningActorCache || !IsReadableAddress(g_lightningActorCache, 0x2C0) ||
        !pscValid(g_normalLightningPSCCache) || !pscValid(g_verticalLightningPSCCache)) {
        g_lightningCacheReady.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void RefreshNaturalLightningPSCsWorker() {
    using namespace BatLitNative;
    if (g_lightningCacheReady.load(std::memory_order_acquire)) return;
    if (!g_classRLightningActor) return;

    // Slow GObjects scan is allowed ONLY from the background worker thread.
    void* actor = FindLiveObjectOfClass(g_classRLightningActor);
    if (!actor || !IsReadableAddress(actor, 0x2C0)) return;

    void* normal = Read<void*>(actor, kRLightningNormalPSC);
    void* vertical = Read<void*>(actor, kRLightningVerticalPSC);

    auto pscValid = [](void* psc) -> bool {
        if (!psc || !IsReadableAddress(psc, 0x2A0)) return false;
        if (!IsReadableAddress(reinterpret_cast<uint8_t*>(psc) + 0x9C, sizeof(void*))) return false;
        return Read<void*>(psc, 0x9C) != nullptr;
    };
    if (!pscValid(normal) || !pscValid(vertical)) return;

    g_lightningActorCache = actor;
    g_normalLightningPSCCache = normal;
    g_verticalLightningPSCCache = vertical;
    g_lightningCacheReady.store(true, std::memory_order_release);
    Log(L"LIGHTNING: natural PSC cache resolved on worker thread");
}

bool TriggerLightningOnGameThread(bool vertical, float seconds, float distance) {
    using namespace BatLitNative;
    if (!g_classRLightningActor || !g_fnParticleActivateSystem || !g_fnParticleDeactivateSystem || !g_fnPrimitiveSetTranslation) {
        Log(L"LIGHTNING: resolver not ready");
        return false;
    }

    if (!ResolveNaturalLightningPSCs()) {
        Log(L"LIGHTNING: cache not ready; background resolver will retry (no game-thread scan)");
        return false;
    }

    void* psc = vertical ? g_verticalLightningPSCCache : g_normalLightningPSCCache;

    void* lifetimeModule = vertical ? g_verticalLightningLifetimeModule : g_normalLightningLifetimeModule;
    if (!SetLightningLifetimeOnGameThread(lifetimeModule, seconds)) {
        Log(L"LIGHTNING: lifetime lookup write failed");
        return false;
    }

    Vec3 cam{};
    RotatorRaw rot{};
    float fov = 0.0f;
    if (!GetRenderedCameraFromCache(cam, rot, fov) && !GetPlayerView(cam, rot)) {
        Log(L"LIGHTNING: camera unavailable");
        return false;
    }

    constexpr double kRuuToRad = 6.28318530717958647692 / 65536.0;
    const double pitch = static_cast<double>(rot.pitch) * kRuuToRad;
    const double yaw = static_cast<double>(rot.yaw) * kRuuToRad;
    const double cp = std::cos(pitch);
    Vec3 target{};
    target.x = cam.x + static_cast<float>(cp * std::cos(yaw) * distance);
    target.y = cam.y + static_cast<float>(cp * std::sin(yaw) * distance);
    target.z = cam.z + static_cast<float>(std::sin(pitch) * distance);

    // Retrigger the already renderer-registered natural PSC at the requested
    // camera-front position.
    ProcessEvent(psc, g_fnParticleDeactivateSystem, nullptr);
    struct PTranslation { Vec3 NewTranslation; } pt{ target };
    if (!ProcessEvent(psc, g_fnPrimitiveSetTranslation, &pt)) {
        Log(L"LIGHTNING: SetTranslation failed");
        return false;
    }
    struct PActivate { int32_t bFlagAsJustAttached{}; int32_t DontUpdateTransform{}; } pa{};
    if (!ProcessEvent(psc, g_fnParticleActivateSystem, &pa)) {
        Log(L"LIGHTNING: ActivateSystem failed");
        return false;
    }

    wchar_t msg[256]{};
    swprintf_s(msg, L"LIGHTNING: %s XYZ %.1f %.1f %.1f | %.2fs | distance %.1f",
        vertical ? L"Vertical" : L"Small", target.x, target.y, target.z, seconds, distance);
    Log(msg);
    return true;
}


struct LightningCERequest {
    bool vertical{};
    float seconds{1.0f};
    float distance{5000.0f};
    int instances{1};
    int salvoCount{1};
    float salvoInterval{0.08f};
    int clusterCount{1};
    float clusterSpread{0.08f};
    bool randomizePosition{};
    float spreadX{0.80f};
    float spreadY{0.65f};
    float distanceJitter{0.15f};
};

static void* SafeReadPtrRaw(void* base, size_t offset) {
    if (!base) return nullptr;
    __try {
        return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(base) + offset);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SafeReadU16Raw(void* base, size_t offset, uint16_t& out) {
    if (!base) return false;
    __try {
        out = *reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(base) + offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadU32Raw(void* base, size_t offset, uint32_t& out) {
    if (!base) return false;
    __try {
        out = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(base) + offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadIntRaw(void* base, size_t offset, int& out) {
    if (!base) return false;
    __try {
        out = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(base) + offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

DWORD WINAPI LightningCEPrepareThread(void* param) {
    auto* reqPtr = reinterpret_cast<LightningCERequest*>(param);
    if (!reqPtr) return 0;
    const LightningCERequest req = *reqPtr;
    delete reqPtr;

    using namespace BatLitNative;
    auto finish = []() { g_lightningCEBusy.store(false, std::memory_order_release); };

    if (!g_gameBase) {
        SetLightningStatus(L"CE fast prep: game base unavailable");
        finish();
        return 0;
    }

    // Exact GObject entries established by the working CE tests.
    void* lightningClass = GObjectAtFixedIndex(kGObj_RLightningActorClass);
    void* lifetimeModule = GObjectAtFixedIndex(req.vertical ? kGObj_VerticalLifetimeModule : kGObj_SmallLifetimeModule);
    void* spawnModule = GObjectAtFixedIndex(req.vertical ? kGObj_VerticalSpawnModule : kGObj_SmallSpawnModule);
    if (!lightningClass || !lifetimeModule || !spawnModule) {
        SetLightningStatus(L"CE fast prep: class / lifetime / spawn GObject unavailable");
        finish();
        return 0;
    }

    const DWORD t0 = GetTickCount();

    // Reuse the cached actor when possible. Otherwise do the same raw exact-class
    // walk that worked in CE. No IsReadableAddress/VirtualQuery and no IsA chain.
    void* lightningActor = g_lightningActorCache;
    if (!IsExactClassRaw(lightningActor, lightningClass)) {
        lightningActor = FindLiveExactClassRaw(lightningClass);
    }
    if (!lightningActor) {
        SetLightningStatus(L"CE fast prep: live RLightningActor not found");
        finish();
        return 0;
    }

    void* normal = SafeReadPtrRaw(lightningActor, kRLightningNormalPSC);
    void* vertical = SafeReadPtrRaw(lightningActor, kRLightningVerticalPSC);

    void* psc = req.vertical ? vertical : normal;
    if (!psc) {
        SetLightningStatus(L"CE fast prep: natural PSC is null");
        finish();
        return 0;
    }

    // Do not reject a natural PSC just because SceneInfo is temporarily null.
    // In the working CE path we triggered the natural component directly; on some
    // loads the renderer registration can appear a little later (Small showed this
    // too on first use). Deactivate/SetTranslation/Activate is allowed to run and
    // the status after the game-thread call tells us whether the trigger completed.
    void* sceneInfo = SafeReadPtrRaw(psc, 0x9C);

    g_lightningActorCache = lightningActor;
    g_normalLightningPSCCache = normal;
    g_verticalLightningPSCCache = vertical;
    g_lightningCacheReady.store(normal && vertical, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(g_lightningPreparedMutex);
        g_lightningPrepared.psc = psc;
        g_lightningPrepared.lifetimeModule = lifetimeModule;
        g_lightningPrepared.seconds = req.seconds;
        g_lightningPrepared.distance = req.distance;
        g_lightningPrepared.instances = req.instances;
        g_lightningPrepared.salvoCount = req.salvoCount;
        g_lightningPrepared.salvoInterval = req.salvoInterval;
        g_lightningPrepared.clusterCount = req.clusterCount;
        g_lightningPrepared.clusterSpread = req.clusterSpread;
        g_lightningPrepared.vertical = req.vertical;
        g_lightningPrepared.randomizePosition = req.randomizePosition;
        g_lightningPrepared.spreadX = req.spreadX;
        g_lightningPrepared.spreadY = req.spreadY;
        g_lightningPrepared.distanceJitter = req.distanceJitter;
        g_lightningPreparedReady.store(true, std::memory_order_release);
    }

    wchar_t status[192]{};
    swprintf_s(status, L"Ready: %s PSC | SceneInfo %s | raw scan %lu ms -> waiting for game tick",
        req.vertical ? L"Vertical" : L"Small", sceneInfo ? L"OK" : L"pending",
        static_cast<unsigned long>(GetTickCount() - t0));
    SetLightningStatus(status);
    finish();
    return 0;
}

bool TriggerLightningCEAsync(bool vertical, float seconds, float distance, int instances, int salvoCount, float salvoInterval, int clusterCount, float clusterSpread, bool randomizePosition, float spreadX, float spreadY, float distanceJitter) {
    if (!std::isfinite(seconds) || seconds < 0.05f) seconds = 0.05f;
    if (!std::isfinite(distance) || distance < 100.0f) distance = 100.0f;
    if (instances < 1) instances = 1;
    if (instances > 32) instances = 32;
    if (salvoCount < 1) salvoCount = 1;
    if (salvoCount > 32) salvoCount = 32;
    if (!std::isfinite(salvoInterval)) salvoInterval = 0.08f;
    salvoInterval = (std::max)(0.0f, (std::min)(salvoInterval, 5.0f));
    if (clusterCount < 1) clusterCount = 1;
    if (clusterCount > salvoCount) clusterCount = salvoCount;
    if (clusterCount > 32) clusterCount = 32;
    if (!std::isfinite(clusterSpread)) clusterSpread = 0.08f;
    clusterSpread = (std::max)(0.0f, (std::min)(clusterSpread, 0.50f));
    if (!std::isfinite(spreadX)) spreadX = 0.80f;
    if (!std::isfinite(spreadY)) spreadY = 0.65f;
    if (!std::isfinite(distanceJitter)) distanceJitter = 0.15f;
    spreadX = (std::max)(0.0f, (std::min)(spreadX, 1.0f));
    spreadY = (std::max)(0.0f, (std::min)(spreadY, 1.0f));
    distanceJitter = (std::max)(0.0f, (std::min)(distanceJitter, 0.75f));

    bool expected = false;
    if (!g_lightningCEBusy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        SetLightningStatus(L"CE prep already running");
        return false;
    }

    auto* req = new (std::nothrow) LightningCERequest{};
    if (!req) {
        g_lightningCEBusy.store(false, std::memory_order_release);
        SetLightningStatus(L"CE prep allocation failed");
        return false;
    }
    req->vertical = vertical;
    req->seconds = seconds;
    req->distance = distance;
    req->instances = instances;
    req->salvoCount = salvoCount;
    req->salvoInterval = salvoInterval;
    req->clusterCount = clusterCount;
    req->clusterSpread = clusterSpread;
    req->randomizePosition = randomizePosition;
    req->spreadX = spreadX;
    req->spreadY = spreadY;
    req->distanceJitter = distanceJitter;

    SetLightningStatus(vertical ? L"Preparing Vertical Lightning salvo..." : L"Preparing Small Lightning salvo...");
    HANDLE h = CreateThread(nullptr, 0, LightningCEPrepareThread, req, 0, nullptr);
    if (!h) {
        delete req;
        g_lightningCEBusy.store(false, std::memory_order_release);
        SetLightningStatus(L"CE prep thread creation failed");
        return false;
    }
    CloseHandle(h);
    return true;
}

void QueueLightningTrigger(bool vertical, float seconds, float distance, int instances, int salvoCount, float salvoInterval, int clusterCount, float clusterSpread, bool randomizePosition, float spreadX, float spreadY, float distanceJitter) {
    TriggerLightningCEAsync(vertical, seconds, distance, instances, salvoCount, salvoInterval, clusterCount, clusterSpread, randomizePosition, spreadX, spreadY, distanceJitter);
}

void ProcessLightningRequestOnGameThread() {
    using namespace BatLitNative;

    // A salvo is deliberately executed over successive EngineTick calls. This is
    // the automatic equivalent of repeatedly pressing Execute in the CE tests,
    // without ever sleeping/blocking the game thread.
    static bool salvoActive = false;
    static LightningPreparedRequest salvoReq{};
    static int salvoRemaining = 0;
    static int salvoShotIndex = 0;
    static int salvoClusterCount = 1;
    static bool clusterCentersReady = false;
    static std::array<float, 32> clusterCenterX{};
    static std::array<float, 32> clusterCenterY{};
    static ULONGLONG nextShotMs = 0;

    if (g_lightningPreparedReady.exchange(false, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lock(g_lightningPreparedMutex);
        salvoReq = g_lightningPrepared;
        salvoReq.salvoCount = (std::max)(1, (std::min)(salvoReq.salvoCount, 32));
        salvoReq.clusterCount = (std::max)(1, (std::min)(salvoReq.clusterCount, salvoReq.salvoCount));
        salvoReq.clusterSpread = (std::max)(0.0f, (std::min)(salvoReq.clusterSpread, 0.50f));
        salvoReq.salvoInterval = (std::max)(0.0f, (std::min)(salvoReq.salvoInterval, 5.0f));
        salvoRemaining = salvoReq.salvoCount;
        salvoShotIndex = 0;
        salvoClusterCount = salvoReq.clusterCount;
        clusterCentersReady = false;
        nextShotMs = 0;
        salvoActive = true;
    }

    if (!salvoActive || salvoRemaining <= 0) return;

    const ULONGLONG nowMs = GetTickCount64();
    if (nextShotMs != 0 && nowMs < nextShotMs) return;

    const LightningPreparedRequest req = salvoReq;
    if (!req.psc || !req.lifetimeModule || !g_fnParticleDeactivateSystem || !g_fnPrimitiveSetTranslation || !g_fnParticleActivateSystem) {
        SetLightningStatus(L"Game tick: resolver / prepared request invalid");
        salvoActive = false;
        return;
    }

    void* pc = g_inputBlockPC;
    if (!pc || !IsReadableAddress(reinterpret_cast<uint8_t*>(pc) + kPlayerControllerPlayerCamera, sizeof(void*))) {
        pc = GetPlayerController_BmSDKPath();
    }
    if (!pc) {
        SetLightningStatus(L"Game tick: PlayerController unavailable");
        salvoActive = false;
        return;
    }

    void* camera = Read<void*>(pc, kPlayerControllerPlayerCamera);
    const ptrdiff_t povOff = kCameraCache + kCameraCachePOV;
    if (!camera || !IsReadableAddress(reinterpret_cast<uint8_t*>(camera) + povOff, 24)) {
        SetLightningStatus(L"Game tick: PlayerCamera / POV invalid");
        salvoActive = false;
        return;
    }

    const Vec3 cam = Read<Vec3>(camera, povOff + 0x00);
    const RotatorRaw rot = Read<RotatorRaw>(camera, povOff + 0x0C);
    if (!std::isfinite(cam.x) || !std::isfinite(cam.y) || !std::isfinite(cam.z)) {
        SetLightningStatus(L"Game tick: camera coordinates invalid");
        salvoActive = false;
        return;
    }

    constexpr double kRuuToRad = 6.28318530717958647692 / 65536.0;
    const double pitch = static_cast<double>(rot.pitch) * kRuuToRad;
    const double yaw = static_cast<double>(rot.yaw) * kRuuToRad;
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);

    Vec3 forward{ static_cast<float>(cp * cy), static_cast<float>(cp * sy), static_cast<float>(sp) };
    Vec3 right{ static_cast<float>(-sy), static_cast<float>(cy), 0.0f };
    Vec3 up{ static_cast<float>(-sp * cy), static_cast<float>(-sp * sy), static_cast<float>(cp) };

    static uint32_t rngState = 0;
    if (rngState == 0) {
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        rngState = static_cast<uint32_t>(qpc.LowPart) ^ GetTickCount() ^ 0xA3C59AC3u;
        if (rngState == 0) rngState = 0x6D2B79F5u;
    }
    auto nextUnit = [&]() -> float {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(rngState & 0x00FFFFFFu) / 16777215.0f;
    };
    auto nextSigned = [&]() -> float { return nextUnit() * 2.0f - 1.0f; };

    float targetDistance = req.distance;
    float screenX = 0.0f;
    float screenY = 0.0f;
    float fovDeg = 90.0f;
    float aspect = 16.0f / 9.0f;
    int clusterIndex = 0;

    if (req.randomizePosition) {
        targetDistance *= 1.0f + nextSigned() * req.distanceJitter;
        if (targetDistance < 100.0f) targetDistance = 100.0f;

        if (IsReadableAddress(reinterpret_cast<uint8_t*>(camera) + povOff, 0x20)) {
            const float rawFov = Read<float>(camera, povOff + 0x18);
            const float rawAspect = Read<float>(camera, povOff + 0x1C);
            if (std::isfinite(rawFov) && rawFov >= 20.0f && rawFov <= 170.0f) fovDeg = rawFov;
            if (std::isfinite(rawAspect) && rawAspect >= 0.5f && rawAspect <= 4.0f) aspect = rawAspect;
        }

        // Generate persistent cluster centers once per salvo. ClusterSpread = 0
        // means repeated strikes can hit exactly the same screen-space location.
        if (!clusterCentersReady) {
            for (int i = 0; i < salvoClusterCount; ++i) {
                clusterCenterX[i] = nextSigned() * req.spreadX;
                clusterCenterY[i] = nextSigned() * req.spreadY;
            }
            clusterCentersReady = true;
        }

        // Make sure every requested cluster gets at least one strike first, then
        // choose randomly. This gives both radical separation and natural repeats.
        if (salvoShotIndex < salvoClusterCount) {
            clusterIndex = salvoShotIndex;
        } else {
            clusterIndex = static_cast<int>(nextUnit() * static_cast<float>(salvoClusterCount));
            if (clusterIndex >= salvoClusterCount) clusterIndex = salvoClusterCount - 1;
        }

        screenX = clusterCenterX[clusterIndex] + nextSigned() * req.clusterSpread;
        screenY = clusterCenterY[clusterIndex] + nextSigned() * req.clusterSpread;
        screenX = (std::max)(-req.spreadX, (std::min)(screenX, req.spreadX));
        screenY = (std::max)(-req.spreadY, (std::min)(screenY, req.spreadY));
    }

    Vec3 target{};
    target.x = cam.x + forward.x * targetDistance;
    target.y = cam.y + forward.y * targetDistance;
    target.z = cam.z + forward.z * targetDistance;

    if (req.randomizePosition) {
        const float fovRad = fovDeg * (3.14159265358979323846f / 180.0f);
        const float halfWidth = std::tan(fovRad * 0.5f) * targetDistance;
        const float halfHeight = halfWidth / aspect;
        const float offRight = screenX * halfWidth;
        const float offUp = screenY * halfHeight;
        target.x += right.x * offRight + up.x * offUp;
        target.y += right.y * offRight + up.y * offUp;
        target.z += right.z * offRight + up.z * offUp;
    }

    if (!SetLightningLifetimeOnGameThread(req.lifetimeModule, req.seconds)) {
        SetLightningStatus(L"Game tick: lifetime lookup write failed");
        salvoActive = false;
        return;
    }

    void* spawnModule = req.vertical ? g_verticalLightningSpawnModule : g_normalLightningSpawnModule;
    int burstEntries = 0;
    if (!SetLightningInstancesOnGameThread(spawnModule, req.instances, &burstEntries)) {
        SetLightningStatus(L"Game tick: lightning BurstList / instances write failed");
        salvoActive = false;
        return;
    }

    ProcessEvent(req.psc, g_fnParticleDeactivateSystem, nullptr);
    alignas(8) uint8_t translationParams[0x20]{};
    memcpy(translationParams + 0x00, &target, sizeof(target));
    if (!ProcessEvent(req.psc, g_fnPrimitiveSetTranslation, translationParams)) {
        SetLightningStatus(L"Game tick: SetTranslation failed");
        salvoActive = false;
        return;
    }
    alignas(8) uint8_t activateParams[0x10]{};
    if (!ProcessEvent(req.psc, g_fnParticleActivateSystem, activateParams)) {
        SetLightningStatus(L"Game tick: ActivateSystem failed");
        salvoActive = false;
        return;
    }

    const int shotNumber = salvoShotIndex + 1;
    --salvoRemaining;
    ++salvoShotIndex;
    if (salvoRemaining > 0) {
        const ULONGLONG intervalMs = static_cast<ULONGLONG>(req.salvoInterval * 1000.0f + 0.5f);
        nextShotMs = nowMs + intervalMs;
    } else {
        salvoActive = false;
        nextShotMs = 0;
    }

    wchar_t status[384]{};
    if (req.randomizePosition) {
        swprintf_s(status, L"OK: %s | salvo %d/%d | cluster %d/%d | %.2fs | distance %.0f | instances %d | random X %.2f Y %.2f",
            req.vertical ? L"Vertical" : L"Small", shotNumber, req.salvoCount,
            clusterIndex + 1, salvoClusterCount, req.seconds, targetDistance, req.instances, screenX, screenY);
    } else {
        swprintf_s(status, L"OK: %s | salvo %d/%d | %.2fs | distance %.0f | instances %d | centered",
            req.vertical ? L"Vertical" : L"Small", shotNumber, req.salvoCount,
            req.seconds, targetDistance, req.instances);
    }
    SetLightningStatus(status);
    Log(status);
}
void ProcessPoseInspectorOnGameThread();
void ProcessPoseWriterHookControlOnGameThread();
void ApplyPoseWriterAfterEngineTick();

void ApplyPoseFocusOptionOnGameThread() {
    if (g_enginePauseOnLossOfFocusOffset < 0) return;

    const bool requested = g_poseKeepRunningUnfocused.load();
    if (requested == g_poseFocusOriginalValid) return;
    void* engine = requested ? GetEngine_BmSDKPath() : g_poseFocusEngine;
    if (!engine) return;
    auto* flags = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(engine) + g_enginePauseOnLossOfFocusOffset);
    if (!IsReadableAddress(flags, sizeof(*flags))) return;

    uint32_t value = *flags;
    if (requested && !g_poseFocusOriginalValid) {
        g_poseFocusEngine = engine;
        g_poseFocusOriginalValue = (value & kEnginePauseOnLossOfFocusMask) != 0;
        g_poseFocusOriginalValid = true;
        value &= ~kEnginePauseOnLossOfFocusMask;
        *flags = value;
        Log(L"POSE FOCUS: disabled pause-on-focus-loss; original value captured");
    } else if (!requested && g_poseFocusOriginalValid) {
        value &= ~kEnginePauseOnLossOfFocusMask;
        if (g_poseFocusOriginalValue) value |= kEnginePauseOnLossOfFocusMask;
        *flags = value;
        g_poseFocusOriginalValid = false;
        g_poseFocusEngine = nullptr;
        Log(L"POSE FOCUS: original pause-on-focus-loss value restored");
    }
}

void UpdateRadioModeEyesOnGameThread() {
    const int req = g_radioEyesRequest.exchange(-1);
    if (req < 0) return;
    QueueConsoleCommand(req ? L"set BmScript.RPawnPlayerBm bRadioModeEyes 1" : L"set BmScript.RPawnPlayerBm bRadioModeEyes 0");
}

intptr_t __fastcall HookEngineTick(void* self) {
    const DWORD tid = GetCurrentThreadId();
    if (g_gameThreadId.load() == 0) g_gameThreadId.store(tid);
    if (!g_inCommandDispatch) {
        g_inCommandDispatch = true;
        CheckForLevelRestartAndCleanupLights();
        ExecuteCommands();
        UpdatePendingLightRecords();
        ApplyPendingLightEdits();
        UpdateRadioModeEyesOnGameThread();
        ProcessQueuedConsoleCommandsOnGameThread();
        ProcessLightningRequestOnGameThread();
        UpdateExposureOverrideOnGameThread();
        UpdateGameInputBlockOnGameThread();
        ApplyPoseFocusOptionOnGameThread();
        ProcessPoseInspectorOnGameThread();
        ProcessPoseWriterHookControlOnGameThread();
        g_inCommandDispatch = false;
    }
    const intptr_t result = g_originalEngineTick ? g_originalEngineTick(self) : 0;
    ApplyPoseWriterAfterEngineTick();
    SamplePoseMeshDiagnosticsAfterTick();
    return result;
}

#if 0 // Superseded: Catwoman's active animation path does not call this internal Steam routine.
void __fastcall HookPoseFinalize(void* mesh) {
    if (g_originalPoseFinalize) g_originalPoseFinalize(mesh);
    if (!g_poseFreezeDesired.load(std::memory_order_relaxed) &&
        !g_poseEditDesired.load(std::memory_order_relaxed)) return;
    g_poseFinalizeCallCount.fetch_add(1, std::memory_order_relaxed);
    if (mesh != g_poseHookMesh.load(std::memory_order_relaxed)) return;
    g_poseFinalizeMatchCount.fetch_add(1, std::memory_order_relaxed);

    const int offset = g_poseSpaceBasesOffset;
    if (offset < 0 || !IsReadableAddress(mesh, static_cast<size_t>(offset) + sizeof(void*) + 8)) {
        g_poseFinalizeFailure.store(1, std::memory_order_relaxed);
        return;
    }

    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
    memcpy(&header, reinterpret_cast<uint8_t*>(mesh) + offset, sizeof(header));
    if (!header.data || header.count <= 0 || header.count > 4096 ||
        header.capacity < header.count || header.capacity > 4096) {
        g_poseFinalizeFailure.store(2, std::memory_order_relaxed);
        return;
    }
    const size_t poseBytes = static_cast<size_t>(header.count) * 0x20;
    if (!IsReadableAddress(header.data, poseBytes) || !IsWritableAddress(header.data, poseBytes)) {
        g_poseFinalizeFailure.store(3, std::memory_order_relaxed);
        return;
    }

    const bool freeze = g_poseFreezeDesired.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        if (freeze) {
            if (!g_poseFrozenSnapshotReady || g_poseFrozenMesh != mesh ||
                g_poseFrozenCount != header.count || g_poseFrozenSnapshot.size() != poseBytes) {
                g_poseFrozenSnapshot.resize(poseBytes);
                memcpy(g_poseFrozenSnapshot.data(), header.data, poseBytes);
                g_poseFrozenMesh = mesh;
                g_poseFrozenCount = header.count;
                g_poseFrozenSnapshotReady = true;
                g_poseFrozenBoneCount.store(header.count, std::memory_order_relaxed);
                Log(L"POSE FREEZE: captured component-space pose snapshot");
            }
            memcpy(header.data, g_poseFrozenSnapshot.data(), poseBytes);
            g_poseFreezeWriteCount.fetch_add(1, std::memory_order_relaxed);
        } else if (g_poseFrozenSnapshotReady) {
            g_poseFrozenSnapshot.clear();
            g_poseFrozenMesh = nullptr;
            g_poseFrozenCount = 0;
            g_poseFrozenSnapshotReady = false;
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
    }

    if (!g_poseEditDesired.load(std::memory_order_relaxed)) return;
    const int bone = g_poseSelectedBone.load(std::memory_order_relaxed);
    if (bone < 0 || bone >= header.count) {
        g_poseFinalizeFailure.store(4, std::memory_order_relaxed);
        return;
    }
    if (ApplyPoseOffsetsToSubtree(static_cast<uint8_t*>(header.data), header.count))
        g_poseEditAppliedCount.fetch_add(1, std::memory_order_relaxed);
}

void ProcessPoseFinalizeControlOnGameThread() {
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
    }
    g_poseHookMesh.store(selectedMesh, std::memory_order_release);

    const bool requested = g_poseFreezeDesired.load(std::memory_order_relaxed) ||
        g_poseEditDesired.load(std::memory_order_relaxed);
    if (!requested) {
        g_poseEditActive.store(false, std::memory_order_release);
        const bool wasRequested = g_poseWriterWasRequested.exchange(false, std::memory_order_acq_rel);
        if (wasRequested) {
            std::lock_guard<std::mutex> lock(g_poseWriterMutex);
            g_poseFrozenSnapshot.clear();
            g_poseFrozenMesh = nullptr;
            g_poseFrozenCount = 0;
            g_poseFrozenSnapshotReady = false;
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
        if (g_poseFinalizeHookInstalled.load(std::memory_order_acquire) &&
            g_poseFinalizeHookEnabled.exchange(false, std::memory_order_acq_rel)) {
            auto* target = reinterpret_cast<void*>(g_gameBase + kPoseFinalizeSteam);
            const MH_STATUS disable = MH_DisableHook(target);
            if (disable != MH_OK && disable != MH_ERROR_DISABLED) {
                g_poseFinalizeHookEnabled.store(true, std::memory_order_release);
                g_poseFinalizeHookState.store(-3);
                Log(L"POSE EDIT: unable to disable pose-finalizer hook; writes remain off");
            } else {
                Log(L"POSE EDIT: pose-finalizer hook disabled; animation path restored");
            }
        }
        return;
    }
    g_poseWriterWasRequested.store(true, std::memory_order_release);
    if (!selectedMesh) return;
    if (g_poseFinalizeHookInstalled.load(std::memory_order_acquire)) {
        if (!g_poseFinalizeHookEnabled.load(std::memory_order_acquire)) {
            auto* target = reinterpret_cast<void*>(g_gameBase + kPoseFinalizeSteam);
            const MH_STATUS enable = MH_EnableHook(target);
            if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
                g_poseFinalizeHookState.store(-3);
                Log(L"POSE EDIT: unable to re-enable pose-finalizer hook");
                return;
            }
            g_poseFinalizeHookEnabled.store(true, std::memory_order_release);
        }
        g_poseEditActive.store(g_poseEditDesired.load(), std::memory_order_release);
        return;
    }
    if (!g_buildProfile || strcmp(g_buildProfile->name, "Steam") != 0) {
        g_poseFinalizeHookState.store(-2);
        return;
    }

    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + kPoseFinalizeSteam);
    static constexpr uint8_t signature[] = {
        0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00,0x8B,0x81,0x74,0x04,0x00
    };
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeHookState.store(-1);
        Log(L"POSE EDIT: Steam pose-finalizer signature mismatch; write path remains disabled");
        return;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeHookState.store(-3);
        return;
    }
    const MH_STATUS create = MH_CreateHook(target, &HookPoseFinalize,
        reinterpret_cast<void**>(&g_originalPoseFinalize));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeHookState.store(-3);
        Log(L"POSE EDIT: unable to create pose-finalizer hook");
        return;
    }
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeHookState.store(-3);
        Log(L"POSE EDIT: unable to enable pose-finalizer hook");
        return;
    }
    g_poseFinalizeHookInstalled.store(true, std::memory_order_release);
    g_poseFinalizeHookEnabled.store(true, std::memory_order_release);
    g_poseFinalizeHookState.store(1);
    g_poseEditActive.store(g_poseEditDesired.load(), std::memory_order_release);
    Log(L"POSE EDIT: guarded Steam component-space translation hook enabled; offset starts at zero");
}
#endif

bool ApplyPoseOffsetsToSubtree(uint8_t* poseData, int poseBoneCount);
bool ApplyPoseOffsetsToSubtreeSelection(uint8_t* poseData, int poseBoneCount,
    const int* selectedIndices, int selectedCount, int pivotBone);

void ApplyPoseWriterFromFinalizer(void* mesh, int variant) {
    if (variant >= 0 && variant < 4)
        g_poseFinalizerVariantCalls[variant].fetch_add(1, std::memory_order_relaxed);
    if (!g_poseFreezeDesired.load(std::memory_order_relaxed) &&
        !g_poseEditDesired.load(std::memory_order_relaxed)) return;
    g_poseFinalizeCallCount.fetch_add(1, std::memory_order_relaxed);

    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
    }
    const int offset = g_poseSpaceBasesOffset;
    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{}, selectedHeader{};
    auto readSpaceBasesHeader = [&](void* component, TArrayHeader& out) -> bool {
        if (!component || offset < 0 ||
            !IsReadableAddress(component, static_cast<size_t>(offset) + sizeof(void*) + 8)) return false;
        memcpy(&out, reinterpret_cast<uint8_t*>(component) + offset, sizeof(out));
        return out.data && out.count > 0 && out.count <= 4096 &&
            out.capacity >= out.count && out.capacity <= 4096;
    };
    const bool handlerHeaderValid = readSpaceBasesHeader(mesh, header);
    const bool selectedHeaderValid = mesh == selectedMesh || readSpaceBasesHeader(selectedMesh, selectedHeader);
    const bool sameObject = mesh && mesh == selectedMesh;
    const bool sameBuffer = !sameObject && handlerHeaderValid && selectedHeaderValid &&
        header.data == selectedHeader.data && header.count == selectedHeader.count;
    if (!sameObject && !sameBuffer) {
        if (variant >= 0 && variant < 4) {
            const uint64_t diagnosticIndex = g_poseFinalizerMismatchDiagnostics[variant].fetch_add(1, std::memory_order_relaxed);
            if (diagnosticIndex < 2) {
                wchar_t message[256]{};
                if (variant == 3) {
                    swprintf_s(message,
                        L"POSE MATCH DIAG dispatcher handler=0x%llX target=0x%llX handlerSB=0x%llX/%d targetSB=0x%llX/%d",
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mesh)),
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(selectedMesh)),
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(handlerHeaderValid ? header.data : nullptr)),
                        handlerHeaderValid ? header.count : 0,
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(selectedHeaderValid ? selectedHeader.data : nullptr)),
                        selectedHeaderValid ? selectedHeader.count : 0);
                } else {
                    swprintf_s(message,
                        L"POSE MATCH DIAG mode %d handler=0x%llX target=0x%llX handlerSB=0x%llX/%d targetSB=0x%llX/%d",
                        variant == 0 ? 1 : variant == 1 ? 2 : 5,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mesh)),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(selectedMesh)),
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(handlerHeaderValid ? header.data : nullptr)),
                    handlerHeaderValid ? header.count : 0,
                    static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(selectedHeaderValid ? selectedHeader.data : nullptr)),
                    selectedHeaderValid ? selectedHeader.count : 0);
                }
                Log(message);
            }
        }
        return;
    }
    g_poseFinalizeMatchCount.fetch_add(1, std::memory_order_relaxed);

    if (!g_classSkeletalMeshComponent || !IsA(mesh, g_classSkeletalMeshComponent) || !handlerHeaderValid) {
        g_poseFinalizeFailure.store(1, std::memory_order_relaxed);
        return;
    }
    const size_t poseBytes = static_cast<size_t>(header.count) * 0x20;
    if (!IsReadableAddress(header.data, poseBytes) || !IsWritableAddress(header.data, poseBytes)) {
        g_poseFinalizeFailure.store(3, std::memory_order_relaxed);
        return;
    }
    g_poseArrayReadyCount.fetch_add(1, std::memory_order_relaxed);
    g_poseFinalizeFailure.store(0, std::memory_order_relaxed);

    const bool freeze = g_poseFreezeDesired.load(std::memory_order_relaxed);
    void* snapshotIdentity = selectedMesh ? selectedMesh : mesh;
    {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        if (freeze) {
            if (!g_poseFrozenSnapshotReady || g_poseFrozenMesh != snapshotIdentity ||
                g_poseFrozenCount != header.count || g_poseFrozenSnapshot.size() != poseBytes) {
                g_poseFrozenSnapshot.resize(poseBytes);
                memcpy(g_poseFrozenSnapshot.data(), header.data, poseBytes);
                g_poseFrozenMesh = snapshotIdentity;
                g_poseFrozenCount = header.count;
                g_poseFrozenSnapshotReady = true;
                g_poseFrozenBoneCount.store(header.count, std::memory_order_relaxed);
                wchar_t captureMessage[160]{};
                if (variant == 3)
                    swprintf_s(captureMessage, L"POSE FREEZE: captured component-space pose after animation dispatcher (mesh 0x%llX, bones %d)",
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(snapshotIdentity)), header.count);
                else
                    swprintf_s(captureMessage, L"POSE FREEZE: captured component-space pose at handler mode %d (mesh 0x%llX, bones %d)",
                        variant == 0 ? 1 : variant == 1 ? 2 : 5,
                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(snapshotIdentity)), header.count);
                Log(captureMessage);
            }
            memcpy(header.data, g_poseFrozenSnapshot.data(), poseBytes);
            g_poseFreezeWriteCount.fetch_add(1, std::memory_order_relaxed);
        } else if (g_poseFrozenSnapshotReady) {
            g_poseFrozenSnapshot.clear();
            g_poseFrozenMesh = nullptr;
            g_poseFrozenCount = 0;
            g_poseFrozenSnapshotReady = false;
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
    }

    if (!g_poseEditDesired.load(std::memory_order_relaxed)) return;
    const int bone = g_poseSelectedBone.load(std::memory_order_relaxed);
    if (bone < 0 || bone >= header.count) {
        g_poseFinalizeFailure.store(4, std::memory_order_relaxed);
        return;
    }
    if (ApplyPoseOffsetsToSubtree(static_cast<uint8_t*>(header.data), header.count))
        g_poseEditAppliedCount.fetch_add(1, std::memory_order_relaxed);
}

void __fastcall HookPoseFinalizeType1(void* mesh) {
    if (g_originalPoseFinalizeType1) g_originalPoseFinalizeType1(mesh);
    ApplyPoseWriterFromFinalizer(mesh, 0);
}

void __fastcall HookPoseFinalizeType2(void* mesh) {
    if (g_originalPoseFinalizeType2) g_originalPoseFinalizeType2(mesh);
    ApplyPoseWriterFromFinalizer(mesh, 1);
}

void __fastcall HookPoseFinalizeType5(void* mesh) {
    if (g_originalPoseFinalizeType5) g_originalPoseFinalizeType5(mesh);
    ApplyPoseWriterFromFinalizer(mesh, 2);
}

bool ApplyPoseOffsetsToSubtree(uint8_t* poseData, int poseBoneCount) {
    const int selectedBone = g_poseSelectedBone.load(std::memory_order_relaxed);
    if (!poseData || poseBoneCount <= 0 || selectedBone < 0 || selectedBone >= poseBoneCount) return false;
    int selectedIndices[512]{};
    int selectedCount = 1;
    selectedIndices[0] = selectedBone;
    if (g_poseApplyToSubtreeDesired.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        if (g_poseSelectedSubtreeRoot == selectedBone && g_poseSelectedSubtreeCount > 0) {
            selectedCount = (std::min)(g_poseSelectedSubtreeCount, static_cast<int>(std::size(selectedIndices)));
            for (int i = 0; i < selectedCount; ++i)
                selectedIndices[i] = g_poseSelectedSubtreeIndices[static_cast<size_t>(i)];
        }
    }
    return ApplyPoseOffsetsToSubtreeSelection(poseData, poseBoneCount,
        selectedIndices, selectedCount, selectedBone);
}

bool ApplyPoseOffsetsToSubtreeSelection(uint8_t* poseData, int poseBoneCount,
    const int* selectedIndices, int selectedCount, int pivotBone) {
    if (!poseData || poseBoneCount <= 0 || !selectedIndices || selectedCount <= 0 ||
        selectedCount > 4096 || pivotBone < 0 || pivotBone >= poseBoneCount) return false;
    const float pitch = g_poseRotationOffsetDegrees[0].load(std::memory_order_relaxed);
    const float yaw = g_poseRotationOffsetDegrees[1].load(std::memory_order_relaxed);
    const float roll = g_poseRotationOffsetDegrees[2].load(std::memory_order_relaxed);
    const float translationDelta[3] = {
        g_poseTranslationOffset[0].load(std::memory_order_relaxed),
        g_poseTranslationOffset[1].load(std::memory_order_relaxed),
        g_poseTranslationOffset[2].load(std::memory_order_relaxed)
    };
    const bool rotationRequested = std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll) &&
        (pitch != 0.0f || yaw != 0.0f || roll != 0.0f);
    const bool translationRequested =
        (std::isfinite(translationDelta[0]) && translationDelta[0] != 0.0f) ||
        (std::isfinite(translationDelta[1]) && translationDelta[1] != 0.0f) ||
        (std::isfinite(translationDelta[2]) && translationDelta[2] != 0.0f);
    if (!rotationRequested && !translationRequested) return false;

    float delta[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (rotationRequested) {
        constexpr float kDegreesToRadians = 0.01745329251994329577f;
        const float hp = pitch * kDegreesToRadians * 0.5f;
        const float hy = yaw * kDegreesToRadians * 0.5f;
        const float hr = roll * kDegreesToRadians * 0.5f;
        const float sp = sinf(hp), cp = cosf(hp);
        const float sy = sinf(hy), cy = cosf(hy);
        const float sr = sinf(hr), cr = cosf(hr);

        // UE Rotator order is yaw * pitch * roll; quaternion components are X/Y/Z/W.
        const float eulerDelta[4] = {
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        };
        memcpy(delta, eulerDelta, sizeof(delta));
    }

    const auto* rootAtom = poseData + static_cast<size_t>(pivotBone) * 0x20;
    if (!IsReadableAddress(rootAtom, 0x20)) return false;
    float pivot[3]{};
    memcpy(pivot, rootAtom + 0x10, sizeof(pivot));

    bool anyChanged = false;
    for (int item = 0; item < selectedCount; ++item) {
        const int bone = selectedIndices[item];
        if (bone < 0 || bone >= poseBoneCount) continue;
        auto* atom = poseData + static_cast<size_t>(bone) * 0x20;
        if (!IsWritableAddress(atom, 0x20)) continue;

        if (rotationRequested) {
            float base[4]{};
            memcpy(base, atom, sizeof(base));
            // Pre-multiply so the requested delta is expressed in component-space axes.
            float result[4] = {
                delta[3] * base[0] + delta[0] * base[3] + delta[1] * base[2] - delta[2] * base[1],
                delta[3] * base[1] - delta[0] * base[2] + delta[1] * base[3] + delta[2] * base[0],
                delta[3] * base[2] + delta[0] * base[1] - delta[1] * base[0] + delta[2] * base[3],
                delta[3] * base[3] - delta[0] * base[0] - delta[1] * base[1] - delta[2] * base[2]
            };
            const float normSq = result[0] * result[0] + result[1] * result[1] +
                result[2] * result[2] + result[3] * result[3];
            if (!std::isfinite(normSq) || normSq <= 1.0e-8f) continue;
            const float invNorm = 1.0f / sqrtf(normSq);
            for (float& component : result) component *= invNorm;
            memcpy(atom, result, sizeof(result));
            anyChanged = true;
        }

        float position[3]{};
        memcpy(position, atom + 0x10, sizeof(position));
        if (rotationRequested) {
            const float qx = delta[0], qy = delta[1], qz = delta[2], qw = delta[3];
            const float vx = position[0] - pivot[0];
            const float vy = position[1] - pivot[1];
            const float vz = position[2] - pivot[2];
            const float tx = 2.0f * (qy * vz - qz * vy);
            const float ty = 2.0f * (qz * vx - qx * vz);
            const float tz = 2.0f * (qx * vy - qy * vx);
            const float crossX = qy * tz - qz * ty;
            const float crossY = qz * tx - qx * tz;
            const float crossZ = qx * ty - qy * tx;
            position[0] = pivot[0] + vx + qw * tx + crossX;
            position[1] = pivot[1] + vy + qw * ty + crossY;
            position[2] = pivot[2] + vz + qw * tz + crossZ;
        }
        bool positionChanged = false;
        for (int axis = 0; axis < 3; ++axis) {
            const float offset = translationDelta[axis];
            if (!std::isfinite(offset) || offset == 0.0f) continue;
            const float value = position[axis] + offset;
            if (!std::isfinite(value)) continue;
            position[axis] = value;
            positionChanged = true;
        }
        if (positionChanged) {
            memcpy(atom + 0x10, position, sizeof(position));
            anyChanged = true;
        }
    }
    return anyChanged;
}

uint64_t ReadPoseSpaceBasesHash(void* mesh) {
    const int offset = g_poseSpaceBasesOffset;
    if (!mesh || offset < 0 || !IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + offset,
        sizeof(void*) + 8)) return 0;
    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
    memcpy(&header, reinterpret_cast<uint8_t*>(mesh) + offset, sizeof(header));
    if (!header.data || header.count <= 0 || header.count > 4096 ||
        header.capacity < header.count || header.capacity > 4096) return 0;
    return HashPoseBytes(header.data, static_cast<size_t>(header.count) * 0x20);
}

void __fastcall HookPoseControlEvaluator(void* mesh, int mode) {
    const int modeIndex = mode == 1 ? 1 : 0;
    g_poseControlEvaluatorCalls[modeIndex].fetch_add(1, std::memory_order_relaxed);
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
    }
    const bool selected = mesh && mesh == selectedMesh;
    const uint64_t beforeHash = selected ? ReadPoseSpaceBasesHash(mesh) : 0;
    if (g_originalPoseControlEvaluator) g_originalPoseControlEvaluator(mesh, mode);
    if (!selected) return;
    g_poseSelectedControlEvaluatorCalls[modeIndex].fetch_add(1, std::memory_order_relaxed);
    const uint64_t afterHash = ReadPoseSpaceBasesHash(mesh);
    if (beforeHash && afterHash && beforeHash != afterHash)
        g_poseSelectedControlEvaluatorChangedCalls[modeIndex].fetch_add(1, std::memory_order_relaxed);
}

void __fastcall HookPoseSpaceBasesCombine(void* sourceAtoms, void* parentAtoms,
    void* boneMap, void* outputHeader) {
    g_poseSpaceBasesCombineCalls.fetch_add(1, std::memory_order_relaxed);
    const bool freeze = g_poseFreezeDesired.load(std::memory_order_relaxed);
    const bool edit = g_poseEditDesired.load(std::memory_order_relaxed);

    // Freeze the tracked output by skipping its combine call. The buffer already
    // contains the captured pose, so this avoids restoring a full array after
    // every high-frequency invocation of this helper.
    if (freeze && outputHeader) {
        struct FastHeader { void* data; int32_t count; int32_t capacity; } fastHeader{};
        // outputHeader is the valid TArray passed to the original routine; read
        // it directly here to avoid a VirtualQuery on every helper invocation.
        memcpy(&fastHeader, outputHeader, sizeof(fastHeader));
        if (fastHeader.data && fastHeader.count > 0 && fastHeader.count <= 4096) {
            const uintptr_t data = reinterpret_cast<uintptr_t>(fastHeader.data);
            for (size_t i = 0; i < kPoseFastFrozenBufferCount; ++i) {
                const uintptr_t frozenData = g_poseFastFrozenSpaceBases[i].load(std::memory_order_acquire);
                if (frozenData && frozenData == data &&
                    g_poseFastFrozenBoneCounts[i].load(std::memory_order_relaxed) == fastHeader.count) {
                    g_poseSpaceBasesCombineMatches.fetch_add(1, std::memory_order_relaxed);
                    g_poseSpaceBasesCombineWrites.fetch_add(1, std::memory_order_relaxed);
                    if (g_poseSpaceBasesCombineWrites.load(std::memory_order_relaxed) == 1)
                        Log(L"POSE FREEZE: skipped tracked SpaceBases combine at +0x3288A0; captured pose retained");
                    return;
                }
            }
        }
    }

    if (g_originalPoseSpaceBasesCombine)
        g_originalPoseSpaceBasesCombine(sourceAtoms, parentAtoms, boneMap, outputHeader);
    // Non-target arrays need no post-combine inspection while only freezing.
    // The tracked arrays were already handled by the fast skip above.
    if (!edit || !outputHeader) return;

    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
    if (!IsReadableAddress(outputHeader, sizeof(header))) return;
    memcpy(&header, outputHeader, sizeof(header));
    if (!header.data || header.count <= 0 || header.count > 4096 ||
        header.capacity < header.count || header.capacity > 4096) return;
    const size_t poseBytes = static_cast<size_t>(header.count) * 0x20;
    if (!IsReadableAddress(header.data, poseBytes) || !IsWritableAddress(header.data, poseBytes)) return;

    bool restored = false;
    if (freeze) {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        const auto snapshot = std::find_if(g_poseFrozenComponents.begin(), g_poseFrozenComponents.end(),
            [&](const auto& value) {
                return value.data == header.data && value.boneCount == header.count &&
                    value.bytes.size() == poseBytes;
            });
        if (snapshot != g_poseFrozenComponents.end()) {
            g_poseSpaceBasesCombineMatches.fetch_add(1, std::memory_order_relaxed);
            memcpy(header.data, snapshot->bytes.data(), poseBytes);
            g_poseSpaceBasesCombineWrites.fetch_add(1, std::memory_order_relaxed);
            restored = true;
        }
    }

    if (edit) {
        void* selectedMesh = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            selectedMesh = g_poseMesh;
        }
        if (selectedMesh && g_poseSpaceBasesOffset >= 0 &&
            IsReadableAddress(reinterpret_cast<uint8_t*>(selectedMesh) + g_poseSpaceBasesOffset, sizeof(header))) {
            TArrayHeader selectedHeader{};
            memcpy(&selectedHeader, reinterpret_cast<uint8_t*>(selectedMesh) + g_poseSpaceBasesOffset, sizeof(header));
            if (selectedHeader.data == header.data && selectedHeader.count == header.count &&
                ApplyPoseOffsetsToSubtree(static_cast<uint8_t*>(header.data), header.count)) {
                g_poseEditAppliedCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    if (restored && g_poseSpaceBasesCombineWrites.load(std::memory_order_relaxed) == 1)
        Log(L"POSE FREEZE: restored captured SpaceBases at Rocksteady bone-combine helper +0x3288A0");
}

void __fastcall HookPoseSpaceBasesToLocal(void* refPose, void* spaceBasesHeader, void* localAtomsHeader) {
    void* immediateCaller = _ReturnAddress();
    g_poseSpaceBasesReaderCalls.fetch_add(1, std::memory_order_relaxed);
    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } source{}, output{};
    memcpy(&source, spaceBasesHeader, sizeof(source));
    memcpy(&output, localAtomsHeader, sizeof(output));
    bool trackedSource = false;
    if (source.data && source.count > 0 && source.count <= 4096) {
        const uintptr_t data = reinterpret_cast<uintptr_t>(source.data);
        for (size_t i = 0; i < kPoseFastFrozenBufferCount; ++i) {
            if (g_poseFastFrozenSpaceBases[i].load(std::memory_order_acquire) == data &&
                g_poseFastFrozenBoneCounts[i].load(std::memory_order_relaxed) == source.count) {
                trackedSource = true;
                break;
            }
        }
        // Trace the currently inspected mesh during ordinary animation too. The prior
        // version only recognized arrays published by Freeze, so an unfrozen run could
        // count helper calls but never identify Catwoman's live SpaceBases buffer.
        if (!trackedSource) {
            const uintptr_t mesh = g_poseTraceSelectedMesh.load(std::memory_order_acquire);
            const int offset = g_poseSpaceBasesOffset;
            if (mesh && offset >= 0) {
                const auto* headerAddress = reinterpret_cast<const uint8_t*>(mesh) + offset;
                TArrayHeader liveHeader{};
                if (IsReadableAddress(headerAddress, sizeof(liveHeader))) {
                    memcpy(&liveHeader, headerAddress, sizeof(liveHeader));
                    trackedSource = liveHeader.data == source.data && liveHeader.count == source.count;
                }
            }
        }
    }

    const int selectedBone = g_poseSelectedBone.load(std::memory_order_relaxed);
    float inputAtom[8]{};
    const bool canSample = trackedSource && selectedBone >= 0 && selectedBone < source.count &&
        output.data && selectedBone < output.count;
    if (canSample) {
        memcpy(inputAtom, static_cast<uint8_t*>(source.data) + static_cast<size_t>(selectedBone) * 0x20,
            sizeof(inputAtom));
    }

    void* frames[5]{};
    USHORT frameCount = 0;
    bool logThisMatch = false;
    if (trackedSource) {
        g_poseSpaceBasesReaderMatches.fetch_add(1, std::memory_order_relaxed);
        g_poseSpaceBasesReaderLastInput.store(reinterpret_cast<uintptr_t>(source.data), std::memory_order_relaxed);
        g_poseSpaceBasesReaderLastOutput.store(reinterpret_cast<uintptr_t>(output.data), std::memory_order_relaxed);
        g_poseSpaceBasesReaderLastCaller.store(reinterpret_cast<uintptr_t>(immediateCaller), std::memory_order_relaxed);
        g_poseSpaceBasesReaderStack[0].store(reinterpret_cast<uintptr_t>(immediateCaller), std::memory_order_relaxed);
        logThisMatch = !g_poseSpaceBasesReaderTraceLogged.exchange(true, std::memory_order_acq_rel);
        if (logThisMatch) {
            frames[0] = immediateCaller;
            frameCount = 1;
            frameCount += CaptureStackBackTrace(1, static_cast<DWORD>(std::size(frames) - 1), frames + 1, nullptr);
        }
    }

    if (g_originalPoseSpaceBasesToLocal)
        g_originalPoseSpaceBasesToLocal(refPose, spaceBasesHeader, localAtomsHeader);

    if (logThisMatch) {
        const uintptr_t caller = frameCount ? reinterpret_cast<uintptr_t>(frames[0]) : 0;
        g_poseSpaceBasesReaderLastCaller.store(caller, std::memory_order_relaxed);
        for (size_t i = 0; i < g_poseSpaceBasesReaderStack.size(); ++i)
            g_poseSpaceBasesReaderStack[i].store(i < frameCount ? reinterpret_cast<uintptr_t>(frames[i]) : 0,
                std::memory_order_relaxed);
        float outputAtom[8]{};
        if (canSample) {
            memcpy(outputAtom, static_cast<uint8_t*>(output.data) + static_cast<size_t>(selectedBone) * 0x20,
                sizeof(outputAtom));
        }
        wchar_t message[768]{};
        swprintf_s(message,
            L"POSE READER +328CB0: SpaceBases 0x%llX[%d] -> output 0x%llX[%d] caller=+0x%llX selected=%d input T(%.3f,%.3f,%.3f) output T(%.3f,%.3f,%.3f) stack",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(source.data)),
            source.count, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(output.data)),
            output.count, static_cast<unsigned long long>(caller >= g_gameBase ? caller - g_gameBase : 0),
            selectedBone, inputAtom[4], inputAtom[5], inputAtom[6],
            outputAtom[4], outputAtom[5], outputAtom[6]);
        for (USHORT i = 0; i < frameCount; ++i) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(frames[i]);
            const uintptr_t rva = address >= g_gameBase ? address - g_gameBase : 0;
            const size_t used = wcslen(message);
            if (used + 40 >= std::size(message)) break;
            swprintf_s(message + used, std::size(message) - used, L" +0x%llX",
                static_cast<unsigned long long>(rva));
        }
        Log(message);
    }
}

void __fastcall HookPoseLocalAtomsUpdate(void* controlList, void* mesh) {
    g_poseLocalAtomsUpdateCalls.fetch_add(1, std::memory_order_relaxed);
    if (g_originalPoseLocalAtomsUpdate) g_originalPoseLocalAtomsUpdate(controlList, mesh);
    if (!g_poseFreezeDesired.load(std::memory_order_relaxed) || !mesh || g_poseLocalAtomsOffset < 0) return;

    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
    auto* address = reinterpret_cast<uint8_t*>(mesh) + g_poseLocalAtomsOffset;
    if (!IsReadableAddress(address, sizeof(header))) return;
    memcpy(&header, address, sizeof(header));
    if (!header.data || header.count <= 0 || header.count > 4096 ||
        header.capacity < header.count || header.capacity > 4096) return;
    const size_t atomBytes = static_cast<size_t>(header.count) * 0x20;
    if (!IsReadableAddress(header.data, atomBytes) || !IsWritableAddress(header.data, atomBytes)) return;

    bool restored = false;
    {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        const auto snapshot = std::find_if(g_poseFrozenComponents.begin(), g_poseFrozenComponents.end(),
            [&](const auto& value) {
                return value.mesh == mesh && value.localAtomsData == header.data &&
                    value.localAtomsBoneCount == header.count && value.localAtomsBytes.size() == atomBytes;
            });
        if (snapshot != g_poseFrozenComponents.end()) {
            g_poseLocalAtomsUpdateMatches.fetch_add(1, std::memory_order_relaxed);
            memcpy(header.data, snapshot->localAtomsBytes.data(), atomBytes);
            g_poseLocalAtomsUpdateWrites.fetch_add(1, std::memory_order_relaxed);
            restored = true;
        }
    }
    if (restored && g_poseLocalAtomsUpdateWrites.load(std::memory_order_relaxed) == 1)
        Log(L"POSE FREEZE: restored tracked Pawn LocalAtoms after Rocksteady update helper +0x2F7370");
}

void __fastcall HookPoseFinalizeDispatcher(void* mesh) {
    int modeIndex = 6;
    if (mesh && IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + 0x460, sizeof(uint8_t))) {
        uint8_t mode{};
        memcpy(&mode, reinterpret_cast<uint8_t*>(mesh) + 0x460, sizeof(mode));
        if (mode <= 5) modeIndex = mode;
    }
    g_poseDispatcherModeCalls[modeIndex].fetch_add(1, std::memory_order_relaxed);
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
    }
    const bool selected = mesh && mesh == selectedMesh;
    if (selected) g_poseSelectedDispatcherModeCalls[modeIndex].fetch_add(1, std::memory_order_relaxed);
    const uint64_t beforeDispatcherHash = selected ? ReadPoseSpaceBasesHash(mesh) : 0;
    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t callerRva = returnAddress >= g_gameBase ? returnAddress - g_gameBase : 0;
    bool recorded = false;
    for (auto& caller : g_poseDispatcherCallers) {
        uintptr_t knownAddress = caller.address.load(std::memory_order_relaxed);
        if (knownAddress == returnAddress) {
            caller.calls.fetch_add(1, std::memory_order_relaxed);
            recorded = true;
            break;
        }
        if (knownAddress == 0 && caller.address.compare_exchange_strong(
                knownAddress, returnAddress, std::memory_order_relaxed)) {
            caller.calls.fetch_add(1, std::memory_order_relaxed);
            wchar_t message[160]{};
            swprintf_s(message, L"POSE CALLER: dispatcher return=0x%llX RVA=+0x%llX",
                static_cast<unsigned long long>(returnAddress),
                static_cast<unsigned long long>(callerRva));
            Log(message);
            recorded = true;
            break;
        }
    }
    if (!recorded) g_poseDispatcherCallerOverflow.fetch_add(1, std::memory_order_relaxed);
    int callerIndex = 4;
    switch (callerRva) {
    case 0x0E37DC: callerIndex = 0; break;
    case 0x34DF53: callerIndex = 1; break;
    case 0x95B9E7: callerIndex = 2; break;
    case 0x95F1C4: callerIndex = 3; break;
    default: break;
    }
    g_poseDispatcherCallerCalls[callerIndex].fetch_add(1, std::memory_order_relaxed);
    if (g_originalPoseFinalizeDispatcher) g_originalPoseFinalizeDispatcher(mesh);
    const uint64_t afterDispatcherHash = selected ? ReadPoseSpaceBasesHash(mesh) : 0;
    if (beforeDispatcherHash && afterDispatcherHash && beforeDispatcherHash != afterDispatcherHash)
        g_poseSelectedDispatcherOriginalChanges.fetch_add(1, std::memory_order_relaxed);
    if (selected) {
        const uint64_t afterWriterHash = ReadPoseSpaceBasesHash(mesh);
        if (afterDispatcherHash && afterWriterHash && afterDispatcherHash != afterWriterHash)
            g_poseSelectedDispatcherWriterChanges.fetch_add(1, std::memory_order_relaxed);
        const uintptr_t previousMesh = g_poseSelectedDispatcherLastPostWriterMesh.exchange(
            reinterpret_cast<uintptr_t>(mesh), std::memory_order_relaxed);
        const uint64_t previousHash = g_poseSelectedDispatcherLastPostWriterHash.exchange(afterWriterHash,
            std::memory_order_relaxed);
        if (previousMesh == reinterpret_cast<uintptr_t>(mesh) && previousHash &&
            afterWriterHash && previousHash != afterWriterHash)
            g_poseSelectedDispatcherPostWriterChanges.fetch_add(1, std::memory_order_relaxed);
    }
}

bool InstallPoseFinalizerHook(int index) {
    if (index < 0 || index >= 3) return false;
    if (g_poseFinalizerHookInstalled[index].load(std::memory_order_acquire)) return true;
    if (g_poseFinalizerHookAttempted[index].exchange(true, std::memory_order_acq_rel)) return false;

    uintptr_t rva = 0;
    const uint8_t* signature = nullptr;
    size_t signatureSize = 0;
    void* detour = nullptr;
    void** original = nullptr;
    static constexpr uint8_t sigType1[] = {
        0x40,0x53,0x48,0x83,0xEC,0x40,0x8B,0x81,0x74,0x04,0x00,0x00,0x48,0x8B,0x99,0x80,0x02,0x00,0x00
    };
    static constexpr uint8_t sigType2[] = {
        0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00,0x8B,0x81,0x74,0x04,0x00,0x00
    };
    static constexpr uint8_t sigType5[] = {
        0x48,0x8B,0xC4,0x55,0x48,0x8D,0x68,0xC8,0x48,0x81,0xEC,0x30,0x01,0x00,0x00,0x48,0xC7,0x44,0x24,0x20,
        0xFE,0xFF,0xFF,0xFF
    };
    switch (index) {
    case 0:
        rva = 0x2F4EF0; signature = sigType1; signatureSize = sizeof(sigType1);
        detour = reinterpret_cast<void*>(&HookPoseFinalizeType1);
        original = reinterpret_cast<void**>(&g_originalPoseFinalizeType1);
        break;
    case 1:
        rva = 0x2FD160; signature = sigType2; signatureSize = sizeof(sigType2);
        detour = reinterpret_cast<void*>(&HookPoseFinalizeType2);
        original = reinterpret_cast<void**>(&g_originalPoseFinalizeType2);
        break;
    default:
        rva = 0x2FD550; signature = sigType5; signatureSize = sizeof(sigType5);
        detour = reinterpret_cast<void*>(&HookPoseFinalizeType5);
        original = reinterpret_cast<void**>(&g_originalPoseFinalizeType5);
        break;
    }

    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + rva);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, signatureSize) ||
        memcmp(target, signature, signatureSize) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        wchar_t message[192]{};
        swprintf_s(message, L"POSE EDIT: finalizer variant %d signature mismatch at RVA +0x%llX",
            index + 1, static_cast<unsigned long long>(rva));
        Log(message);
        return false;
    }

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        return false;
    }
    const MH_STATUS create = MH_CreateHook(target, detour, original);
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        wchar_t message[128]{};
        swprintf_s(message, L"POSE EDIT: unable to create finalizer variant %d hook", index + 1);
        Log(message);
        return false;
    }
    g_poseFinalizerHookInstalled[index].store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        wchar_t message[128]{};
        swprintf_s(message, L"POSE EDIT: unable to enable finalizer variant %d hook", index + 1);
        Log(message);
        return false;
    }
    g_poseFinalizerHookEnabled[index].store(true, std::memory_order_release);
    g_poseFinalizerHookMask.fetch_or(1 << index, std::memory_order_relaxed);
    wchar_t message[128]{};
    swprintf_s(message, L"POSE EDIT: finalizer handler mode %d hooked at RVA +0x%llX",
        index == 0 ? 1 : index == 1 ? 2 : 5, static_cast<unsigned long long>(rva));
    Log(message);
    return true;
}

void SetPoseFinalizerHooksEnabled(bool enabled) {
    struct HookTarget { uintptr_t rva; };
    static constexpr HookTarget targets[] = {{0x2F4EF0}, {0x2FD160}, {0x2FD550}};
    for (int i = 0; i < 3; ++i) {
        if (!g_poseFinalizerHookInstalled[i].load(std::memory_order_acquire)) continue;
        const bool wasEnabled = g_poseFinalizerHookEnabled[i].load(std::memory_order_acquire);
        if (wasEnabled == enabled) continue;
        auto* target = reinterpret_cast<void*>(g_gameBase + targets[i].rva);
        const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
        if ((enabled && result == MH_OK) || (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
            g_poseFinalizerHookEnabled[i].store(enabled, std::memory_order_release);
        } else {
            g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
            wchar_t message[128]{};
            swprintf_s(message, L"POSE EDIT: unable to %s finalizer variant %d",
                enabled ? L"enable" : L"disable", i + 1);
            Log(message);
        }
    }

}

void SetPoseDispatcherHookEnabled(bool enabled) {
    if (!g_poseDispatcherHookInstalled.load(std::memory_order_acquire)) return;
    const bool wasEnabled = g_poseDispatcherHookEnabled.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    auto* target = reinterpret_cast<void*>(g_gameBase + 0x94F970);
    const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
    if ((enabled && (result == MH_OK || result == MH_ERROR_ENABLED)) ||
        (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
        g_poseDispatcherHookEnabled.store(enabled, std::memory_order_release);
        if (enabled) g_poseFinalizerHookMask.fetch_or(1 << 3, std::memory_order_relaxed);
        else g_poseFinalizerHookMask.fetch_and(~(1 << 3), std::memory_order_relaxed);
    } else {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(enabled ? L"POSE TRACE: unable to enable animation dispatcher hook" :
            L"POSE TRACE: unable to disable animation dispatcher hook");
    }
}

void SetPoseControlEvaluatorHookEnabled(bool enabled) {
    if (!g_poseControlEvaluatorHookInstalled.load(std::memory_order_acquire)) return;
    const bool wasEnabled = g_poseControlEvaluatorHookEnabled.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    auto* target = reinterpret_cast<void*>(g_gameBase + 0x304340);
    const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
    if ((enabled && (result == MH_OK || result == MH_ERROR_ENABLED)) ||
        (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
        g_poseControlEvaluatorHookEnabled.store(enabled, std::memory_order_release);
    } else {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(enabled ? L"POSE TRACE: unable to enable mode 0/1 control evaluator hook" :
            L"POSE TRACE: unable to disable mode 0/1 control evaluator hook");
    }
}

bool InstallPoseControlEvaluatorHook() {
    if (g_poseControlEvaluatorHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_poseControlEvaluatorHookAttempted.exchange(true, std::memory_order_acq_rel)) return false;
    static constexpr uint8_t signature[] = {
        0x48,0x8B,0xC4,0x89,0x50,0x10,0x48,0x89,0x48,0x08,0x55,0x53,0x56,0x57,
        0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57
    };
    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + 0x304340);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        Log(L"POSE TRACE: mode 0/1 control evaluator signature mismatch at RVA +0x304340");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        return false;
    }
    const MH_STATUS create = MH_CreateHook(target, &HookPoseControlEvaluator,
        reinterpret_cast<void**>(&g_originalPoseControlEvaluator));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to create mode 0/1 control evaluator hook");
        return false;
    }
    g_poseControlEvaluatorHookInstalled.store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to enable mode 0/1 control evaluator hook");
        return false;
    }
    g_poseControlEvaluatorHookEnabled.store(true, std::memory_order_release);
    Log(L"POSE TRACE: mode 0/1 control evaluator hooked at RVA +0x304340");
    return true;
}

bool InstallPoseSpaceBasesCombineHook() {
    if (g_poseSpaceBasesCombineHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_poseSpaceBasesCombineHookAttempted.exchange(true, std::memory_order_acq_rel)) return false;
    if (!g_buildProfile || strcmp(g_buildProfile->name, "Steam") != 0) {
        g_poseFinalizeFailure.store(7, std::memory_order_relaxed);
        return false;
    }
    static constexpr uint8_t signature[] = {
        0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,
        0x48,0x89,0x78,0x20,0x41,0x54,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00
    };
    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + 0x3288A0);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        Log(L"POSE TRACE: Rocksteady bone-combine helper signature mismatch at RVA +0x3288A0");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        return false;
    }
    const MH_STATUS create = MH_CreateHook(target, &HookPoseSpaceBasesCombine,
        reinterpret_cast<void**>(&g_originalPoseSpaceBasesCombine));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to create Rocksteady bone-combine helper hook");
        return false;
    }
    g_poseSpaceBasesCombineHookInstalled.store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to enable Rocksteady bone-combine helper hook");
        return false;
    }
    g_poseSpaceBasesCombineHookEnabled.store(true, std::memory_order_release);
    g_poseFinalizerHookMask.fetch_or(1 << 4, std::memory_order_relaxed);
    Log(L"POSE TRACE: Rocksteady SpaceBases bone-combine helper hooked at RVA +0x3288A0");
    return true;
}

void SetPoseSpaceBasesCombineHookEnabled(bool enabled) {
    if (!g_poseSpaceBasesCombineHookInstalled.load(std::memory_order_acquire)) return;
    const bool wasEnabled = g_poseSpaceBasesCombineHookEnabled.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    auto* target = reinterpret_cast<void*>(g_gameBase + 0x3288A0);
    const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
    if ((enabled && (result == MH_OK || result == MH_ERROR_ENABLED)) ||
        (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
        g_poseSpaceBasesCombineHookEnabled.store(enabled, std::memory_order_release);
        if (enabled) g_poseFinalizerHookMask.fetch_or(1 << 4, std::memory_order_relaxed);
        else g_poseFinalizerHookMask.fetch_and(~(1 << 4), std::memory_order_relaxed);
    } else {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(enabled ? L"POSE TRACE: unable to re-enable Rocksteady bone-combine hook" :
            L"POSE TRACE: unable to disable Rocksteady bone-combine hook");
    }
}

bool InstallPoseSpaceBasesToLocalHook() {
    if (g_poseSpaceBasesToLocalHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_poseSpaceBasesToLocalHookAttempted.exchange(true, std::memory_order_acq_rel)) return false;
    if (!g_buildProfile || strcmp(g_buildProfile->name, "Steam") != 0) return false;
    static constexpr uint8_t signature[] = {
        0x4C,0x8B,0xDC,0x53,0x48,0x81,0xEC,0xB0,0x00,0x00,0x00,0x4C,0x8B,0xC9,
        0x48,0x63,0x49,0x08,0x49,0x8B,0xD8,0x4C,0x8B,0xD2,0x85,0xC9
    };
    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + 0x328CB0);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        Log(L"POSE TRACE: SpaceBases-to-LocalAtoms helper signature mismatch at RVA +0x328CB0");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const MH_STATUS create = MH_CreateHook(target, &HookPoseSpaceBasesToLocal,
        reinterpret_cast<void**>(&g_originalPoseSpaceBasesToLocal));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        Log(L"POSE TRACE: unable to create SpaceBases-to-LocalAtoms reader hook");
        return false;
    }
    g_poseSpaceBasesToLocalHookInstalled.store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        Log(L"POSE TRACE: unable to enable SpaceBases-to-LocalAtoms reader hook");
        return false;
    }
    g_poseSpaceBasesToLocalHookEnabled.store(true, std::memory_order_release);
    g_poseFinalizerHookMask.fetch_or(1 << 6, std::memory_order_relaxed);
    Log(L"POSE TRACE: SpaceBases reader hooked at RVA +0x328CB0; matches include the inspected live mesh and frozen Pawn arrays");
    return true;
}

void SetPoseSpaceBasesToLocalHookEnabled(bool enabled) {
    if (!g_poseSpaceBasesToLocalHookInstalled.load(std::memory_order_acquire)) return;
    const bool wasEnabled = g_poseSpaceBasesToLocalHookEnabled.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    auto* target = reinterpret_cast<void*>(g_gameBase + 0x328CB0);
    const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
    if ((enabled && (result == MH_OK || result == MH_ERROR_ENABLED)) ||
        (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
        g_poseSpaceBasesToLocalHookEnabled.store(enabled, std::memory_order_release);
        if (enabled) g_poseFinalizerHookMask.fetch_or(1 << 6, std::memory_order_relaxed);
        else g_poseFinalizerHookMask.fetch_and(~(1 << 6), std::memory_order_relaxed);
    } else {
        Log(enabled ? L"POSE TRACE: unable to re-enable SpaceBases reader hook" :
            L"POSE TRACE: unable to disable SpaceBases reader hook");
    }
}

bool InstallPoseLocalAtomsUpdateHook() {
    if (g_poseLocalAtomsUpdateHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_poseLocalAtomsUpdateHookAttempted.exchange(true, std::memory_order_acq_rel)) return false;
    if (!g_buildProfile || strcmp(g_buildProfile->name, "Steam") != 0) {
        g_poseFinalizeFailure.store(7, std::memory_order_relaxed);
        return false;
    }
    static constexpr uint8_t signature[] = {
        0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x83,0xEC,0x20,0x33,0xFF,0x48,0x8B,0xF2,0x48,0x8B,0xD9
    };
    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + 0x2F7370);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        Log(L"POSE TRACE: Rocksteady LocalAtoms helper signature mismatch at RVA +0x2F7370");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        return false;
    }
    const MH_STATUS create = MH_CreateHook(target, &HookPoseLocalAtomsUpdate,
        reinterpret_cast<void**>(&g_originalPoseLocalAtomsUpdate));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to create Rocksteady LocalAtoms helper hook");
        return false;
    }
    g_poseLocalAtomsUpdateHookInstalled.store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE TRACE: unable to enable Rocksteady LocalAtoms helper hook");
        return false;
    }
    g_poseLocalAtomsUpdateHookEnabled.store(true, std::memory_order_release);
    g_poseFinalizerHookMask.fetch_or(1 << 5, std::memory_order_relaxed);
    Log(L"POSE TRACE: Rocksteady LocalAtoms update helper hooked at RVA +0x2F7370");
    return true;
}

void SetPoseLocalAtomsUpdateHookEnabled(bool enabled) {
    if (!g_poseLocalAtomsUpdateHookInstalled.load(std::memory_order_acquire)) return;
    const bool wasEnabled = g_poseLocalAtomsUpdateHookEnabled.load(std::memory_order_acquire);
    if (wasEnabled == enabled) return;
    auto* target = reinterpret_cast<void*>(g_gameBase + 0x2F7370);
    const MH_STATUS result = enabled ? MH_EnableHook(target) : MH_DisableHook(target);
    if ((enabled && (result == MH_OK || result == MH_ERROR_ENABLED)) ||
        (!enabled && (result == MH_OK || result == MH_ERROR_DISABLED))) {
        g_poseLocalAtomsUpdateHookEnabled.store(enabled, std::memory_order_release);
        if (enabled) g_poseFinalizerHookMask.fetch_or(1 << 5, std::memory_order_relaxed);
        else g_poseFinalizerHookMask.fetch_and(~(1 << 5), std::memory_order_relaxed);
    } else {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(enabled ? L"POSE TRACE: unable to re-enable Rocksteady LocalAtoms helper hook" :
            L"POSE TRACE: unable to disable Rocksteady LocalAtoms helper hook");
    }
}

void RestorePoseAnimationPause() {
    void* mesh = g_poseAnimPauseMesh;
    bool readbackValid = false;
    if (mesh && g_posePauseAnimsOffset >= 0 && g_posePauseAnimsMask &&
        IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset, sizeof(uint8_t)) &&
        IsWritableAddress(reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset, sizeof(uint8_t))) {
        auto* flags = reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset;
        const uint8_t current = *flags;
        *flags = g_poseAnimPauseOriginal
            ? static_cast<uint8_t>(current | g_posePauseAnimsMask)
            : static_cast<uint8_t>(current & ~g_posePauseAnimsMask);
        g_poseAnimPauseWriteReadback.store((*flags & g_posePauseAnimsMask) ? 1 : 0, std::memory_order_release);
        readbackValid = true;
        Log(L"POSE ANIM: restored selected mesh bPauseAnims state");
    }
    if (!readbackValid) g_poseAnimPauseWriteReadback.store(-1, std::memory_order_release);
    g_poseAnimPauseMesh = nullptr;
    g_poseAnimPauseOriginal = false;
    g_poseAnimPauseApplied.store(false, std::memory_order_release);
}

bool SetPoseAnimationPause(void* mesh, bool paused) {
    if (!paused) {
        RestorePoseAnimationPause();
        return true;
    }
    if (!mesh || g_posePauseAnimsOffset < 0 || !g_posePauseAnimsMask ||
        !IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset, sizeof(uint8_t)) ||
        !IsWritableAddress(reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset, sizeof(uint8_t))) {
        g_poseAnimPauseWriteReadback.store(-1, std::memory_order_release);
        return false;
    }

    if (g_poseAnimPauseMesh != mesh) {
        RestorePoseAnimationPause();
        auto* flags = reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset;
        g_poseAnimPauseMesh = mesh;
        g_poseAnimPauseOriginal = (*flags & g_posePauseAnimsMask) != 0;
    }
    auto* flags = reinterpret_cast<uint8_t*>(mesh) + g_posePauseAnimsOffset;
    *flags = static_cast<uint8_t>(*flags | g_posePauseAnimsMask);
    g_poseAnimPauseWriteReadback.store((*flags & g_posePauseAnimsMask) ? 1 : 0, std::memory_order_release);
    g_poseAnimPauseApplied.store(true, std::memory_order_release);
    return true;
}

bool InstallPoseDispatcherHook() {
    if (g_poseDispatcherHookInstalled.load(std::memory_order_acquire)) return true;
    if (g_poseDispatcherHookAttempted.exchange(true, std::memory_order_acq_rel)) return false;
    static constexpr uint8_t signature[] = {
        0x48,0x83,0xEC,0x48,0x48,0x8B,0x91,0x80,0x02,0x00,0x00,0x4C,0x8B,0xD9
    };
    auto* target = reinterpret_cast<uint8_t*>(g_gameBase + 0x94F970);
    if (!IsExecutableAddress(target) || !IsReadableAddress(target, sizeof(signature)) ||
        memcmp(target, signature, sizeof(signature)) != 0) {
        g_poseFinalizeFailure.store(5, std::memory_order_relaxed);
        Log(L"POSE EDIT: Steam animation dispatcher signature mismatch at RVA +0x94F970");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        return false;
    }
    const MH_STATUS create = MH_CreateHook(target, &HookPoseFinalizeDispatcher,
        reinterpret_cast<void**>(&g_originalPoseFinalizeDispatcher));
    if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE EDIT: unable to create animation dispatcher hook");
        return false;
    }
    g_poseDispatcherHookInstalled.store(true, std::memory_order_release);
    const MH_STATUS enable = MH_EnableHook(target);
    if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
        g_poseFinalizeFailure.store(6, std::memory_order_relaxed);
        Log(L"POSE EDIT: unable to enable animation dispatcher hook");
        return false;
    }
    g_poseDispatcherHookEnabled.store(true, std::memory_order_release);
    g_poseFinalizerHookMask.fetch_or(1 << 3, std::memory_order_relaxed);
    Log(L"POSE TRACE: all-mode animation dispatcher hooked at RVA +0x94F970");
    return true;
}

void ProcessPoseWriterHookControlOnGameThread() {
    const bool requested = g_poseFreezeDesired.load(std::memory_order_relaxed);
    // Keep the read-only SpaceBases consumer trace active for the inspected mesh
    // during normal animation. All pose-writing and animation-control hooks remain
    // gated by Freeze below.
    if (g_poseTraceSelectedMesh.load(std::memory_order_acquire) &&
        InstallPoseSpaceBasesToLocalHook())
        SetPoseSpaceBasesToLocalHookEnabled(true);
    if (!requested) {
        g_poseEditDesired.store(false, std::memory_order_release);
        g_poseEditActive.store(false, std::memory_order_release);
        SetPoseFinalizerHooksEnabled(false);
        SetPoseSpaceBasesCombineHookEnabled(false);
        SetPoseLocalAtomsUpdateHookEnabled(false);
        SetPoseDispatcherHookEnabled(false);
        SetPoseControlEvaluatorHookEnabled(false);
        RestorePoseAnimationPause();
        if (g_poseWriterWasRequested.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(g_poseWriterMutex);
            g_poseFrozenSnapshot.clear();
            g_poseFrozenMesh = nullptr;
            g_poseFrozenCount = 0;
            g_poseFrozenSnapshotReady = false;
            g_poseFrozenComponents.clear();
            ClearPoseFastFrozenBuffers();
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
        return;
    }

    g_poseWriterWasRequested.store(true, std::memory_order_release);
    g_poseEditActive.store(g_poseEditDesired.load(), std::memory_order_release);
    if (!g_buildProfile || strcmp(g_buildProfile->name, "Steam") != 0) {
        g_poseFinalizeFailure.store(7, std::memory_order_relaxed);
        return;
    }
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
    }
    if (!selectedMesh) {
        g_poseFinalizeFailure.store(1, std::memory_order_relaxed);
        RestorePoseAnimationPause();
        return;
    }

    // Gate the verified combine helper only for arrays owned by this Pawn. Its
    // hot path skips the combine for those arrays instead of copying whole
    // arrays after each call; LocalAtoms remains restored at +0x2F7370.
    SetPoseFinalizerHooksEnabled(false);
    if (InstallPoseSpaceBasesCombineHook()) SetPoseSpaceBasesCombineHookEnabled(true);
    if (InstallPoseSpaceBasesToLocalHook()) SetPoseSpaceBasesToLocalHookEnabled(true);
    if (InstallPoseLocalAtomsUpdateHook()) SetPoseLocalAtomsUpdateHookEnabled(true);
    if (InstallPoseDispatcherHook()) SetPoseDispatcherHookEnabled(true);
    if (InstallPoseControlEvaluatorHook()) SetPoseControlEvaluatorHookEnabled(true);
}

void ApplyPoseWriterAfterEngineTick() {
    const bool freeze = g_poseFreezeDesired.load(std::memory_order_relaxed);
    if (!freeze) g_poseEditDesired.store(false, std::memory_order_release);
    const bool edit = freeze && g_poseEditDesired.load(std::memory_order_relaxed);
    if (!freeze && !edit) {
        g_poseEditActive.store(false, std::memory_order_release);
        if (g_poseWriterWasRequested.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(g_poseWriterMutex);
            g_poseFrozenSnapshot.clear();
            g_poseFrozenMesh = nullptr;
            g_poseFrozenCount = 0;
            g_poseFrozenSnapshotReady = false;
            g_poseFrozenComponents.clear();
            ClearPoseFastFrozenBuffers();
            g_poseFrozenComponentCount.store(0, std::memory_order_relaxed);
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
        return;
    }
    g_poseWriterWasRequested.store(true, std::memory_order_release);
    g_poseEditActive.store(edit, std::memory_order_release);
    g_poseFinalizeCallCount.fetch_add(1, std::memory_order_relaxed);

    std::vector<PoseMeshDiagnostic> diagnostics;
    void* selectedMesh = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_poseMutex);
        selectedMesh = g_poseMesh;
        diagnostics = g_poseMeshDiagnostics;
    }
    const int offset = g_poseSpaceBasesOffset;
    if (!g_classSkeletalMeshComponent || offset < 0) {
        g_poseFinalizeFailure.store(1, std::memory_order_relaxed);
        return;
    }

    struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
    struct WritablePoseArray {
        void* mesh{};
        void* data{};
        int count{};
        void* localAtomsData{};
        int localAtomsCount{};
    };
    std::vector<WritablePoseArray> arrays;
    arrays.reserve(diagnostics.size() + 1);
    auto addArray = [&](void* mesh) {
        if (!mesh || !IsA(mesh, g_classSkeletalMeshComponent) ||
            !IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + offset, sizeof(header))) return;
        if (std::any_of(arrays.begin(), arrays.end(), [mesh](const auto& item) { return item.mesh == mesh; })) return;
        memcpy(&header, reinterpret_cast<uint8_t*>(mesh) + offset, sizeof(header));
        if (!header.data || header.count <= 0 || header.count > 4096 ||
            header.capacity < header.count || header.capacity > 4096) return;
        const size_t poseBytes = static_cast<size_t>(header.count) * 0x20;
        if (!IsReadableAddress(header.data, poseBytes) || !IsWritableAddress(header.data, poseBytes)) return;
        void* localAtomsData = nullptr;
        int localAtomsCount = 0;
        if (g_poseLocalAtomsOffset >= 0 &&
            IsReadableAddress(reinterpret_cast<uint8_t*>(mesh) + g_poseLocalAtomsOffset, sizeof(header))) {
            TArrayHeader localHeader{};
            memcpy(&localHeader, reinterpret_cast<uint8_t*>(mesh) + g_poseLocalAtomsOffset, sizeof(localHeader));
            if (localHeader.data && localHeader.count > 0 && localHeader.count <= 4096 &&
                localHeader.capacity >= localHeader.count && localHeader.capacity <= 4096) {
                const size_t localBytes = static_cast<size_t>(localHeader.count) * 0x20;
                if (IsReadableAddress(localHeader.data, localBytes) && IsWritableAddress(localHeader.data, localBytes)) {
                    localAtomsData = localHeader.data;
                    localAtomsCount = localHeader.count;
                }
            }
        }
        arrays.push_back({mesh, header.data, header.count, localAtomsData, localAtomsCount});
    };
    for (const auto& diag : diagnostics) addArray(diag.mesh);
    addArray(selectedMesh);
    if (arrays.empty()) {
        g_poseFinalizeFailure.store(2, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(g_poseWriterMutex);
            g_poseFrozenComponents.clear();
            ClearPoseFastFrozenBuffers();
            g_poseFrozenComponentCount.store(0, std::memory_order_relaxed);
            g_poseFrozenBoneCount.store(0, std::memory_order_relaxed);
        }
        return;
    }
    g_poseArrayReadyCount.fetch_add(arrays.size(), std::memory_order_relaxed);
    g_poseFinalizeMatchCount.fetch_add(1, std::memory_order_relaxed);
    g_poseFinalizeFailure.store(0, std::memory_order_relaxed);

    uint64_t writes = 0;
    int frozenComponents = 0;
    int frozenBones = 0;
    if (freeze) {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        std::erase_if(g_poseFrozenComponents, [&](const auto& snapshot) {
            return std::none_of(arrays.begin(), arrays.end(), [&](const auto& item) {
                return item.mesh == snapshot.mesh && item.data == snapshot.data && item.count == snapshot.boneCount;
            });
        });
        for (const auto& item : arrays) {
            const size_t poseBytes = static_cast<size_t>(item.count) * 0x20;
            auto snapshot = std::find_if(g_poseFrozenComponents.begin(), g_poseFrozenComponents.end(),
                [&](const auto& value) {
                    return value.mesh == item.mesh && value.data == item.data && value.boneCount == item.count;
                });
            if (snapshot == g_poseFrozenComponents.end()) {
                PoseFrozenComponentSnapshot captured{};
                captured.mesh = item.mesh;
                captured.data = item.data;
                captured.boneCount = item.count;
                captured.bytes.resize(poseBytes);
                memcpy(captured.bytes.data(), item.data, poseBytes);
                g_poseFrozenComponents.push_back(std::move(captured));
                snapshot = std::prev(g_poseFrozenComponents.end());
            }
            if (snapshot->bytes.size() != poseBytes) continue;
            if (item.localAtomsData && item.localAtomsCount > 0) {
                const size_t localBytes = static_cast<size_t>(item.localAtomsCount) * 0x20;
                if (snapshot->localAtomsData != item.localAtomsData ||
                    snapshot->localAtomsBoneCount != item.localAtomsCount ||
                    snapshot->localAtomsBytes.size() != localBytes) {
                    snapshot->localAtomsData = item.localAtomsData;
                    snapshot->localAtomsBoneCount = item.localAtomsCount;
                    snapshot->localAtomsBytes.resize(localBytes);
                    memcpy(snapshot->localAtomsBytes.data(), item.localAtomsData, localBytes);
                }
                memcpy(item.localAtomsData, snapshot->localAtomsBytes.data(), localBytes);
            }
            memcpy(item.data, snapshot->bytes.data(), poseBytes);
            ++writes;
            ++frozenComponents;
            frozenBones += item.count;
        }
        PublishPoseFastFrozenBuffers();
        if (writes && g_poseFreezeWriteCount.load(std::memory_order_relaxed) == 0)
            Log(L"POSE FREEZE: captured and locked all valid Pawn SpaceBases arrays after EngineTick");
    } else {
        std::lock_guard<std::mutex> lock(g_poseWriterMutex);
        g_poseFrozenSnapshot.clear();
        g_poseFrozenMesh = nullptr;
        g_poseFrozenCount = 0;
        g_poseFrozenSnapshotReady = false;
        g_poseFrozenComponents.clear();
        ClearPoseFastFrozenBuffers();
    }

    g_poseFrozenComponentCount.store(frozenComponents, std::memory_order_relaxed);
    g_poseFrozenBoneCount.store(frozenBones, std::memory_order_relaxed);
    if (writes) g_poseFreezeWriteCount.fetch_add(writes, std::memory_order_relaxed);

    if (edit) {
        const int selectedBone = g_poseSelectedBone.load(std::memory_order_relaxed);
        if (g_poseAbsoluteTargetEnabled.load(std::memory_order_relaxed) &&
            g_poseAbsoluteTargetBone.load(std::memory_order_relaxed) != selectedBone) return;
        const auto selectedArray = std::find_if(arrays.begin(), arrays.end(),
            [selectedMesh](const auto& item) { return item.mesh == selectedMesh; });
        if (selectedArray == arrays.end() || selectedBone < 0 || selectedBone >= selectedArray->count) {
            g_poseFinalizeFailure.store(4, std::memory_order_relaxed);
            return;
        }
        if (g_poseAbsoluteTargetEnabled.load(std::memory_order_relaxed) &&
            g_poseAbsoluteTargetBone.load(std::memory_order_relaxed) == selectedBone) {
            // SpaceBases has just been restored from the frozen snapshot. Convert
            // the absolute component-space target shown in the UI into a delta
            // from this stable baseline before applying it to the selected branch.
            auto* atom = static_cast<uint8_t*>(selectedArray->data) +
                static_cast<size_t>(selectedBone) * 0x20;
            float baseQ[4]{};
            float baseP[3]{};
            memcpy(baseQ, atom, sizeof(baseQ));
            memcpy(baseP, atom + 0x10, sizeof(baseP));
            const float pitch = g_poseAbsoluteRotationDegrees[0].load(std::memory_order_relaxed);
            const float yaw = g_poseAbsoluteRotationDegrees[1].load(std::memory_order_relaxed);
            const float roll = g_poseAbsoluteRotationDegrees[2].load(std::memory_order_relaxed);
            constexpr float kDegreesToRadians = 0.01745329251994329577f;
            const float hp = pitch * kDegreesToRadians * 0.5f;
            const float hy = yaw * kDegreesToRadians * 0.5f;
            const float hr = roll * kDegreesToRadians * 0.5f;
            const float sp = sinf(hp), cp = cosf(hp);
            const float sy = sinf(hy), cy = cosf(hy);
            const float sr = sinf(hr), cr = cosf(hr);
            const float targetQ[4] = {
                sr * cp * cy - cr * sp * sy,
                cr * sp * cy + sr * cp * sy,
                cr * cp * sy - sr * sp * cy,
                cr * cp * cy + sr * sp * sy
            };
            float deltaQ[4] = {
                -targetQ[3] * baseQ[0] + targetQ[0] * baseQ[3] - targetQ[1] * baseQ[2] + targetQ[2] * baseQ[1],
                -targetQ[3] * baseQ[1] + targetQ[0] * baseQ[2] + targetQ[1] * baseQ[3] - targetQ[2] * baseQ[0],
                -targetQ[3] * baseQ[2] - targetQ[0] * baseQ[1] + targetQ[1] * baseQ[0] + targetQ[2] * baseQ[3],
                targetQ[3] * baseQ[3] + targetQ[0] * baseQ[0] + targetQ[1] * baseQ[1] + targetQ[2] * baseQ[2]
            };
            const float normSq = deltaQ[0] * deltaQ[0] + deltaQ[1] * deltaQ[1] +
                deltaQ[2] * deltaQ[2] + deltaQ[3] * deltaQ[3];
            if (std::isfinite(normSq) && normSq > 1.0e-8f) {
                const float invNorm = 1.0f / sqrtf(normSq);
                for (float& component : deltaQ) component *= invNorm;
                const float pitchSin = (std::max)(-1.0f, (std::min)(1.0f,
                    2.0f * (deltaQ[3] * deltaQ[1] - deltaQ[2] * deltaQ[0])));
                const float deltaPitch = asinf(pitchSin) / kDegreesToRadians;
                const float deltaYaw = atan2f(2.0f * (deltaQ[3] * deltaQ[2] + deltaQ[0] * deltaQ[1]),
                    1.0f - 2.0f * (deltaQ[1] * deltaQ[1] + deltaQ[2] * deltaQ[2])) / kDegreesToRadians;
                const float deltaRoll = atan2f(2.0f * (deltaQ[3] * deltaQ[0] + deltaQ[1] * deltaQ[2]),
                    1.0f - 2.0f * (deltaQ[0] * deltaQ[0] + deltaQ[1] * deltaQ[1])) / kDegreesToRadians;
                g_poseRotationOffsetDegrees[0].store(deltaPitch, std::memory_order_relaxed);
                g_poseRotationOffsetDegrees[1].store(deltaYaw, std::memory_order_relaxed);
                g_poseRotationOffsetDegrees[2].store(deltaRoll, std::memory_order_relaxed);
            }
            for (int axis = 0; axis < 3; ++axis) {
                const float target = g_poseAbsolutePosition[axis].load(std::memory_order_relaxed);
                g_poseTranslationOffset[axis].store(target - baseP[axis], std::memory_order_relaxed);
            }
        }
        if (ApplyPoseOffsetsToSubtree(static_cast<uint8_t*>(selectedArray->data), selectedArray->count))
            g_poseEditAppliedCount.fetch_add(1, std::memory_order_relaxed);

        std::vector<uint8_t> rootBranch(static_cast<size_t>(selectedArray->count), 0);
        rootBranch[static_cast<size_t>(selectedBone)] = 1;
        if (g_poseApplyToSubtreeDesired.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            if (g_poseSelectedSubtreeRoot == selectedBone) {
                const int count = (std::min)(g_poseSelectedSubtreeCount,
                    static_cast<int>(rootBranch.size()));
                for (int i = 0; i < count; ++i) {
                    const int rootIndex = g_poseSelectedSubtreeIndices[static_cast<size_t>(i)];
                    if (rootIndex >= 0 && rootIndex < selectedArray->count)
                        rootBranch[static_cast<size_t>(rootIndex)] = 1;
                }
            }
        }

        int mappedComponents = 0;
        int mappedBones = 0;
        if (g_poseParentAnimComponentOffset >= 0 && g_poseParentBoneMapOffset >= 0) {
            for (const auto& child : arrays) {
                if (child.mesh == selectedMesh || !child.data || child.count <= 0) continue;
                auto* childBytes = reinterpret_cast<uint8_t*>(child.mesh);
                void* parentAnimComponent = nullptr;
                if (!IsReadableAddress(childBytes + g_poseParentAnimComponentOffset, sizeof(parentAnimComponent))) continue;
                memcpy(&parentAnimComponent, childBytes + g_poseParentAnimComponentOffset, sizeof(parentAnimComponent));
                if (parentAnimComponent != selectedMesh) continue;
                if (g_poseParentAnimComponentModeOffset >= 0) {
                    uint8_t mode = 0xFF;
                    if (!IsReadableAddress(childBytes + g_poseParentAnimComponentModeOffset, sizeof(mode))) continue;
                    memcpy(&mode, childBytes + g_poseParentAnimComponentModeOffset, sizeof(mode));
                    if (mode != 1) continue;
                }

                TArrayHeader mapHeader{};
                auto* mapAddress = childBytes + g_poseParentBoneMapOffset;
                if (!IsReadableAddress(mapAddress, sizeof(mapHeader))) continue;
                memcpy(&mapHeader, mapAddress, sizeof(mapHeader));
                if (!mapHeader.data || mapHeader.count != child.count || mapHeader.count <= 0 ||
                    mapHeader.count > 4096 || mapHeader.capacity < mapHeader.count || mapHeader.capacity > 4096 ||
                    !IsReadableAddress(mapHeader.data, static_cast<size_t>(mapHeader.count) * sizeof(int32_t))) continue;

                std::vector<int> childIndices;
                childIndices.reserve(static_cast<size_t>(mapHeader.count));
                int childPivot = -1;
                for (int childIndex = 0; childIndex < mapHeader.count; ++childIndex) {
                    int32_t parentIndex = -1;
                    memcpy(&parentIndex, static_cast<uint8_t*>(mapHeader.data) +
                        static_cast<size_t>(childIndex) * sizeof(parentIndex), sizeof(parentIndex));
                    if (parentIndex < 0 || parentIndex >= selectedArray->count) continue;
                    if (parentIndex == selectedBone && childPivot < 0) childPivot = childIndex;
                    if (rootBranch[static_cast<size_t>(parentIndex)]) childIndices.push_back(childIndex);
                }
                if (childPivot < 0 || childIndices.empty()) continue;
                if (ApplyPoseOffsetsToSubtreeSelection(static_cast<uint8_t*>(child.data), child.count,
                    childIndices.data(), static_cast<int>(childIndices.size()), childPivot)) {
                    ++mappedComponents;
                    mappedBones += static_cast<int>(childIndices.size());
                }
            }
        }
        g_poseMappedChildComponentCount.store(mappedComponents, std::memory_order_relaxed);
        g_poseMappedChildBoneCount.store(mappedBones, std::memory_order_relaxed);
        if (mappedComponents && g_poseMappedChildLogCount.fetch_add(1, std::memory_order_relaxed) == 0) {
            wchar_t message[192]{};
            swprintf_s(message, L"POSE EDIT: applied selected branch through ParentBoneMap to %d child meshes / %d child bones",
                mappedComponents, mappedBones);
            Log(message);
        }
    }
}


bool ResolvePropertyOffsetLayout() {
    // Resolve UProperty::Offset_Internal from the SDK metadata itself.
    // Prefer relationships between adjacent members of Pollen/Dust/Snow;
    // this avoids depending on one hard-coded actor/component offset.
    void* pPollenBox   = FindObject(L"Engine.RPollenComponent.BoxSize");
    void* pPollenCount = FindObject(L"Engine.RPollenComponent.ParticleCount");
    void* pDustFade    = FindObject(L"Engine.RDustComponent.fade");
    void* pDustBox     = FindObject(L"Engine.RDustComponent.BoxSize");
    void* pDustCount   = FindObject(L"Engine.RDustComponent.ParticleCount");
    void* pSnowCull    = FindObject(L"Engine.RSnowComponent.CameraCullingDistance");
    void* pSnowBox     = FindObject(L"Engine.RSnowComponent.BoxSize");
    void* pSnowCount   = FindObject(L"Engine.RSnowComponent.ParticleCount");

    int field = -1;
    // UE3 UProperty::Offset_Internal is a WORD in this BatmanAK build.
    // The current SDK dump confirms it at UProperty + 0x6A.
    auto readCandidate = [](void* prop, int off, int& out) -> bool {
        if (!prop || !IsReadableAddress(reinterpret_cast<uint8_t*>(prop) + off, sizeof(uint16_t))) return false;
        out = static_cast<int>(*reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(prop) + off));
        return out >= 0 && out < 0x4000;
    };

    // In all three SDK classes these fields are declared contiguously:
    //   Pollen: BoxSize, ParticleCount
    //   Dust:   fade, BoxSize, ParticleCount
    //   Snow:   CameraCullingDistance, BoxSize, ParticleCount
    // Their runtime member offsets must therefore advance by 4 bytes.
    for (int off = 0x40; off <= 0x180; ++off) {
        int pb=0, pc=0, df=0, db=0, dc=0, sc=0, sb=0, sn=0;
        if (!readCandidate(pPollenBox, off, pb) || !readCandidate(pPollenCount, off, pc) ||
            !readCandidate(pDustFade, off, df) || !readCandidate(pDustBox, off, db) || !readCandidate(pDustCount, off, dc) ||
            !readCandidate(pSnowCull, off, sc) || !readCandidate(pSnowBox, off, sb) || !readCandidate(pSnowCount, off, sn)) continue;
        if (pc == pb + 4 && db == df + 4 && dc == db + 4 && sb == sc + 4 && sn == sb + 4) {
            field = off;
            break;
        }
    }

    // Fallback: calibrate from two long-confirmed Actor offsets if the SDK
    // relationship pass could not identify the field.
    if (field < 0) {
        void* pLocation = FindObject(L"Engine.Actor.Location");
        void* pRotation = FindObject(L"Engine.Actor.Rotation");
        if (pLocation && pRotation) {
            for (int off = 0x40; off <= 0x180; ++off) {
                int a=0, b=0;
                if (!readCandidate(pLocation, off, a) || !readCandidate(pRotation, off, b)) continue;
                if (a == static_cast<int>(kActorLocation) && b == static_cast<int>(kActorRotation)) {
                    field = off;
                    break;
                }
            }
        }
    }

    if (field < 0) {
        // Final build-specific fallback. Validate the known metadata location
        // against Actor.Location/Rotation before accepting it.
        void* pLocation = FindObject(L"Engine.Actor.Location");
        void* pRotation = FindObject(L"Engine.Actor.Rotation");
        int locOff = -1, rotOff = -1;
        if (readCandidate(pLocation, 0x6A, locOff) && readCandidate(pRotation, 0x6A, rotOff) &&
            locOff == static_cast<int>(kActorLocation) && rotOff == static_cast<int>(kActorRotation)) {
            field = 0x6A;
            Log(L"ENV: UProperty Offset_Internal validated at +0x6A");
        }
    }

    if (field < 0) {
        Log(L"ENV: could not calibrate UProperty Offset_Internal field from SDK metadata");
        return false;
    }

    auto propertyOffset = [&](const wchar_t* name) -> int {
        void* prop = FindObject(name);
        int v = -1;
        if (!readCandidate(prop, field, v)) return -1;
        return v;
    };

    g_uPropertyOffsetField = field;
    g_rainParticleCountOffset = propertyOffset(L"Engine.RRainComponent.ParticleCount");
    g_pollenParticleCountOffset = propertyOffset(L"Engine.RPollenComponent.ParticleCount");
    g_dustParticleCountOffset = propertyOffset(L"Engine.RDustComponent.ParticleCount");
    g_snowParticleCountOffset = propertyOffset(L"Engine.RSnowComponent.ParticleCount");
    g_fogStartDistanceOffset = propertyOffset(L"Engine.ExponentialHeightFogComponent.StartDistance");
    g_fogMaxOpacityOffset = propertyOffset(L"Engine.ExponentialHeightFogComponent.FogMaxOpacity");
    g_fogHeightFalloffOffset = propertyOffset(L"Engine.ExponentialHeightFogComponent.FogHeightFalloff");
    g_fogDensityOffset = propertyOffset(L"Engine.ExponentialHeightFogComponent.FogDensity");
    g_fogHeightOffset = propertyOffset(L"Engine.ExponentialHeightFogComponent.FogHeight");
    g_enginePauseOnLossOfFocusOffset = propertyOffset(L"Engine.Engine.bPauseOnLossOfFocus");
    if (g_enginePauseOnLossOfFocusOffset != 0x5B4) {
        g_enginePauseOnLossOfFocusOffset = -1;
        Log(L"POSE FOCUS: unavailable; Engine.bPauseOnLossOfFocus layout did not match the verified build");
    } else {
        Log(L"POSE FOCUS: Engine.bPauseOnLossOfFocus reflection validated at +0x5B4");
    }

    // Resolve the UE3 BoolProperty bitmask for bRadioModeEyes.
    // Use neighboring RPawnPlayerBm bools and look for the metadata field where
    // each property has a distinct single-bit mask.
    g_radioModeEyesOffset = propertyOffset(L"BmScript.RPawnPlayerBm.bRadioModeEyes");
    g_radioModeEyesMask = 0;
    void* pInvEyes   = FindObject(L"BmScript.RPawnPlayerBm.bInvestigateModeEyes");
    void* pRadioEyes = FindObject(L"BmScript.RPawnPlayerBm.bRadioModeEyes");
    void* pSwitchX   = FindObject(L"BmScript.RPawnPlayerBm.bSwitchCharInXRay");
    void* pFingers   = FindObject(L"BmScript.RPawnPlayerBm.bFingersAttached");
    auto readU32 = [](void* prop, int off, uint32_t& out) -> bool {
        if (!prop || !IsReadableAddress(reinterpret_cast<uint8_t*>(prop) + off, 4)) return false;
        out = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(prop) + off);
        return true;
    };
    auto singleBit = [](uint32_t v) -> bool { return v && ((v & (v - 1u)) == 0); };
    for (int off = 0x40; off <= 0x180; off += 4) {
        uint32_t a=0,b=0,c=0,d=0;
        if (!readU32(pInvEyes,off,a) || !readU32(pRadioEyes,off,b) ||
            !readU32(pSwitchX,off,c) || !readU32(pFingers,off,d)) continue;
        if (singleBit(a) && singleBit(b) && singleBit(c) && singleBit(d) &&
            a != b && a != c && a != d && b != c && b != d && c != d) {
            g_radioModeEyesMask = b;
            break;
        }
    }

    // Resolve SkeletalMeshComponent.bPauseAnims through the SDK UBoolProperty
    // metadata. Validate the mask against nearby bools sharing the same byte.
    g_posePauseAnimsOffset = propertyOffset(L"Engine.SkeletalMeshComponent.bPauseAnims");
    g_posePauseAnimsMask = 0;
    void* pPauseAnims = FindObject(L"Engine.SkeletalMeshComponent.bPauseAnims");
    static constexpr const wchar_t* pauseAnimPeers[] = {
        L"Engine.SkeletalMeshComponent.bNoSkeletonUpdate",
        L"Engine.SkeletalMeshComponent.bUpdateSkelWhenNotRendered",
        L"Engine.SkeletalMeshComponent.bIgnoreControllersWhenNotRendered",
        L"Engine.SkeletalMeshComponent.bTickAnimNodesWhenNotRendered",
        L"Engine.SkeletalMeshComponent.bAnimTreeInitialised",
        L"Engine.SkeletalMeshComponent.bForceMeshObjectUpdate",
        L"Engine.SkeletalMeshComponent.bUseRawData",
        L"Engine.SkeletalMeshComponent.bDisableWarningWhenAnimNotFound"
    };
    for (int metadataOffset = 0x40; metadataOffset <= 0x180; metadataOffset += 4) {
        uint32_t pauseMask = 0;
        if (!readU32(pPauseAnims, metadataOffset, pauseMask) || !singleBit(pauseMask) || pauseMask > 0x80u)
            continue;
        int sameBytePeers = 0;
        bool masksDistinct = true;
        for (const wchar_t* peerPath : pauseAnimPeers) {
            void* peer = FindObject(peerPath);
            if (!peer || propertyOffset(peerPath) != g_posePauseAnimsOffset) continue;
            uint32_t peerMask = 0;
            if (!readU32(peer, metadataOffset, peerMask) || !singleBit(peerMask)) continue;
            if (peerMask == pauseMask) masksDistinct = false;
            ++sameBytePeers;
        }
        if (sameBytePeers >= 2 && masksDistinct) {
            g_posePauseAnimsMask = static_cast<uint8_t>(pauseMask);
            break;
        }
    }
    if (g_posePauseAnimsOffset >= 0 && g_posePauseAnimsMask) {
        wchar_t posePauseMsg[160]{};
        swprintf_s(posePauseMsg, L"POSE ANIM: bPauseAnims resolved at +0x%X mask=0x%02X",
            g_posePauseAnimsOffset, g_posePauseAnimsMask);
        Log(posePauseMsg);
    } else {
        g_posePauseAnimsOffset = -1;
        g_posePauseAnimsMask = 0;
        Log(L"POSE ANIM: bPauseAnims metadata could not be validated; animation pause remains unavailable");
    }

    wchar_t radioMsg[256]{};
    swprintf_s(radioMsg, L"RADIO EYES: bRadioModeEyes offset=+0x%X mask=0x%08X",
        g_radioModeEyesOffset, g_radioModeEyesMask);
    Log(radioMsg);

    wchar_t msg[384]{};
    swprintf_s(msg, L"ENV: UProperty field=+0x%X | Rain=+0x%X Pollen=+0x%X Dust=+0x%X Snow=+0x%X | Fog Start=+0x%X MaxOpacity=+0x%X Falloff=+0x%X Density=+0x%X Height=+0x%X",
        g_uPropertyOffsetField, g_rainParticleCountOffset, g_pollenParticleCountOffset, g_dustParticleCountOffset, g_snowParticleCountOffset,
        g_fogStartDistanceOffset, g_fogMaxOpacityOffset, g_fogHeightFalloffOffset, g_fogDensityOffset, g_fogHeightOffset);
    Log(msg);

    const bool particlesReady = g_rainParticleCountOffset >= 0 && g_pollenParticleCountOffset >= 0 && g_dustParticleCountOffset >= 0 && g_snowParticleCountOffset >= 0;
    if (!particlesReady) Log(L"ENV: SDK metadata field found, but one or more particle offsets are unresolved");
    return particlesReady;
}

bool ResolvePoseReflection() {
    if (g_uPropertyOffsetField < 0) return false;
    auto propertyOffset = [](const wchar_t* path) -> int {
        void* property = FindObject(path);
        if (!property || !IsReadableAddress(reinterpret_cast<uint8_t*>(property) + g_uPropertyOffsetField, sizeof(uint16_t)))
            return -1;
        return static_cast<int>(Read<uint16_t>(property, g_uPropertyOffsetField));
    };

    // BmSDK-AK generated layouts: Controller.Pawn @ +668, Pawn.Mesh @ +908.
    // Keep the runtime reflection check so an unfamiliar build never gets guessed offsets.
    g_controllerPawnOffset = propertyOffset(L"Engine.Controller.Pawn");
    g_pawnMeshOffset = propertyOffset(L"Engine.Pawn.Mesh");
    g_worldPawnListOffset = propertyOffset(L"Engine.WorldInfo.PawnList");
    g_pawnNextPawnOffset = propertyOffset(L"Engine.Pawn.NextPawn");
    g_actorComponentsOffset = propertyOffset(L"Engine.Actor.Components");
    g_actorAllComponentsOffset = propertyOffset(L"Engine.Actor.AllComponents");
    g_poseSpaceBasesOffset = propertyOffset(L"Engine.SkeletalMeshComponent.SpaceBases");
    g_poseLocalAtomsOffset = propertyOffset(L"Engine.SkeletalMeshComponent.LocalAtoms");
    g_poseCachedSpaceBasesOffset = propertyOffset(L"Engine.SkeletalMeshComponent.CachedSpaceBases");
    g_poseCachedLocalAtomsOffset = propertyOffset(L"Engine.SkeletalMeshComponent.CachedLocalAtoms");
    g_poseParentAnimComponentOffset = propertyOffset(L"Engine.SkeletalMeshComponent.ParentAnimComponent");
    g_poseParentAnimComponentModeOffset = propertyOffset(L"Engine.SkeletalMeshComponent.ParentAnimComponentMode");
    g_poseParentBoneMapOffset = propertyOffset(L"Engine.SkeletalMeshComponent.ParentBoneMap");
    g_poseAttachedToSkelComponentOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AttachedToSkelComponent");
    g_poseAnimTreeTemplateOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AnimTreeTemplate");
    g_poseAnimationsOffset = propertyOffset(L"Engine.SkeletalMeshComponent.Animations");
    g_poseAnimNodePoseOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AnimNodePose");
    g_poseAnimTickArrayOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AnimTickArray");
    g_poseAnimAlwaysTickArrayOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AnimAlwaysTickArray");
    g_poseAnimTickWeightsArrayOffset = propertyOffset(L"Engine.SkeletalMeshComponent.AnimTickWeightsArray");
    g_poseSkelControlInstancesOffset = propertyOffset(L"Engine.SkeletalMeshComponent.SkelControlInstances");
    g_posePostFixerUpdateBonesOffset = propertyOffset(L"Engine.SkeletalMeshComponent.PostFixerUpdateBones");
    g_classPawn = FindObject(L"Engine.Pawn");
    g_classSkeletalMeshComponent = FindObject(L"Engine.SkeletalMeshComponent");
    g_fnGetBoneNames = FindFunction(L"Engine.SkeletalMeshComponent.GetBoneNames");
    g_fnGetParentBone = FindFunction(L"Engine.SkeletalMeshComponent.GetParentBone");
    g_fnGetBoneLocation = FindFunction(L"Engine.SkeletalMeshComponent.GetBoneLocation");
    g_fnGetBoneQuaternion = FindFunction(L"Engine.SkeletalMeshComponent.GetBoneQuaternion");
    g_fnGetBoneMatrix = FindFunction(L"Engine.SkeletalMeshComponent.GetBoneMatrix");
    g_fnMatrixGetRotator = FindFunction(L"Core.Object.MatrixGetRotator");
    g_coreObjectClass = FindObject(L"Core.Object");

    const bool ok = g_controllerPawnOffset == 668 && g_pawnMeshOffset == 908 &&
        g_worldPawnListOffset == 1476 && g_pawnNextPawnOffset == 708 && g_classPawn &&
        g_classSkeletalMeshComponent && g_fnGetBoneNames && g_fnGetParentBone &&
        g_fnGetBoneLocation && g_fnGetBoneQuaternion;
    Log(ok ? L"POSE: reflection ready (Controller.Pawn, Pawn.Mesh, SkeletalMesh bone API)"
           : L"POSE: reflection unavailable or layout mismatch; inspector disabled");
    wchar_t componentOffsets[160]{};
    swprintf_s(componentOffsets, L"POSE DIAG: Actor.Components=+0x%X AllComponents=+0x%X",
        g_actorComponentsOffset, g_actorAllComponentsOffset);
    Log(componentOffsets);
    Log(g_fnGetBoneMatrix && g_fnMatrixGetRotator && g_coreObjectClass
        ? L"POSE: GetBoneMatrix rotation fallback ready"
        : L"POSE: GetBoneMatrix rotation fallback unavailable");
    wchar_t arrayOffsets[256]{};
    swprintf_s(arrayOffsets, L"POSE: buffers SpaceBases=+0x%X LocalAtoms=+0x%X CachedSpaceBases=+0x%X CachedLocalAtoms=+0x%X",
        g_poseSpaceBasesOffset, g_poseLocalAtomsOffset, g_poseCachedSpaceBasesOffset, g_poseCachedLocalAtomsOffset);
    Log(arrayOffsets);
    wchar_t relationshipOffsets[256]{};
    swprintf_s(relationshipOffsets, L"POSE: links ParentAnim=+0x%X ParentBoneMap=+0x%X AttachedSkel=+0x%X AnimTree=+0x%X Animations=+0x%X AnimNodePose=+0x%X",
        g_poseParentAnimComponentOffset, g_poseParentBoneMapOffset, g_poseAttachedToSkelComponentOffset,
        g_poseAnimTreeTemplateOffset, g_poseAnimationsOffset, g_poseAnimNodePoseOffset);
    Log(relationshipOffsets);
    return ok;
}

void* GetCoreObjectDefaultObjectForPose() {
    if (g_coreObjectCDO) return g_coreObjectCDO;
    if (!g_coreObjectClass || !g_getDefaultObject) return nullptr;
    __try { g_coreObjectCDO = g_getDefaultObject(g_coreObjectClass, 0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { g_coreObjectCDO = nullptr; }
    return g_coreObjectCDO;
}

void ProcessPoseInspectorOnGameThread() {
    if (g_poseRefreshRequested.exchange(false)) {
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            g_poseSelectedSubtreeIndices.fill(0);
            g_poseSelectedSubtreeCount = 0;
            g_poseSelectedSubtreeRoot = -1;
        }
        void* requestedPawn = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            const int oldIndex = g_poseSelectedTarget.load();
            if (oldIndex >= 0 && oldIndex < static_cast<int>(g_poseTargets.size()))
                requestedPawn = g_poseTargets[static_cast<size_t>(oldIndex)].pawn;
        }
        void* pc = GetPlayerController_BmSDKPath();
        void* playerPawn = pc && g_controllerPawnOffset >= 0 ? Read<void*>(pc, g_controllerPawnOffset) : nullptr;
        void* worldInfo = GetWorldInfo_BmSDKPath();
        std::vector<PoseTarget> targets;
        if (worldInfo && g_worldPawnListOffset >= 0 && g_pawnNextPawnOffset >= 0 &&
            g_pawnMeshOffset >= 0 && g_classPawn && g_classSkeletalMeshComponent) {
            void* pawn = Read<void*>(worldInfo, g_worldPawnListOffset);
            for (int i = 0; pawn && i < 512; ++i) {
                if (!IsReadableAddress(pawn, 0x50) || !IsA(pawn, g_classPawn)) break;
                void* next = Read<void*>(pawn, g_pawnNextPawnOffset);
                void* mesh = Read<void*>(pawn, g_pawnMeshOffset);
                if (mesh && IsA(mesh, g_classSkeletalMeshComponent))
                    targets.push_back({pawn, mesh, pawn == playerPawn});
                pawn = next;
            }
        }

        int selectedTarget = -1;
        for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].pawn == requestedPawn) { selectedTarget = static_cast<int>(i); break; }
        }
        if (selectedTarget < 0) for (size_t i = 0; i < targets.size(); ++i) {
            if (targets[i].isPlayer) { selectedTarget = static_cast<int>(i); break; }
        }
        if (selectedTarget < 0 && !targets.empty()) selectedTarget = 0;
        void* pawn = selectedTarget >= 0 ? targets[static_cast<size_t>(selectedTarget)].pawn : playerPawn;
        void* mesh = selectedTarget >= 0 ? targets[static_cast<size_t>(selectedTarget)].mesh : nullptr;
        std::wstring diagnosticStatus;
        auto meshDiagnostics = CollectPoseMeshDiagnostics(pawn, mesh, diagnosticStatus);

        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            g_poseTargets = std::move(targets);
            g_poseSelectedTarget.store(selectedTarget < 0 ? 0 : selectedTarget);
            g_poseDiagnosticPawn = pawn;
            g_poseMeshDiagnostics = std::move(meshDiagnostics);
            g_poseDiagnosticStatus = std::move(diagnosticStatus);
        }

        if (!pc || selectedTarget < 0 || !pawn || !mesh || !IsA(mesh, g_classSkeletalMeshComponent) || !g_fnGetBoneNames) {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            g_poseBones.clear();
            g_posePawn = pawn;
            g_poseMesh = mesh;
            g_poseTraceSelectedMesh.store(reinterpret_cast<uintptr_t>(mesh), std::memory_order_release);
            g_poseSpaceBasesReaderTraceLogged.store(false, std::memory_order_release);
            g_poseSelectedTransform = {};
            g_poseStatus = L"No pawn with a SkeletalMesh found. Load gameplay and refresh.";
            g_poseParentCursor.store(-1);
        } else {
            alignas(8) uint8_t params[16]{};
            struct TArrayHeader { FNameRaw* data; int32_t count; int32_t capacity; } names{};
            bool ok = ProcessScriptEvent(mesh, g_fnGetBoneNames, params);
            if (ok) memcpy(&names, params, sizeof(names));

            std::vector<PoseBoneInfo> bones;
            if (ok && names.count > 0 && names.count <= 512 &&
                IsReadableAddress(names.data, static_cast<size_t>(names.count) * sizeof(FNameRaw))) {
                bones.resize(static_cast<size_t>(names.count));
                for (size_t i = 0; i < bones.size(); ++i) {
                    bones[i].name = names.data[i];
                    bones[i].resolvedName = ResolveKnownPoseBoneName(bones[i].name);
                    bones[i].category = CategorizePoseBoneName(bones[i].resolvedName);
                }
            }
            if (bones.empty()) {
                std::lock_guard<std::mutex> lock(g_poseMutex);
                g_poseBones.clear();
                g_posePawn = pawn;
                g_poseMesh = mesh;
                g_poseTraceSelectedMesh.store(reinterpret_cast<uintptr_t>(mesh), std::memory_order_release);
                g_poseSpaceBasesReaderTraceLogged.store(false, std::memory_order_release);
                g_poseSelectedTransform = {};
                g_poseStatus = L"GetBoneNames returned no readable bones (or more than 512).";
                g_poseParentCursor.store(-1);
            } else {
                std::lock_guard<std::mutex> lock(g_poseMutex);
                g_poseBones = std::move(bones);
                g_posePawn = pawn;
                g_poseMesh = mesh;
                g_poseTraceSelectedMesh.store(reinterpret_cast<uintptr_t>(mesh), std::memory_order_release);
                g_poseSpaceBasesReaderTraceLogged.store(false, std::memory_order_release);
                g_poseSelectedTransform = {};
                g_poseSelectedBone.store(0);
                g_poseStatus = L"Reading skeleton hierarchy...";
                g_poseParentCursor.store(0);
                g_poseTransformRequested.store(true);
            }
        }
    }

    // Spread parent lookups over successive game ticks to avoid a large one-frame burst.
    int cursor = g_poseParentCursor.load();
    if (cursor >= 0 && g_fnGetParentBone) {
        std::vector<std::pair<int, FNameRaw>> batch;
        void* mesh = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            mesh = g_poseMesh;
            const int end = (std::min)(cursor + 12, static_cast<int>(g_poseBones.size()));
            for (int i = cursor; i < end; ++i) batch.emplace_back(i, g_poseBones[static_cast<size_t>(i)].name);
        }
        for (const auto& item : batch) {
            alignas(8) uint8_t params[16]{};
            memcpy(params, &item.second, sizeof(FNameRaw));
            if (!ProcessScriptEvent(mesh, g_fnGetParentBone, params)) continue;
            FNameRaw parent{};
            memcpy(&parent, params + 8, sizeof(parent));
            std::lock_guard<std::mutex> lock(g_poseMutex);
            if (item.first < static_cast<int>(g_poseBones.size())) {
                auto& bone = g_poseBones[static_cast<size_t>(item.first)];
                bone.parent = parent;
                bone.parentIndex = -1;
                for (size_t j = 0; j < g_poseBones.size(); ++j) {
                    if (g_poseBones[j].name.index == parent.index && g_poseBones[j].name.number == parent.number) {
                        bone.parentIndex = static_cast<int>(j);
                        break;
                    }
                }
            }
        }
        const int next = cursor + static_cast<int>(batch.size());
        if (next >= static_cast<int>(g_poseBones.size())) {
            g_poseParentCursor.store(-1);
            {
                std::lock_guard<std::mutex> lock(g_poseMutex);
                size_t resolvedCount = 0;
                for (const auto& bone : g_poseBones) if (bone.resolvedName) ++resolvedCount;
                wchar_t status[160]{};
                if (g_poseNameDumpState.load(std::memory_order_acquire) == 1)
                    swprintf_s(status, L"Skeleton ready: %zu bones; %zu names resolved from game NamesDump.",
                        g_poseBones.size(), resolvedCount);
                else
                    swprintf_s(status, L"Skeleton ready: %zu bones; %zu names resolved by bundled FName map.",
                        g_poseBones.size(), resolvedCount);
                g_poseStatus = status;
            }
            RebuildPoseSelectedSubtreeIndices();
        } else {
            g_poseParentCursor.store(next);
        }
    }

    if (g_poseProbeRequested.exchange(false)) {
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            const int selected = g_poseSelectedBone.load();
            ready = g_poseMesh && selected >= 0 && selected < static_cast<int>(g_poseBones.size());
            g_poseStatus = ready
                ? L"Pose probe queued; reading selected bone on the game thread."
                : L"Pose probe needs a refreshed character and selected bone.";
        }
        Log(ready ? L"POSE PROBE: launcher request accepted; running selected-bone read on EngineTick"
                  : L"POSE PROBE: request rejected; refresh the skeleton and select a bone first");
        if (ready) g_poseTransformRequested.store(true);
    }

    if (g_poseTransformRequested.exchange(false)) {
        void* mesh = nullptr;
        int selected = g_poseSelectedBone.load();
        FNameRaw boneName{};
        {
            std::lock_guard<std::mutex> lock(g_poseMutex);
            mesh = g_poseMesh;
            if (selected >= 0 && selected < static_cast<int>(g_poseBones.size()))
                boneName = g_poseBones[static_cast<size_t>(selected)].name;
            else
                mesh = nullptr;
        }
        PoseBoneTransform transform{};
        transform.selectedBone = selected;
        const wchar_t* readStatus = L"Bone transform read skipped: no mesh or bone selected.";
        bool locationOk = false;
        bool quaternionOk = false;
        bool anySpaceValid = false;
        bool matrixOk = false;
        bool matrixRotatorOk = false;
        DWORD quaternionExceptionCode = 0;
        DWORD matrixExceptionCode = 0;
        DWORD matrixRotatorExceptionCode = 0;
        if (mesh && g_fnGetBoneLocation && g_fnGetBoneQuaternion) {
            // The meaning/order of UE3's Space enum is not confirmed for this game.
            // Capture values 0..2 explicitly and label them numerically in the UI.
            for (int space = 0; space < 3; ++space) {
                alignas(8) uint8_t locationParams[24]{};
                memcpy(locationParams, &boneName, sizeof(boneName));
                memcpy(locationParams + 8, &space, sizeof(space));
                const bool thisLocationOk = ProcessScriptEvent(mesh, g_fnGetBoneLocation, locationParams);
                if (!thisLocationOk) continue;
                memcpy(&transform.locationBySpace[space], locationParams + 12, sizeof(Vec3));

                alignas(16) uint8_t quaternionParams[32]{};
                memcpy(quaternionParams, &boneName, sizeof(boneName));
                memcpy(quaternionParams + 8, &space, sizeof(space));
                DWORD thisQuaternionExceptionCode = 0;
                const bool thisQuaternionOk = ProcessScriptEvent(mesh, g_fnGetBoneQuaternion, quaternionParams,
                    space == 0 ? &quaternionExceptionCode : &thisQuaternionExceptionCode);
                if (!thisQuaternionOk) continue;
                memcpy(transform.quaternionBySpace[space], quaternionParams + 16, sizeof(transform.quaternionBySpace[space]));
                const float* q = transform.quaternionBySpace[space];
                transform.validBySpace[space] = std::isfinite(transform.locationBySpace[space].x) &&
                    std::isfinite(transform.locationBySpace[space].y) && std::isfinite(transform.locationBySpace[space].z) &&
                    std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]);
            }
            locationOk = std::isfinite(transform.locationBySpace[0].x) && std::isfinite(transform.locationBySpace[0].y) &&
                std::isfinite(transform.locationBySpace[0].z);
            quaternionOk = transform.validBySpace[0];
            anySpaceValid = transform.validBySpace[0] || transform.validBySpace[1] || transform.validBySpace[2];
            transform.location = transform.locationBySpace[0];
            memcpy(transform.quaternion, transform.quaternionBySpace[0], sizeof(transform.quaternion));
            transform.quaternionValid = quaternionOk;
            transform.valid = anySpaceValid;
            if (locationOk && !transform.valid && g_fnGetBoneMatrix && g_fnMatrixGetRotator && g_coreObjectClass && g_getDefaultObject) {
                alignas(16) uint8_t matrixParams[80]{};
                memcpy(matrixParams, &selected, sizeof(selected));
                matrixOk = ProcessScriptEvent(mesh, g_fnGetBoneMatrix, matrixParams, &matrixExceptionCode);
                if (matrixOk) {
                    alignas(16) uint8_t rotatorParams[76]{};
                    memcpy(rotatorParams, matrixParams + 16, 64);
                    if (!g_coreObjectCDO) GetCoreObjectDefaultObjectForPose();
                    if (g_coreObjectCDO && ProcessScriptEvent(g_coreObjectCDO, g_fnMatrixGetRotator, rotatorParams, &matrixRotatorExceptionCode)) {
                        memcpy(&transform.rotation, rotatorParams + 64, sizeof(transform.rotation));
                        transform.rotationValid = true;
                        transform.valid = std::isfinite(transform.location.x) &&
                            std::isfinite(transform.location.y) && std::isfinite(transform.location.z);
                    }
                    matrixRotatorOk = transform.rotationValid;
                }
            }
            auto readPoseArray = [&](int offset, size_t stride, size_t floatCount, PoseArraySnapshot& snapshot) {
                if (offset < 0 || selected < 0) return;
                struct TArrayHeader { void* data; int32_t count; int32_t capacity; } header{};
                auto* address = reinterpret_cast<uint8_t*>(mesh) + offset;
                if (!IsReadableAddress(address, sizeof(header))) return;
                memcpy(&header, address, sizeof(header));
                if (!header.data || header.count <= selected || header.count > 4096 ||
                    header.capacity < header.count || header.capacity > 4096 ||
                    !IsReadableAddress(header.data, static_cast<size_t>(header.count) * stride)) return;
                snapshot.data = reinterpret_cast<uintptr_t>(header.data);
                snapshot.count = header.count;
                snapshot.capacity = header.capacity;
                auto* element = reinterpret_cast<uint8_t*>(header.data) + static_cast<size_t>(selected) * stride;
                memcpy(snapshot.values, element, floatCount * sizeof(float));
                snapshot.valid = true;
            };
            readPoseArray(g_poseSpaceBasesOffset, 0x20, 8, transform.spaceBases);
            readPoseArray(g_poseLocalAtomsOffset, 0x20, 8, transform.localAtoms);
            readStatus = anySpaceValid ? L"Bone transforms read for one or more numeric space values."
                : !locationOk ? L"GetBoneLocation failed; see BAKTweaks.log."
                : transform.valid && transform.quaternionValid ? L"Bone transform read successfully (quaternion)."
                : transform.valid && transform.rotationValid ? L"Bone transform read via GetBoneMatrix fallback."
                : !quaternionOk ? L"GetBoneQuaternion failed; matrix fallback failed; see BAKTweaks.log."
                : L"Bone getters returned invalid values; see BAKTweaks.log.";
        }
        wchar_t trace[256]{};
        swprintf_s(trace, L"POSE READ: bone=%d FName=%d:%d mesh=0x%llX location=%d quaternion=%d(qexc=0x%08X) spaces=%d/%d/%d arrays=%d/%d matrix=%d(mexc=0x%08X) matrixRotator=%d(rexc=0x%08X) valid=%d",
            selected, boneName.index, boneName.number,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mesh)),
            locationOk ? 1 : 0, quaternionOk ? 1 : 0, quaternionExceptionCode,
            transform.validBySpace[0] ? 1 : 0, transform.validBySpace[1] ? 1 : 0, transform.validBySpace[2] ? 1 : 0,
            transform.spaceBases.valid ? 1 : 0, transform.localAtoms.valid ? 1 : 0,
            matrixOk ? 1 : 0, matrixExceptionCode, matrixRotatorOk ? 1 : 0,
            matrixRotatorExceptionCode, transform.valid ? 1 : 0);
        Log(trace);
        std::lock_guard<std::mutex> lock(g_poseMutex);
        g_poseSelectedTransform = transform;
        if (!transform.valid) g_poseStatus = readStatus;
    }
}

bool ValidateBuildCode() {
    struct Check { uintptr_t rva; const char* bytes; size_t size; };
    const Check checks[] = {
        {kStaticFindObject, "\x40\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8B\xEC", 15},
        {kProcessEvent, "\x40\x55\x57\x41\x54\x41\x55\x41\x56\x48\x83\xEC\x70", 13},
        {kSpawnActor, "\x48\x8B\xC4\x4C\x89\x40\x18\x55\x41\x54\x41\x55\x41\x56\x41\x57", 16},
        {kGetDefaultObject, "\x40\x53\x48\x83\xEC\x40\x48\x83\xB9\xA8\x01\x00\x00\x00", 14},
        {kEngineTick, "\x48\x8B\xC4\x56\x57\x41\x54\x41\x55\x41\x56\x48\x81\xEC\x90\x00\x00\x00", 18}
    };
    for (const auto& check : checks) {
        auto* address = reinterpret_cast<void*>(g_gameBase + check.rva);
        if (!IsExecutableAddress(address) || !IsReadableAddress(address, check.size) ||
            memcmp(address, check.bytes, check.size) != 0) {
            Log(L"BatLit: build function signature mismatch; native hook disabled");
            return false;
        }
    }
    return true;
}

bool ValidateLightPropertyLayout() {
    struct Check { const wchar_t* name; uint16_t offset; };
    const Check checks[] = {
        {L"Engine.Actor.Location", 0xC4}, {L"Engine.Actor.Rotation", 0xD0},
        {L"Engine.Light.LightComponent", 0x29C},
        {L"Engine.LightComponent.Brightness", 0x1F0},
        {L"Engine.LightComponent.LightColor", 0x200},
        {L"Engine.PointLightComponent.Radius", 0x268},
        {L"Engine.SpotLightComponent.InnerConeAngle", 0x2DC},
        {L"Engine.SpotLightComponent.OuterConeAngle", 0x2E0}
    };
    for (const auto& check : checks) {
        void* property = FindObject(check.name);
        if (!IsReadableAddress(reinterpret_cast<uint8_t*>(property) + 0x6A, sizeof(uint16_t)) ||
            Read<uint16_t>(property, 0x6A) != check.offset) {
            Log(check.name);
            Log(L"BatLit: reflected light property layout mismatch; native hook disabled");
            return false;
        }
    }
    return true;
}

bool Init() {
    g_gameBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"BatmanAK.exe")); if(!g_gameBase) return false;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(g_gameBase);
    auto* nt=(dos && dos->e_magic==IMAGE_DOS_SIGNATURE) ? reinterpret_cast<IMAGE_NT_HEADERS64*>(g_gameBase+dos->e_lfanew) : nullptr;
    g_buildProfile = nullptr;
    if (nt && nt->Signature == IMAGE_NT_SIGNATURE) {
        for (const auto& profile : kBuildProfiles) {
            if (nt->FileHeader.TimeDateStamp == profile.timestamp &&
                nt->OptionalHeader.SizeOfImage == profile.imageSize) {
                g_buildProfile = &profile;
                break;
            }
        }
    }
    if (!g_buildProfile) {
        Log(L"BatLit: BatmanAK.exe build mismatch; native light hook disabled for safety");
        return false;
    }
    const auto& profile = *g_buildProfile;
    kStaticFindObject = profile.findObject; kProcessEvent = profile.processEvent;
    kSpawnActor = profile.spawnActor; kGetDefaultObject = profile.defaultObject;
    kEngineTick = profile.engineTick; kGObjects = profile.objects;
    kGObjectsNumOff = profile.objectCount; kExposureLevelPPWriter = profile.exposureWriter;
    if (!ValidateBuildCode()) return false;
    wchar_t profileMessage[128]{};
    swprintf_s(profileMessage, L"BatLit: %S build selected (%08X / %08X)",
        profile.name, profile.timestamp, profile.imageSize);
    Log(profileMessage);
    g_staticFindObject=reinterpret_cast<StaticFindObjectFn>(g_gameBase+kStaticFindObject);
    g_processEvent=reinterpret_cast<ProcessEventFn>(g_gameBase+kProcessEvent);
    g_spawnActor=reinterpret_cast<SpawnActorFn>(g_gameBase+kSpawnActor);
    g_getDefaultObject=reinterpret_cast<GetDefaultObjectFn>(g_gameBase+kGetDefaultObject);
    if(!IsExecutableAddress(reinterpret_cast<void*>(g_staticFindObject))||!IsExecutableAddress(reinterpret_cast<void*>(g_processEvent))||!IsExecutableAddress(reinterpret_cast<void*>(g_spawnActor))||!IsExecutableAddress(reinterpret_cast<void*>(g_getDefaultObject))) { Log(L"BatLit: native offsets failed executable-address validation"); return false; }
    g_classFunction=FindObject(L"Core.Function"); g_classPointLightMovable=FindObject(L"Engine.PointLightMovable"); g_classSpotLightMovable=FindObject(L"Engine.SpotLightMovable"); g_classPlayerController=FindObject(L"BmGame.RPlayerController"); g_classWorldInfo=FindObject(L"Engine.WorldInfo"); g_classEngine=FindObject(L"Engine.Engine"); g_classRRainComponent=FindObject(L"Engine.RRainComponent"); g_classRPollenComponent=FindObject(L"Engine.RPollenComponent"); g_classRDustComponent=FindObject(L"Engine.RDustComponent"); g_classRSnowComponent=FindObject(L"Engine.RSnowComponent"); g_classExponentialHeightFogComponent=FindObject(L"Engine.ExponentialHeightFogComponent"); g_classRPawnPlayerBm=FindObject(L"BmScript.RPawnPlayerBm");
    g_classRLightningActor=GObjectAtFixedIndex(kGObj_RLightningActorClass); if(!g_classRLightningActor) g_classRLightningActor=FindObject(L"BmGame.RLightningActor");
    g_normalLightningLifetimeModule=GObjectAtFixedIndex(kGObj_SmallLifetimeModule);
    g_verticalLightningLifetimeModule=GObjectAtFixedIndex(kGObj_VerticalLifetimeModule);
    g_normalLightningSpawnModule=GObjectAtFixedIndex(kGObj_SmallSpawnModule);
    g_verticalLightningSpawnModule=GObjectAtFixedIndex(kGObj_VerticalSpawnModule);
    g_fnGetPlayerViewPoint=FindFunction(L"Engine.PlayerController.GetPlayerViewPoint"); g_fnWorldInfoGetWorldInfo=FindFunction(L"Engine.WorldInfo.GetWorldInfo"); g_fnEngineGetEngine=FindFunction(L"Engine.Engine.GetEngine"); g_fnFindPlayerByControllerId=FindFunction(L"Engine.GameViewportClient.FindPlayerByControllerId"); g_fnSetEnabled=FindFunction(L"Engine.LightComponent.SetEnabled"); g_fnLightColor=FindFunction(L"Engine.PointLightComponent.OnUpdatePropertyLightColor"); g_fnBrightness=FindFunction(L"Engine.PointLightComponent.OnUpdatePropertyBrightness"); g_fnUpdateColorBrightness=FindFunction(L"Engine.LightComponent.UpdateColorAndBrightness"); g_fnUpdateShafts=FindFunction(L"Engine.LightComponent.UpdateLightShaftParameters"); g_fnSetTranslation=FindFunction(L"Engine.PointLightComponent.SetTranslation"); g_fnSetAngleRadius=FindFunction(L"Engine.SpotLightComponent.SetAngleAndRadius"); g_fnSetRotation=FindFunction(L"Engine.SpotLightComponent.SetRotation"); g_fnSetAbsoluteRotation=FindFunction(L"Engine.SpotLightComponent.SetAbsoluteRotation"); g_fnActorSetLocation=FindFunction(L"Engine.Actor.SetLocation"); g_fnActorSetRotation=FindFunction(L"Engine.Actor.SetRotation"); g_fnActorDestroy=FindFunction(L"Engine.Actor.Destroy");
    g_fnIgnoreMoveInput=FindFunction(L"Engine.PlayerController.IgnoreMoveInput"); g_fnIgnoreLookInput=FindFunction(L"Engine.PlayerController.IgnoreLookInput"); g_fnIgnoreButtonInput=FindFunction(L"Engine.PlayerController.IgnoreButtonInput"); g_fnIsMoveInputIgnored=FindFunction(L"Engine.PlayerController.IsMoveInputIgnored"); g_fnIsLookInputIgnored=FindFunction(L"Engine.PlayerController.IsLookInputIgnored"); g_fnIsButtonInputIgnored=FindFunction(L"Engine.PlayerController.IsButtonInputIgnored"); g_fnResetPlayerMovementInput=FindFunction(L"Engine.PlayerController.ResetPlayerMovementInput"); g_fnFogSetEnabled=FindFunction(L"Engine.ExponentialHeightFogComponent.SetEnabled"); g_fnRadioModeEyesAndEffects=FindFunction(L"BmScript.RPawnPlayerBm.RadioModeEyesAndEffects"); g_fnConsoleCommand=FindFunction(L"Engine.PlayerController.ConsoleCommand"); if(!g_fnRadioModeEyesAndEffects) g_fnRadioModeEyesAndEffects=FindFunction(L"BmGame.RPawnPlayer.RadioModeEyesAndEffects");
    g_fnParticleActivateSystem=FindFunction(L"Engine.ParticleSystemComponent.ActivateSystem");
    g_fnParticleDeactivateSystem=FindFunction(L"Engine.ParticleSystemComponent.DeactivateSystem");
    g_fnPrimitiveSetTranslation=FindFunction(L"Engine.PrimitiveComponent.SetTranslation");
    Log((g_classRLightningActor && g_normalLightningLifetimeModule && g_verticalLightningLifetimeModule && g_normalLightningSpawnModule && g_verticalLightningSpawnModule && g_fnParticleActivateSystem && g_fnParticleDeactivateSystem && g_fnPrimitiveSetTranslation) ? L"LIGHTNING: Small/Vertical resolver ready" : L"LIGHTNING: resolver incomplete");
    const bool inputReady=g_fnIgnoreMoveInput&&g_fnIgnoreLookInput&&g_fnIgnoreButtonInput&&g_fnIsMoveInputIgnored&&g_fnIsLookInputIgnored&&g_fnIsButtonInputIgnored;
    Log(inputReady?L"INPUT UE3: IgnoreMove/Look/Button functions resolved":L"INPUT UE3: one or more ignore-input functions NOT resolved");
    const bool envReflectionReady = ResolvePropertyOffsetLayout();
    Log(envReflectionReady ? L"ENV: location-dependent particle reflection ready" : L"ENV: location-dependent particle reflection NOT ready");
    ResolvePoseReflection();
    const bool ok=g_classFunction&&g_classPointLightMovable&&g_classSpotLightMovable&&g_classPlayerController&&g_classWorldInfo&&g_classEngine&&g_fnGetPlayerViewPoint&&g_fnWorldInfoGetWorldInfo&&g_fnEngineGetEngine&&g_fnFindPlayerByControllerId&&g_fnSetEnabled&&g_fnLightColor&&g_fnBrightness&&g_fnSetTranslation&&g_fnSetAngleRadius&&g_fnSetRotation&&g_fnActorSetLocation&&g_fnActorSetRotation;
    Log(ok?L"BatLit: native UE3 resolver ready":L"BatLit: one or more UE3 objects/functions were not resolved");
    return ok && ValidateLightPropertyLayout();
}

bool HookTick() {
    if(!g_gameBase && !Init()) return false;

    void* tickAddr=reinterpret_cast<void*>(g_gameBase+kEngineTick);
    if(!IsExecutableAddress(tickAddr)) return false;

    const MH_STATUS init=MH_Initialize();
    if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED) return false;

    const MH_STATUS createTick=MH_CreateHook(tickAddr,&HookEngineTick,reinterpret_cast<void**>(&g_originalEngineTick));
    if(createTick!=MH_OK&&createTick!=MH_ERROR_ALREADY_CREATED) return false;

    const MH_STATUS enableTick=MH_EnableHook(tickAddr);
    const bool ok=(enableTick==MH_OK||enableTick==MH_ERROR_ENABLED);
    if (ok) Log(L"BatLit SAFE: EngineTick dispatcher enabled (no ProcessInternal re-entry)");
    return ok;
}


// Rain follows the validated CE table Batman_Arkham_Knight_Rain_BmSDK_v0.5_WRITE_ENGINE_ONLY.
// IMPORTANT: only the ENGINE copy is writable. Console is deliberately ignored.
// Engine AOB: 0x0D000000..0x10000000; RainEngineBase = hit - 0x210;
// ParticleCount = RainEngineBase + 0x21C = hit + 0x0C.


} // namespace BatLitNative


struct CapeDefaults {
    uint32_t flags{};
    float linearForceLimit{}, linearStrength{}, linearDamping{};
    float angularForceLimit{}, angularStrength{}, angularDamping{};
    float driveTolerance{}, minRotVel{};
    float forceStrengthMultiplier{}, maxForceCap{};
    float chainStrengthMin{}, chainStrengthOverLength{};
    float overstretchDistance{}, minWindSpeed{}, maxWindSpeed{}, upwardsWindScale{};
    float windX{}, windY{}, windZ{};
    float springBlendSpeed{}, runStartLength{}, runStartForce{}, runIncreaseRate{};
    bool valid = false;
};
struct RainDefaults { int particleCount{}; bool valid = false; };
struct EnvParticleRef {
    void* object{};
    int originalCount{};
};
struct FogRef {
    void* object{};
    float startDistance{}, maxOpacity{}, heightFalloff{}, density{}, height{};
};
CapeDefaults g_capeDefaults;
RainDefaults g_rainDefaults;
std::mutex g_envParticleMutex;
std::vector<EnvParticleRef> g_rainRefs;
std::vector<EnvParticleRef> g_pollenRefs;
std::vector<EnvParticleRef> g_dustRefs;
std::vector<EnvParticleRef> g_snowRefs;
std::vector<FogRef> g_fogRefs;
std::atomic<int> g_rainLiveCount{0};
std::atomic<int> g_pollenLiveCount{0};
std::atomic<int> g_dustLiveCount{0};
std::atomic<int> g_snowLiveCount{0};
std::atomic<int> g_fogLiveCount{0};
std::atomic<bool> g_envParticleResolverReady{false};
std::atomic<bool> g_weatherParticleResolverReady{false};
std::atomic<bool> g_snowResolverReady{false};

int PreserveOriginalCount(void* obj, int current, const std::vector<EnvParticleRef>& oldRefs) {
    for (const auto& r : oldRefs) if (r.object == obj) return r.originalCount;
    return current;
}


void RefreshWeatherParticleComponentsFast() {
    using namespace BatLitNative;
    if (!g_gameBase || !g_classRRainComponent || !g_classRPollenComponent || !g_classRDustComponent || !g_classRSnowComponent) {
        g_weatherParticleResolverReady.store(false);
        g_snowResolverReady.store(false);
        return;
    }

    auto resolveOffset = [&](const wchar_t* propName, int& outOffset, const wchar_t* label) {
        if (outOffset >= 0) return;
        void* prop = FindObject(propName);
        if (!prop) return;
        uint16_t off = 0;
        if (!SafeReadU16Raw(prop, 0x6A, off)) return;
        if (off > 0 && off < 0x4000) {
            outOffset = static_cast<int>(off);
            wchar_t m[192]{};
            swprintf_s(m, L"ENV %s: ParticleCount offset resolved directly at +0x%X", label, outOffset);
            Log(m);
        }
    };

    resolveOffset(L"Engine.RRainComponent.ParticleCount",   g_rainParticleCountOffset,   L"Rain");
    resolveOffset(L"Engine.RPollenComponent.ParticleCount", g_pollenParticleCountOffset, L"Pollen");
    resolveOffset(L"Engine.RDustComponent.ParticleCount",   g_dustParticleCountOffset,   L"Dust");
    resolveOffset(L"Engine.RSnowComponent.ParticleCount",   g_snowParticleCountOffset,   L"Snow");

    if (g_rainParticleCountOffset < 0 || g_pollenParticleCountOffset < 0 ||
        g_dustParticleCountOffset < 0 || g_snowParticleCountOffset < 0) {
        g_weatherParticleResolverReady.store(false);
        g_snowResolverReady.store(false);
        return;
    }

    std::vector<EnvParticleRef> oldRain, oldPollen, oldDust, oldSnow;
    {
        std::lock_guard<std::mutex> lock(g_envParticleMutex);
        oldRain = g_rainRefs;
        oldPollen = g_pollenRefs;
        oldDust = g_dustRefs;
        oldSnow = g_snowRefs;
    }

    std::vector<EnvParticleRef> rain, pollen, dust, snow;
    auto** objects = reinterpret_cast<void**>(g_gameBase + kGObjects);
    int n = 0;
    SafeReadIntRaw(reinterpret_cast<void*>(g_gameBase), kGObjectsNumOff, n);
    if (n <= 0 || n > kGObjectsCapacity) n = kGObjectsCapacity;

    for (int i = 0; i < n; ++i) {
        void* obj = SafeGObjectAt(objects, i);
        if (!obj) continue;
        void* cls = SafeReadPtrRaw(obj, kUObjectClass);
        if (!cls) continue;

        int off = -1;
        std::vector<EnvParticleRef>* out = nullptr;
        const std::vector<EnvParticleRef>* old = nullptr;
        if (cls == g_classRRainComponent)        { off = g_rainParticleCountOffset;   out = &rain;   old = &oldRain; }
        else if (cls == g_classRPollenComponent) { off = g_pollenParticleCountOffset; out = &pollen; old = &oldPollen; }
        else if (cls == g_classRDustComponent)   { off = g_dustParticleCountOffset;   out = &dust;   old = &oldDust; }
        else if (cls == g_classRSnowComponent)   { off = g_snowParticleCountOffset;   out = &snow;   old = &oldSnow; }
        else continue;

        uint32_t flags = 0;
        if (!SafeReadU32Raw(obj, kUObjectFlags, flags)) continue;
        if (flags & kRF_ClassDefaultObject) continue;
        int v = 0;
        if (!SafeReadIntRaw(obj, static_cast<size_t>(off), v)) continue;
        if (v < 0 || v > 5000000) continue;
        out->push_back({obj, PreserveOriginalCount(obj, v, *old)});
    }

    {
        std::lock_guard<std::mutex> lock(g_envParticleMutex);
        g_rainRefs.swap(rain);
        g_pollenRefs.swap(pollen);
        g_dustRefs.swap(dust);
        g_snowRefs.swap(snow);
        g_rainLiveCount.store(static_cast<int>(g_rainRefs.size()));
        g_pollenLiveCount.store(static_cast<int>(g_pollenRefs.size()));
        g_dustLiveCount.store(static_cast<int>(g_dustRefs.size()));
        g_snowLiveCount.store(static_cast<int>(g_snowRefs.size()));

        auto logFirst = [&](const wchar_t* label, const std::vector<EnvParticleRef>& refs, int off) {
            if (refs.empty()) return;
            auto* pCount = reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(refs.front().object) + off);
            if (!IsReadableAddress(pCount, sizeof(int))) return;
            wchar_t m[192]{};
            swprintf_s(m, L"ENV %s: live component=%p ParticleCount=%d offset=+0x%X", label, refs.front().object, *pCount, off);
            Log(m);
        };
        logFirst(L"Rain", g_rainRefs, g_rainParticleCountOffset);
        logFirst(L"Pollen", g_pollenRefs, g_pollenParticleCountOffset);
        logFirst(L"Dust", g_dustRefs, g_dustParticleCountOffset);
        logFirst(L"Snow", g_snowRefs, g_snowParticleCountOffset);
    }

    g_weatherParticleResolverReady.store(true);
    g_snowResolverReady.store(true);
}

void RefreshLocationDependentParticles() {
    using namespace BatLitNative;
    if (!g_gameBase) {
        g_envParticleResolverReady.store(false);
        return;
    }

    std::vector<FogRef> oldFog;
    {
        std::lock_guard<std::mutex> lock(g_envParticleMutex);
        oldFog = g_fogRefs;
    }
    std::vector<FogRef> fog;
    auto** objects = reinterpret_cast<void**>(g_gameBase + kGObjects);
    const int n = GObjectCount();
    for (int i = 0; i < n; ++i) {
        void* obj = SafeGObjectAt(objects, i);
        if (!obj || !IsReadableAddress(obj, 0x4C)) continue;
        const uint32_t flags = Read<uint32_t>(obj, kUObjectFlags);
        if (flags & kRF_ClassDefaultObject) continue;

        if (g_classExponentialHeightFogComponent && IsA(obj, g_classExponentialHeightFogComponent) &&
            g_fogStartDistanceOffset >= 0 && g_fogMaxOpacityOffset >= 0 && g_fogHeightFalloffOffset >= 0 &&
            g_fogDensityOffset >= 0 && g_fogHeightOffset >= 0) {
            auto rf = [&](int off, float& v)->bool { auto* p = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(obj)+off); if(!IsReadableAddress(p,4)) return false; v=*p; return std::isfinite(v); };
            FogRef fr{}; fr.object=obj;
            if (rf(g_fogStartDistanceOffset, fr.startDistance) && rf(g_fogMaxOpacityOffset, fr.maxOpacity) &&
                rf(g_fogHeightFalloffOffset, fr.heightFalloff) && rf(g_fogDensityOffset, fr.density) && rf(g_fogHeightOffset, fr.height)) {
                for (const auto& o : oldFog) if (o.object == obj) { fr = o; fr.object = obj; break; }
                fog.push_back(fr);
            }
            continue;
        }

        continue;
    }

    {
        std::lock_guard<std::mutex> lock(g_envParticleMutex);
        g_fogRefs.swap(fog);
        g_fogLiveCount.store(static_cast<int>(g_fogRefs.size()));
    }
    g_envParticleResolverReady.store(true);
}

void SetLocationParticleAmount(const wchar_t* label, std::vector<EnvParticleRef>& refs, int propertyOffset, int value) {
    if (value < 0) value = 0;
    int changed = 0;
    std::lock_guard<std::mutex> lock(g_envParticleMutex);
    for (auto& r : refs) {
        auto* pCount = reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(r.object) + propertyOffset);
        if (!BatLitNative::IsReadableAddress(pCount, sizeof(int))) continue;
        *reinterpret_cast<volatile int*>(pCount) = value;
        ++changed;
    }
    wchar_t msg[192]{};
    swprintf_s(msg, L"ENV %s: ParticleCount=%d applied to %d live component(s)", label, value, changed);
    Log(msg);
}

void ResetLocationParticleAmount(const wchar_t* label, std::vector<EnvParticleRef>& refs, int propertyOffset) {
    int changed = 0;
    std::lock_guard<std::mutex> lock(g_envParticleMutex);
    for (auto& r : refs) {
        auto* pCount = reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(r.object) + propertyOffset);
        if (!BatLitNative::IsReadableAddress(pCount, sizeof(int))) continue;
        *reinterpret_cast<volatile int*>(pCount) = r.originalCount;
        ++changed;
    }
    wchar_t msg[192]{};
    swprintf_s(msg, L"ENV %s: session/location defaults restored on %d live component(s)", label, changed);
    Log(msg);
}


bool IsReadable(DWORD protect) {
    if (protect & PAGE_GUARD) return false;
    const DWORD p = protect & 0xff;
    return p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
           p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

bool MatchPattern(const uint8_t* p, const uint8_t* pattern, const char* mask, size_t len) {
    for (size_t i = 0; i < len; ++i) if (mask[i] == 'x' && p[i] != pattern[i]) return false;
    return true;
}

uint8_t* ScanRegion(uintptr_t start, uintptr_t end, const uint8_t* pattern, const char* mask, size_t len, bool validateParticle = false) {
    MEMORY_BASIC_INFORMATION mbi{};
    for (uintptr_t cur = start; cur < end;) {
        if (!VirtualQuery(reinterpret_cast<void*>(cur), &mbi, sizeof(mbi))) { cur += 0x1000; continue; }
        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t next = base + mbi.RegionSize;
        if (next <= cur) break;
        if (mbi.State == MEM_COMMIT && IsReadable(mbi.Protect)) {
            uintptr_t a = (std::max)(base, start);
            uintptr_t z = (std::min)(next, end);
            if (z >= a + len) {
                for (uintptr_t x = a; x + len <= z; ++x) {
                    auto* p = reinterpret_cast<uint8_t*>(x);
                    if (!MatchPattern(p, pattern, mask, len)) continue;
                    if (validateParticle) {
                        const int v = *reinterpret_cast<volatile int*>(p + 0x0C);
                        if (v < 100 || v > 5000000) continue;
                    }
                    return p;
                }
            }
        }
        cur = next;
    }
    return nullptr;
}

uint8_t* ScanModule(HMODULE mod, const uint8_t* pattern, const char* mask, size_t len) {
    if (!mod) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return ScanRegion(reinterpret_cast<uintptr_t>(base), reinterpret_cast<uintptr_t>(base) + nt->OptionalHeader.SizeOfImage, pattern, mask, len);
}

float ReadCape(uint32_t off) {
    const uintptr_t b = g_capeBase.load();
    return b ? *reinterpret_cast<volatile float*>(b + off) : 0.0f;
}
void WriteCape(uint32_t off, float v) {
    const uintptr_t b = g_capeBase.load();
    if (b) *reinterpret_cast<volatile float*>(b + off) = v;
}

uint32_t ReadCapeFlags() {
    const uintptr_t b = g_capeBase.load();
    return b ? *reinterpret_cast<volatile uint32_t*>(b + 0x6C) : 0;
}
void WriteCapeFlags(uint32_t v) {
    const uintptr_t b = g_capeBase.load();
    if (b) *reinterpret_cast<volatile uint32_t*>(b + 0x6C) = v;
}
void EnableCapeWindOverride() {
    uint32_t flags = ReadCapeFlags();
    flags |= 0x00001000u; // bEnableWindEffect
    flags |= 0x00002000u; // bOverrideWindDirection
    WriteCapeFlags(flags);
}

void CaptureCapeDefaults() {
    if (g_capeDefaults.valid || !g_capeBase.load()) return;
    g_capeDefaults.flags = ReadCapeFlags();
    g_capeDefaults.linearForceLimit = ReadCape(0xB0);
    g_capeDefaults.linearStrength = ReadCape(0xB4);
    g_capeDefaults.linearDamping = ReadCape(0xB8);
    g_capeDefaults.angularForceLimit = ReadCape(0xBC);
    g_capeDefaults.angularStrength = ReadCape(0xC0);
    g_capeDefaults.angularDamping = ReadCape(0xC4);
    g_capeDefaults.driveTolerance = ReadCape(0xC8);
    g_capeDefaults.minRotVel = ReadCape(0xCC);
    g_capeDefaults.forceStrengthMultiplier = ReadCape(0xD0);
    g_capeDefaults.maxForceCap = ReadCape(0xD4);
    g_capeDefaults.chainStrengthMin = ReadCape(0xD8);
    g_capeDefaults.chainStrengthOverLength = ReadCape(0xDC);
    g_capeDefaults.overstretchDistance = ReadCape(0xE0);
    g_capeDefaults.minWindSpeed = ReadCape(0xE4);
    g_capeDefaults.maxWindSpeed = ReadCape(0xE8);
    g_capeDefaults.upwardsWindScale = ReadCape(0xEC);
    g_capeDefaults.windX = ReadCape(0xF0);
    g_capeDefaults.windY = ReadCape(0xF4);
    g_capeDefaults.windZ = ReadCape(0xF8);
    g_capeDefaults.springBlendSpeed = ReadCape(0xFC);
    g_capeDefaults.runStartLength = ReadCape(0x100);
    g_capeDefaults.runStartForce = ReadCape(0x104);
    g_capeDefaults.runIncreaseRate = ReadCape(0x108);
    g_capeDefaults.valid = true;
}

void CaptureRainDefaults() {
    if (g_rainDefaults.valid || !g_rainEngine.load()) return;
    const int v = *reinterpret_cast<volatile int*>(g_rainEngine.load());
    if (v > 0 && v < 5000000) { g_rainDefaults.particleCount = v; g_rainDefaults.valid = true; }
}

void ResetCape() {
    if (!g_capeDefaults.valid || !g_capeBase.load()) return;
    WriteCapeFlags(g_capeDefaults.flags);
    WriteCape(0xB0, g_capeDefaults.linearForceLimit);
    WriteCape(0xB4, g_capeDefaults.linearStrength);
    WriteCape(0xB8, g_capeDefaults.linearDamping);
    WriteCape(0xBC, g_capeDefaults.angularForceLimit);
    WriteCape(0xC0, g_capeDefaults.angularStrength);
    WriteCape(0xC4, g_capeDefaults.angularDamping);
    WriteCape(0xC8, g_capeDefaults.driveTolerance);
    WriteCape(0xCC, g_capeDefaults.minRotVel);
    WriteCape(0xD0, g_capeDefaults.forceStrengthMultiplier);
    WriteCape(0xD4, g_capeDefaults.maxForceCap);
    WriteCape(0xD8, g_capeDefaults.chainStrengthMin);
    WriteCape(0xDC, g_capeDefaults.chainStrengthOverLength);
    WriteCape(0xE0, g_capeDefaults.overstretchDistance);
    WriteCape(0xE4, g_capeDefaults.minWindSpeed);
    WriteCape(0xE8, g_capeDefaults.maxWindSpeed);
    WriteCape(0xEC, g_capeDefaults.upwardsWindScale);
    WriteCape(0xF0, g_capeDefaults.windX);
    WriteCape(0xF4, g_capeDefaults.windY);
    WriteCape(0xF8, g_capeDefaults.windZ);
    WriteCape(0xFC, g_capeDefaults.springBlendSpeed);
    WriteCape(0x100, g_capeDefaults.runStartLength);
    WriteCape(0x104, g_capeDefaults.runStartForce);
    WriteCape(0x108, g_capeDefaults.runIncreaseRate);
}

void SetRain(int v) {
    if (v < 100) v = 100;
    const uintptr_t engine = g_rainEngine.load();
    if (!engine) {
        Log(L"RAIN: ENGINE AOB address not ready");
        return;
    }
    if (!BatLitNative::IsReadableAddress(reinterpret_cast<void*>(engine), sizeof(int))) {
        Log(L"RAIN: ENGINE ParticleCount address unreadable");
        return;
    }

    *reinterpret_cast<volatile int*>(engine) = v;

    wchar_t msg[192]{};
    swprintf_s(msg, L"RAIN: ParticleCount=%d written ENGINE ONLY @ 0x%llX (F5 to rebuild rain)",
        v, (unsigned long long)engine);
    Log(msg);
}

void ResetRain() { if (g_rainDefaults.valid) SetRain(g_rainDefaults.particleCount); }
void ResetGlobal() {
    ResetCape();
    ResetRain();
    BatLitNative::g_poseKeepRunningUnfocused.store(true);
}

bool InstallCapeResolverHook() {
    HMODULE exe = GetModuleHandleW(L"BatmanAK.exe");
    const uint8_t pattern[] = {0xF3,0x0F,0x10,0x83,0xE8,0x00,0x00,0x00,0xF3,0x0F,0x10,0x8B,0xE4,0x00,0x00,0x00,0x44,0x0F,0x2F,0xE9};
    const char mask[] = "xxxxxxxxxxxxxxxxxxxx";
    auto* site = ScanModule(exe, pattern, mask, sizeof(pattern));
    if (!site) return false;

    // Capture RBX, execute the original 16 bytes, return to site+16.
    auto* stub = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stub) return false;
    size_t i = 0;
    stub[i++] = 0x48; stub[i++] = 0xB8; // mov rax, &g_capeBase
    const uint64_t addr = reinterpret_cast<uint64_t>(&g_capeBase);
    memcpy(stub + i, &addr, 8); i += 8;
    stub[i++] = 0x48; stub[i++] = 0x89; stub[i++] = 0x18; // mov [rax],rbx
    memcpy(stub + i, site, 16); i += 16;
    stub[i++] = 0x48; stub[i++] = 0xB8;
    const uint64_t ret = reinterpret_cast<uint64_t>(site + 16);
    memcpy(stub + i, &ret, 8); i += 8;
    stub[i++] = 0xFF; stub[i++] = 0xE0;

    DWORD old{};
    if (!VirtualProtect(site, 16, PAGE_EXECUTE_READWRITE, &old)) return false;
    uint8_t patch[16]{}; size_t p = 0;
    patch[p++] = 0x48; patch[p++] = 0xB8;
    const uint64_t dst = reinterpret_cast<uint64_t>(stub);
    memcpy(patch + p, &dst, 8); p += 8;
    patch[p++] = 0xFF; patch[p++] = 0xE0;
    while (p < sizeof(patch)) patch[p++] = 0x90;
    memcpy(site, patch, sizeof(patch));
    VirtualProtect(site, 16, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 16);
    return true;
}

void ResolveRain() {
    // Exact validated CE v0.5 WRITE_ENGINE_ONLY method:
    //   Engine data : 0x0D000000..0x10000000
    //   AOB         : FF FF 00 00 00 00 00 00 00 00 00 00 ?? ?? ?? ?? 00 00 80 44
    //   RainEngineBase      = match - 0x210
    //   Engine ParticleCount = RainEngineBase + 0x21C = match + 0x0C
    static const uint8_t pattern[] = {
        0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,
        0x00,0x00,0x80,0x44
    };
    static const char mask[] = "xxxxxxxxxxxx????xxxx";

    uintptr_t engine = g_rainEngine.load();
    if (!engine) {
        if (auto* hit = ScanRegion(0x0D000000ull, 0x10000000ull, pattern, mask, sizeof(pattern), false)) {
            const uintptr_t engineBase = reinterpret_cast<uintptr_t>(hit) - 0x210;
            engine = engineBase + 0x21C;
            g_rainEngine.store(engine);

            wchar_t msg[256]{};
            swprintf_s(msg, L"RAIN ENGINE ONLY: Base=0x%llX ParticleCount=0x%llX value=%d",
                (unsigned long long)engineBase, (unsigned long long)engine,
                *reinterpret_cast<volatile int*>(engine));
            Log(msg);
        }
    }
}


bool LoadBackgroundFromResource(ID3D11Device* device, ID3D11ShaderResourceView** outSrv) {
    *outSrv = nullptr;
    HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_BAT_BACKGROUND), MAKEINTRESOURCEW(10));
    if (!res) return false;
    HGLOBAL data = LoadResource(g_module, res);
    if (!data) return false;
    void* bytes = LockResource(data);
    DWORD size = SizeofResource(g_module, res);
    if (!bytes || !size) return false;

    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* tex = nullptr;

    auto releaseAll = [&]() {
        if (tex) tex->Release();
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
    };

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { releaseAll(); return false; }
    if (FAILED(factory->CreateStream(&stream))) { releaseAll(); return false; }
    if (FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(bytes), size))) { releaseAll(); return false; }
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) { releaseAll(); return false; }
    if (FAILED(decoder->GetFrame(0, &frame))) { releaseAll(); return false; }
    if (FAILED(factory->CreateFormatConverter(&converter))) { releaseAll(); return false; }
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) { releaseAll(); return false; }

    UINT w = 0, h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || !w || !h) { releaseAll(); return false; }
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data()))) { releaseAll(); return false; }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = pixels.data(); sd.SysMemPitch = w * 4;
    if (FAILED(device->CreateTexture2D(&td, &sd, &tex))) { releaseAll(); return false; }
    hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
    releaseAll();
    return SUCCEEDED(hr);
}
void MaintainUiInputCapture();

void CreateRenderTarget(IDXGISwapChain* swap) {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        backBuffer->Release();
    }
}

void RebindGameWindowAfterResize(IDXGISwapChain* swap) {
    if (!g_imguiReady || !swap) return;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap->GetDesc(&desc)) || !desc.OutputWindow) return;
    HWND newHwnd = desc.OutputWindow;

    // Restore the old window proc before binding ImGui to a recreated/replaced HWND.
    if (g_hwnd && g_originalWndProc && IsWindow(g_hwnd)) {
        const auto current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC));
        if (current == HookWndProc)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
    }

    ImGui_ImplWin32_Shutdown();

    g_hwnd = newHwnd;
    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc))
    );
    ImGui_ImplWin32_Init(g_hwnd);

    // Force the next open-frame to capture the new resolution's cursor state,
    // never restore the pre-resize ClipCursor rectangle.
    g_savedClipValid = false;
    g_inputCaptureActive = false;
    MaintainUiInputCapture();
    Log(L"ImGui Win32 binding refreshed after ResizeBuffers");
}

void BeginUiInputCapture() {
    if (g_inputCaptureActive) return;
    g_savedClipValid = GetClipCursor(&g_savedClipRect) != FALSE;
    POINT pt{};
    if (g_originalGetCursorPos ? g_originalGetCursorPos(&pt) : GetCursorPos(&pt)) {
        g_gameFrozenCursor = pt;
        g_gameFrozenCursorValid = true;
    } else {
        g_gameFrozenCursorValid = false;
    }
    g_inputCaptureActive = true;
}

void MaintainUiInputCapture() {
    if (!g_imguiReady) return;

    ImGuiIO& io = ImGui::GetIO();
    const bool open = g_uiOpen.load();
    io.MouseDrawCursor = open; // software cursor: survives resolution/fullscreen cursor hiding

    if (open) {
        BeginUiInputCapture();
        ReleaseCapture();
        if (g_originalClipCursor) g_originalClipCursor(nullptr); else ClipCursor(nullptr);
        // The game may hide/re-own the OS cursor after a mode/resolution switch.
        // ImGui draws its own cursor, but setting a normal cursor here also keeps
        // Win32 cursor state sane for the current game HWND.
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
    } else if (g_inputCaptureActive) {
        if (g_savedClipValid) { if (g_originalClipCursor) g_originalClipCursor(&g_savedClipRect); else ClipCursor(&g_savedClipRect); }
        g_inputCaptureActive = false;
        g_savedClipValid = false;
        g_gameFrozenCursorValid = false;
    }
}

bool IsBlockedGameInputMessage(UINT msg) {
    switch (msg) {
        // Raw input (important for Arkham Knight mouse/keyboard handling)
        case WM_INPUT:
        case WM_INPUT_DEVICE_CHANGE:

        // Mouse
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:

        // Keyboard
        case WM_KEYDOWN: case WM_KEYUP:
        case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        case WM_CHAR: case WM_SYSCHAR:
            return true;
        default:
            return false;
    }
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE || msg == WM_DISPLAYCHANGE || msg == WM_DPICHANGED) {
        // Any stale clip rectangle/capture from the previous resolution must die.
        g_savedClipValid = false;
        g_inputCaptureActive = false;
        if (g_uiOpen.load()) {
            ReleaseCapture();
            ClipCursor(nullptr);
        }
    }

    if (g_uiOpen.load()) {
        // Let ImGui consume/update its own state first.
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);

        // Never let Batman receive keyboard, mouse or RAWINPUT while the overlay is open.
        if (IsBlockedGameInputMessage(msg)) return 1;

        // Keep a usable cursor even when the game tries to own/hide it.
        if (msg == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
            return TRUE;
        }
    }

    return CallWindowProcW(g_originalWndProc, hwnd, msg, wp, lp);
}

void DrawBackground() {
    if (!g_backgroundSrv) return;
    ImVec2 a = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImVec2 b(a.x + sz.x, a.y + sz.y);
    ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(g_backgroundSrv), a, b, ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,105));
}

void DrawCapeTab() {
    const uintptr_t base = g_capeBase.load();
    if (!base) {
        ImGui::TextColored(ImVec4(1,0.55f,0.25f,1), "Cape resolver: waiting for active RCapeStateConfig...");
        return;
    }

    ImGui::TextDisabled("RCapeStateConfig: 0x%llX", static_cast<unsigned long long>(base));
    ImGui::Spacing();

    ImGui::TextUnformatted("Wind Direction");
    ImGui::Separator();
    ImGui::TextDisabled("Local-space direction. Editing X / Y / Z enables the cape wind override.");

    float windX = ReadCape(0xF0);
    float windY = ReadCape(0xF4);
    float windZ = ReadCape(0xF8);

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Wind Direction X", &windX, 0.25f, -100.0f, 100.0f, "%.3f")) { EnableCapeWindOverride(); WriteCape(0xF0, windX); }
    HelpMarker("Local-space X component of the cape wind-direction override.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Wind Direction Y", &windY, 0.25f, -100.0f, 100.0f, "%.3f")) { EnableCapeWindOverride(); WriteCape(0xF4, windY); }
    HelpMarker("Local-space Y component of the cape wind-direction override.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Wind Direction Z", &windZ, 0.25f, -100.0f, 100.0f, "%.3f")) { EnableCapeWindOverride(); WriteCape(0xF8, windZ); }
    HelpMarker("Local-space Z component of the cape wind-direction override.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Wind Speed");
    ImGui::Separator();

    float minWind = ReadCape(0xE4);
    float maxWind = ReadCape(0xE8);
    float upwardWind = ReadCape(0xEC);

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Minimum Wind Speed", &minWind, 1.0f, -5000.0f, 5000.0f, "%.1f")) WriteCape(0xE4, minWind);
    HelpMarker("Lower bound of the wind speed used by the cape simulation.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Maximum Wind Speed", &maxWind, 1.0f, -5000.0f, 5000.0f, "%.1f")) WriteCape(0xE8, maxWind);
    HelpMarker("Upper bound of the wind speed used by the cape simulation.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Upward Wind Scale", &upwardWind, 0.01f, -100.0f, 100.0f, "%.3f")) WriteCape(0xEC, upwardWind);
    HelpMarker("Scales the upward component of the cape wind response.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Force");
    ImGui::Separator();

    float forceStrength = ReadCape(0xD0);
    float maxForce = ReadCape(0xD4);

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Force Strength", &forceStrength, 0.01f, -100.0f, 100.0f, "%.3f")) WriteCape(0xD0, forceStrength);
    HelpMarker("Global multiplier applied to cape forces.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Maximum Force", &maxForce, 0.1f, -10000.0f, 10000.0f, "%.2f")) WriteCape(0xD4, maxForce);
    HelpMarker("Caps the maximum force applied to the cape.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Cape Response");
    ImGui::Separator();

    float chainMin = ReadCape(0xD8);
    float chainOverLength = ReadCape(0xDC);
    float overstretch = ReadCape(0xE0);

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Minimum Chain Force", &chainMin, 0.01f, -100.0f, 100.0f, "%.3f")) WriteCape(0xD8, chainMin);
    HelpMarker("Minimum positional-force multiplier used along the cape chain.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Chain Force Along Cape", &chainOverLength, 0.01f, -100.0f, 100.0f, "%.3f")) WriteCape(0xDC, chainOverLength);
    HelpMarker("Controls how positional force changes over the length of the cape.");

    ImGui::SetNextItemWidth(220.0f * g_uiScale);
    if (ImGui::DragFloat("Overstretch Distance", &overstretch, 0.1f, -10000.0f, 10000.0f, "%.2f")) WriteCape(0xE0, overstretch);
    HelpMarker("Overrides the distance threshold used by overstretch handling.");

    ImGui::Spacing();
    if (ImGui::Button("Reset Cape")) ResetCape();
    ImGui::SameLine();
    ImGui::TextDisabled(g_capeDefaults.valid ? "startup/session values captured" : "waiting for startup values");
}


void SetFogValues(float startDistance, float maxOpacity, float heightFalloff, float density, float height) {
    using namespace BatLitNative; int changed = 0; std::lock_guard<std::mutex> lock(g_envParticleMutex);
    for (auto& r : g_fogRefs) { auto wf=[&](int off,float v){ auto* p=reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(r.object)+off); if(IsReadableAddress(p,4)) *reinterpret_cast<volatile float*>(p)=v; }; wf(g_fogStartDistanceOffset,startDistance); wf(g_fogMaxOpacityOffset,maxOpacity); wf(g_fogHeightFalloffOffset,heightFalloff); wf(g_fogDensityOffset,density); wf(g_fogHeightOffset,height); ++changed; }
    wchar_t msg[192]{}; swprintf_s(msg,L"ENV Fog: values applied to %d live component(s)",changed); Log(msg);
}

void ResetFogValues() {
    using namespace BatLitNative; int changed=0; std::lock_guard<std::mutex> lock(g_envParticleMutex);
    for (auto& r : g_fogRefs) { auto wf=[&](int off,float v){ auto* p=reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(r.object)+off); if(IsReadableAddress(p,4)) *reinterpret_cast<volatile float*>(p)=v; }; wf(g_fogStartDistanceOffset,r.startDistance); wf(g_fogMaxOpacityOffset,r.maxOpacity); wf(g_fogHeightFalloffOffset,r.heightFalloff); wf(g_fogDensityOffset,r.density); wf(g_fogHeightOffset,r.height); ++changed; }
    wchar_t msg[192]{}; swprintf_s(msg,L"ENV Fog: location defaults restored on %d live component(s)",changed); Log(msg);
}

bool SetFogEnabledImmediate(bool enabled) {
    using namespace BatLitNative; int changed=0; std::lock_guard<std::mutex> lock(g_envParticleMutex); if (!g_fnFogSetEnabled) return false; struct PEnable { int32_t b{}; } pe{enabled?1:0};
    for (auto& r : g_fogRefs) if (r.object && IsReadableAddress(r.object,0x80)) { ProcessScriptEvent(r.object,g_fnFogSetEnabled,&pe); ++changed; } return changed>0;
}


void QueueRadioModeEyes(bool enable) {
    BatLitNative::g_radioEyesRequest.store(enable ? 1 : 0);
}

void DrawEnvironmentTab() {
    if (ImGui::CollapsingHeader("Weather", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int rainAmount = 0, pollenAmount = 0, dustAmount = 0, snowAmount = 0;
        static bool seededRain = false, seededPollen = false, seededDust = false, seededSnow = false;
        {
            std::lock_guard<std::mutex> lock(g_envParticleMutex);
            auto seed = [&](bool& seeded, int& amount, const std::vector<EnvParticleRef>& refs, int off) {
                if (seeded || refs.empty() || off < 0) return;
                auto* pCount = reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(refs.front().object) + off);
                if (BatLitNative::IsReadableAddress(pCount, 4)) { amount = *pCount; seeded = true; }
            };
            seed(seededRain, rainAmount, g_rainRefs, BatLitNative::g_rainParticleCountOffset);
            seed(seededPollen, pollenAmount, g_pollenRefs, BatLitNative::g_pollenParticleCountOffset);
            seed(seededDust, dustAmount, g_dustRefs, BatLitNative::g_dustParticleCountOffset);
            seed(seededSnow, snowAmount, g_snowRefs, BatLitNative::g_snowParticleCountOffset);
        }

        auto drawWeatherRow = [&](const char* title, int& amount, int liveCount,
                                  std::vector<EnvParticleRef>& refs, int offset, const wchar_t* logLabel) {
            ImGui::PushID(title);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(title);
            ImGui::SameLine(92.0f * g_uiScale);
            if (!g_weatherParticleResolverReady.load() || offset < 0 || liveCount <= 0) {
                ImGui::BeginDisabled();
                int unavailable = amount;
                ImGui::SetNextItemWidth(190.0f * g_uiScale);
                ImGui::InputInt("##Amount", &unavailable, 500, 5000);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("not active");
            } else {
                ImGui::SetNextItemWidth(190.0f * g_uiScale);
                if (ImGui::InputInt("##Amount", &amount, 500, 5000)) {
                    amount = (std::max)(0, amount);
                    SetLocationParticleAmount(logLabel, refs, offset, amount);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%d live", liveCount);
            }
            ImGui::PopID();
        };

        drawWeatherRow("Rain", rainAmount, g_rainLiveCount.load(), g_rainRefs, BatLitNative::g_rainParticleCountOffset, L"Rain");
        drawWeatherRow("Snow", snowAmount, g_snowLiveCount.load(), g_snowRefs, BatLitNative::g_snowParticleCountOffset, L"Snow");
        drawWeatherRow("Dust", dustAmount, g_dustLiveCount.load(), g_dustRefs, BatLitNative::g_dustParticleCountOffset, L"Dust");
        drawWeatherRow("Pollen", pollenAmount, g_pollenLiveCount.load(), g_pollenRefs, BatLitNative::g_pollenParticleCountOffset, L"Pollen");
    }

    if (ImGui::CollapsingHeader("Lightning", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::wstring lightningStatus = BatLitNative::GetLightningStatus();
        ImGui::TextWrapped("Status: %ls", lightningStatus.c_str());

        ImGui::Checkbox("Randomize Screen Position", &BatLitNative::g_lightningRandomizePosition);
        if (BatLitNative::g_lightningRandomizePosition) {
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::SliderFloat("Horizontal Spread", &BatLitNative::g_lightningSpreadX, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::SliderFloat("Vertical Spread", &BatLitNative::g_lightningSpreadY, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::SliderFloat("Distance Variation", &BatLitNative::g_lightningDistanceJitter, 0.0f, 0.50f, "%.2f");
        }

        if (ImGui::TreeNodeEx("Small Lightning", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(180.0f * g_uiScale); if (ImGui::InputFloat("Duration (s)##Small", &BatLitNative::g_smallLightningDuration, 0.1f, 1.0f, "%.2f") && BatLitNative::g_smallLightningDuration < 0.05f) BatLitNative::g_smallLightningDuration = 0.05f;
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::DragFloat("Distance##Small", &BatLitNative::g_smallLightningDistance, 50.0f, 100.0f, 50000.0f, "%.0f");
            ImGui::InputInt("Instances##Small", &BatLitNative::g_smallLightningInstances, 1, 5); BatLitNative::g_smallLightningInstances = (std::max)(1, (std::min)(32, BatLitNative::g_smallLightningInstances));
            ImGui::InputInt("Salvo Count##Small", &BatLitNative::g_smallLightningSalvoCount, 1, 5); BatLitNative::g_smallLightningSalvoCount = (std::max)(1, (std::min)(32, BatLitNative::g_smallLightningSalvoCount));
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::InputFloat("Salvo Interval (s)##Small", &BatLitNative::g_smallLightningSalvoInterval, 0.01f, 0.10f, "%.3f"); BatLitNative::g_smallLightningSalvoInterval = (std::max)(0.0f, (std::min)(5.0f, BatLitNative::g_smallLightningSalvoInterval));
            ImGui::InputInt("Clusters##Small", &BatLitNative::g_smallLightningClusters, 1, 1); BatLitNative::g_smallLightningClusters = (std::max)(1, (std::min)(BatLitNative::g_smallLightningSalvoCount, BatLitNative::g_smallLightningClusters));
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::SliderFloat("Cluster Spread##Small", &BatLitNative::g_smallLightningClusterSpread, 0.0f, 0.50f, "%.2f");
            if (ImGui::Button("Trigger Small Salvo")) {
                BatLitNative::QueueLightningTrigger(false, BatLitNative::g_smallLightningDuration, BatLitNative::g_smallLightningDistance, BatLitNative::g_smallLightningInstances,
                    BatLitNative::g_smallLightningSalvoCount, BatLitNative::g_smallLightningSalvoInterval, BatLitNative::g_smallLightningClusters, BatLitNative::g_smallLightningClusterSpread,
                    BatLitNative::g_lightningRandomizePosition, BatLitNative::g_lightningSpreadX, BatLitNative::g_lightningSpreadY, BatLitNative::g_lightningDistanceJitter);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Vertical / Big Lightning")) {
            ImGui::SetNextItemWidth(180.0f * g_uiScale); if (ImGui::InputFloat("Duration (s)##Vertical", &BatLitNative::g_verticalLightningDuration, 0.1f, 1.0f, "%.2f") && BatLitNative::g_verticalLightningDuration < 0.05f) BatLitNative::g_verticalLightningDuration = 0.05f;
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::DragFloat("Distance##Vertical", &BatLitNative::g_verticalLightningDistance, 50.0f, 100.0f, 50000.0f, "%.0f");
            ImGui::InputInt("Instances##Vertical", &BatLitNative::g_verticalLightningInstances, 1, 5); BatLitNative::g_verticalLightningInstances = (std::max)(1, (std::min)(32, BatLitNative::g_verticalLightningInstances));
            ImGui::InputInt("Salvo Count##Vertical", &BatLitNative::g_verticalLightningSalvoCount, 1, 5); BatLitNative::g_verticalLightningSalvoCount = (std::max)(1, (std::min)(32, BatLitNative::g_verticalLightningSalvoCount));
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::InputFloat("Salvo Interval (s)##Vertical", &BatLitNative::g_verticalLightningSalvoInterval, 0.01f, 0.10f, "%.3f"); BatLitNative::g_verticalLightningSalvoInterval = (std::max)(0.0f, (std::min)(5.0f, BatLitNative::g_verticalLightningSalvoInterval));
            ImGui::InputInt("Clusters##Vertical", &BatLitNative::g_verticalLightningClusters, 1, 1); BatLitNative::g_verticalLightningClusters = (std::max)(1, (std::min)(BatLitNative::g_verticalLightningSalvoCount, BatLitNative::g_verticalLightningClusters));
            ImGui::SetNextItemWidth(180.0f * g_uiScale); ImGui::SliderFloat("Cluster Spread##Vertical", &BatLitNative::g_verticalLightningClusterSpread, 0.0f, 0.50f, "%.2f");
            if (ImGui::Button("Trigger Vertical Salvo")) {
                BatLitNative::QueueLightningTrigger(true, BatLitNative::g_verticalLightningDuration, BatLitNative::g_verticalLightningDistance, BatLitNative::g_verticalLightningInstances,
                    BatLitNative::g_verticalLightningSalvoCount, BatLitNative::g_verticalLightningSalvoInterval, BatLitNative::g_verticalLightningClusters, BatLitNative::g_verticalLightningClusterSpread,
                    BatLitNative::g_lightningRandomizePosition, BatLitNative::g_lightningSpreadX, BatLitNative::g_lightningSpreadY, BatLitNative::g_lightningDistanceJitter);
            }
            ImGui::TreePop();
        }
    }

    if (ImGui::CollapsingHeader("Misc", ImGuiTreeNodeFlags_DefaultOpen)) {
        static bool radioModeEyes = false;
        static bool gauntletLight = false;
        static bool vehicles = false;
        static bool vehiclesOnFire = false;
        static bool combatVfx = true;

        if (ImGui::Checkbox("Radio Mode Eyes", &radioModeEyes))
            QueueRadioModeEyes(radioModeEyes);

        if (ImGui::Checkbox("Gauntlet Light", &gauntletLight))
            BatLitNative::QueueConsoleCommand(gauntletLight ?
                L"set BmGame.RPawnPlayerBmBase bGauntletLightRunning true" :
                L"set BmGame.RPawnPlayerBmBase bGauntletLightRunning false");

        if (ImGui::Checkbox("Vehicles", &vehicles)) {
            BatLitNative::QueueConsoleCommand(vehicles ? L"set BmGame.RAbandonedVehicle HeadlightsSwitchedOn 1" : L"set BmGame.RAbandonedVehicle HeadlightsSwitchedOn 0");
            BatLitNative::QueueConsoleCommand(vehicles ? L"set BmGame.RAbandonedVehicle InteriorLightsSwitchedOn 1" : L"set BmGame.RAbandonedVehicle InteriorLightsSwitchedOn 0");
            BatLitNative::QueueConsoleCommand(vehicles ? L"set BmGame.RAbandonedVehicle AuxLightsSwitchedOn 1" : L"set BmGame.RAbandonedVehicle AuxLightsSwitchedOn 0");
            BatLitNative::QueueConsoleCommand(vehicles ? L"set BmGame.RAbandonedVehicle HazardLightsSwitchedOn 1" : L"set BmGame.RAbandonedVehicle HazardLightsSwitchedOn 0");
        }

        if (ImGui::Checkbox("Vehicles On Fire", &vehiclesOnFire))
            BatLitNative::QueueConsoleCommand(vehiclesOnFire ?
                L"set BmGame.RAbandonedVehicle bOnFire true" :
                L"set BmGame.RAbandonedVehicle bOnFire false");

        if (ImGui::Checkbox("Combat VFX", &combatVfx)) {
            BatLitNative::QueueConsoleCommand(combatVfx ?
                L"set BmGame.RPersistentDebugData Debug_Combat_DisableStrikeTrails false" :
                L"set BmGame.RPersistentDebugData Debug_Combat_DisableStrikeTrails true");
            BatLitNative::QueueConsoleCommand(combatVfx ?
                L"set BmGame.RPersistentDebugData Debug_Combat_DisableImpactVFX 0" :
                L"set BmGame.RPersistentDebugData Debug_Combat_DisableImpactVFX 1");
        }
    }
}


void DrawBatLitEditor(BatLitNative::LightRecord& rec) {
    if (!rec.editOpen) { rec.editorLoggedOpen = false; return; }
    if (!rec.editorLoggedOpen) {
        BatLitNative::LogLightRecord(L"DrawEditor entered", rec);
        rec.editorLoggedOpen = true;
    }
    std::string title = rec.name + " - Edit##BatLit_" + std::to_string(rec.id);
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workSize = viewport ? viewport->WorkSize : ImGui::GetIO().DisplaySize;
    const ImVec2 workPos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 wantedSize(610.0f * g_uiScale, 700.0f * g_uiScale);
    const ImVec2 editorSize(
        (wantedSize.x < workSize.x - 40.0f) ? wantedSize.x : (workSize.x - 40.0f),
        (wantedSize.y < workSize.y - 40.0f) ? wantedSize.y : (workSize.y - 40.0f));
    ImGui::SetNextWindowSize(editorSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(title.c_str(), &rec.editOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }

    auto& st = rec.state;
    ImGui::TextDisabled("Actor: 0x%llX   Component: 0x%llX", (unsigned long long)rec.actor, (unsigned long long)rec.component);
    if (rec.statePending) ImGui::TextDisabled("Initializing spawned light state...");
    if (!rec.valid) ImGui::TextColored(ImVec4(1,0.35f,0.25f,1), "Light is no longer valid.");

    if (ImGui::Button("Move to Camera")) { BatLitNative::LogLightRecord(L"Move to Camera clicked", rec); BatLitNative::Queue(BatLitNative::CommandType::MoveToCamera, rec.id); }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) BatLitNative::Queue(BatLitNative::CommandType::Reset, rec.id);
    ImGui::Separator();

    ImGui::BeginDisabled(!rec.valid || rec.statePending);
    if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Checkbox("Enabled", &st.enabled)) rec.liveDirty |= BatLitNative::LiveEnabled;
        if (ImGui::ColorEdit3("Color", st.color)) rec.liveDirty |= BatLitNative::LiveColor;
        if (ImGui::DragFloat("Intensity", &st.intensity, 0.05f, 0.0f, 100000.0f, "%.3f")) rec.liveDirty |= BatLitNative::LiveIntensity;
        if (ImGui::DragFloat("Radius", &st.radius, 1.0f, 0.0f, 1000000.0f, "%.1f")) rec.liveDirty |= BatLitNative::LiveRadius;
        if (ImGui::DragFloat("Light Bulb Radius", &st.lightBulbRadius, 0.1f, 0.0f, 100000.0f, "%.2f")) rec.liveDirty |= BatLitNative::LiveBulb;
        if (ImGui::DragFloat("Falloff Exponent", &st.falloffExponent, 0.01f, -1000.0f, 1000.0f, "%.3f")) rec.liveDirty |= BatLitNative::LiveFalloff;
        if (ImGui::Checkbox("Quadratic Falloff", &st.quadraticFalloff)) rec.liveDirty |= BatLitNative::LiveQuadratic;
    }

    if (ImGui::CollapsingHeader("World Position and Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::DragFloat("X", &st.translation.x, 1.0f, -1000000.0f, 1000000.0f, "%.1f")) rec.liveDirty |= BatLitNative::LivePosition;
        if (ImGui::DragFloat("Y", &st.translation.y, 1.0f, -1000000.0f, 1000000.0f, "%.1f")) rec.liveDirty |= BatLitNative::LivePosition;
        if (ImGui::DragFloat("Z", &st.translation.z, 1.0f, -1000000.0f, 1000000.0f, "%.1f")) rec.liveDirty |= BatLitNative::LivePosition;

        if (rec.isSpot) {
            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::DragFloat("Pitch", &st.pitch, 0.25f, -360.0f, 360.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveRotation;
            if (ImGui::DragFloat("Yaw", &st.yaw, 0.25f, -360.0f, 360.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveRotation;
            if (ImGui::DragFloat("Roll", &st.roll, 0.25f, -360.0f, 360.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveRotation;
            if (ImGui::Checkbox("Absolute Rotation", &st.absoluteRotation)) rec.liveDirty |= BatLitNative::LiveAbsoluteRotation;

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::DragFloat("Inner Cone", &st.innerConeAngle, 0.1f, 0.0f, 180.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveCone;
            if (ImGui::DragFloat("Outer Cone", &st.outerConeAngle, 0.1f, 0.0f, 180.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveCone;
            if (ImGui::DragFloat("Light Shaft Cone", &st.lightShaftConeAngle, 0.1f, -360.0f, 360.0f, "%.2f deg")) rec.liveDirty |= BatLitNative::LiveShaftCone;
        }
    }

    ImGui::EndDisabled();
    if (rec.liveEditFailed) ImGui::TextColored(ImVec4(1,0.4f,0.3f,1), "The last light update failed.");

    // Shadows and advanced experimental light sections are intentionally hidden from the UI.
    // Their state fields remain in the backend for compatibility while BatLit is cleaned up.


    ImGui::Separator();
    ImGui::TextDisabled("Live editing - changes are sent automatically.");
    ImGui::End();
}

void DrawBatLitTab() {
    ImGui::TextUnformatted("BatLit");
    ImGui::TextDisabled("Native light manager - one record per spawned light.");

    ImGui::Checkbox("Show Light Gizmo", &g_showLightGizmo);
    if (g_showLightGizmo) {
        ImGui::SameLine(); ImGui::Checkbox("Radius", &g_showGizmoRadius);
        ImGui::SameLine(); ImGui::Checkbox("Axis", &g_showGizmoAxis);
        ImGui::SameLine(); ImGui::Checkbox("Cone", &g_showGizmoCone);
    }

    if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool enabled = g_exposureWanted.load();
        if (ImGui::Checkbox("Exposure Override", &enabled))
            g_exposureWanted.store(enabled);

        float exposure = g_exposureValue.load();
        const bool applied = g_exposureApplied.load();
        if (!applied) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(220.0f * g_uiScale);
        if (ImGui::DragFloat("Exposure Base Offset", &exposure, 0.05f, -10.0f, 10.0f, "%.3f"))
            g_exposureValue.store(exposure);
        if (!applied) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Reset Exposure")) g_exposureWanted.store(false);

        if (enabled && !applied)
            ImGui::TextDisabled("Resolving LocalPlayer / enabling LevelPP override...");
        else if (applied)
            ImGui::TextDisabled("Live exposure - LevelPP + CurrentPP; active player only.");
        else
            ImGui::TextDisabled("Game-controlled exposure. Enable override to edit BaseOffset.");
        ImGui::Spacing();
        ImGui::Separator();
    }
    if (ImGui::Button("Create Spot")) BatLitNative::Queue(BatLitNative::CommandType::SpawnSpot);
    ImGui::SameLine();
    if (ImGui::Button("Create Point")) BatLitNative::Queue(BatLitNative::CommandType::SpawnPoint);
    ImGui::Spacing();
    ImGui::Separator();

    std::lock_guard<std::mutex> lock(BatLitNative::g_lightMutex);
    if (BatLitNative::g_lights.empty()) {
        ImGui::TextDisabled("No BatLit lights yet.");
        ImGui::TextDisabled("Create a Spot or Point light; it will appear here as a separate record.");
        return;
    }
    for (auto& rec : BatLitNative::g_lights) {
        ImGui::PushID((int)rec.id);
        ImGui::TextUnformatted(rec.name.c_str());
        if (rec.statePending) { ImGui::SameLine(); ImGui::TextDisabled("(initializing)"); }
        ImGui::SameLine(170.0f * g_uiScale);
        if (ImGui::Button("Edit")) {
            rec.editOpen = true;
            BatLitNative::LogLightRecord(L"Edit clicked", rec);
        }
        ImGui::SameLine();
        bool enabled = rec.state.enabled;
        if (ImGui::Checkbox("Enabled", &enabled)) {
            rec.state.enabled = enabled;
            BatLitNative::Queue(BatLitNative::CommandType::ToggleEnabled, rec.id, enabled);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Gizmo", &rec.gizmoVisible);
        ImGui::SameLine();
        ImGui::TextDisabled("Intensity %.2f", rec.state.intensity);
        ImGui::PopID();
    }
}

void DrawBatLitEditors() {
    std::lock_guard<std::mutex> lock(BatLitNative::g_lightMutex);
    for (auto& rec : BatLitNative::g_lights) DrawBatLitEditor(rec);
}

static float Dot3(const BatLitNative::Vec3& a, const BatLitNative::Vec3& b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static BatLitNative::Vec3 Sub3(const BatLitNative::Vec3& a, const BatLitNative::Vec3& b) {
    return {a.x-b.x, a.y-b.y, a.z-b.z};
}

static BatLitNative::Vec3 AddScaled3(const BatLitNative::Vec3& a, const BatLitNative::Vec3& d, float scale) {
    return {a.x+d.x*scale, a.y+d.y*scale, a.z+d.z*scale};
}

static BatLitNative::Vec3 Add3(const BatLitNative::Vec3& a, const BatLitNative::Vec3& b) {
    return {a.x+b.x, a.y+b.y, a.z+b.z};
}

static BatLitNative::Vec3 Mul3(const BatLitNative::Vec3& a, float s) {
    return {a.x*s, a.y*s, a.z*s};
}

static BatLitNative::Vec3 Cross3(const BatLitNative::Vec3& a, const BatLitNative::Vec3& b) {
    return {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    };
}

static float Length3(const BatLitNative::Vec3& v) {
    return sqrtf(Dot3(v, v));
}

static BatLitNative::Vec3 Normalize3(const BatLitNative::Vec3& v) {
    const float len = Length3(v);
    if (len <= 0.00001f) return {0.0f, 0.0f, 0.0f};
    return {v.x/len, v.y/len, v.z/len};
}

static void MakePerpendicularBasis(
    const BatLitNative::Vec3& forward,
    BatLitNative::Vec3& outRight,
    BatLitNative::Vec3& outUp)
{
    // IMPORTANT: this basis is derived ONLY from the light direction and world axes.
    // It must never use the camera right/up vectors, otherwise the gizmo rotates with camera.
    const BatLitNative::Vec3 f = Normalize3(forward);
    const BatLitNative::Vec3 worldUp =
        (fabsf(f.z) < 0.98f) ? BatLitNative::Vec3{0.0f, 0.0f, 1.0f}
                             : BatLitNative::Vec3{0.0f, 1.0f, 0.0f};

    outRight = Normalize3(Cross3(worldUp, f));
    outUp = Normalize3(Cross3(f, outRight));
}

struct GizmoCamera;
static bool ProjectGizmoPoint(const GizmoCamera& cam, const BatLitNative::Vec3& world, ImVec2& screen, float* depthOut = nullptr);

static void DrawWorldCircle(
    ImDrawList* dl,
    const GizmoCamera& cam,
    const BatLitNative::Vec3& center,
    const BatLitNative::Vec3& axisA,
    const BatLitNative::Vec3& axisB,
    float radius,
    ImU32 color,
    int segments = 48,
    float thickness = 1.0f)
{
    ImVec2 first{};
    ImVec2 prev{};
    bool firstValid = false;
    bool prevValid = false;

    for (int i = 0; i <= segments; ++i) {
        const float a = (static_cast<float>(i) / static_cast<float>(segments)) * 6.28318530717958647692f;
        const BatLitNative::Vec3 world =
            Add3(center, Add3(Mul3(axisA, cosf(a) * radius), Mul3(axisB, sinf(a) * radius)));

        ImVec2 cur{};
        const bool valid = ProjectGizmoPoint(cam, world, cur);

        if (valid) {
            if (!firstValid) {
                first = cur;
                firstValid = true;
            }
            if (prevValid)
                dl->AddLine(prev, cur, color, thickness);
            prev = cur;
            prevValid = true;
        } else {
            prevValid = false;
        }
    }
}

static BatLitNative::Vec3 DegreesForward(float pitchDeg, float yawDeg) {
    const float p = pitchDeg * 3.14159265358979323846f / 180.0f;
    const float y = yawDeg   * 3.14159265358979323846f / 180.0f;
    const float cp = cosf(p), sp = sinf(p), cy = cosf(y), sy = sinf(y);
    return {cp*cy, cp*sy, sp};
}

struct GizmoCamera {
    BatLitNative::Vec3 pos{};
    BatLitNative::Vec3 forward{};
    BatLitNative::Vec3 right{};
    BatLitNative::Vec3 up{};
    float focal{};
    ImVec2 center{};
};

static bool GetGizmoCamera(GizmoCamera& out) {
    void* pc = BatLitNative::g_lastPlayerController;
    if (!pc || !BatLitNative::IsReadableAddress(pc, BatLitNative::kPlayerControllerPlayerCamera + sizeof(void*))) return false;
    void* camera = BatLitNative::Read<void*>(pc, BatLitNative::kPlayerControllerPlayerCamera);
    const ptrdiff_t pov = BatLitNative::kCameraCache + BatLitNative::kCameraCachePOV;
    if (!camera || !BatLitNative::IsReadableAddress(reinterpret_cast<uint8_t*>(camera) + pov, 28)) return false;

    const BatLitNative::Vec3 pos = BatLitNative::Read<BatLitNative::Vec3>(camera, pov + 0x00);
    const BatLitNative::RotatorRaw rr = BatLitNative::Read<BatLitNative::RotatorRaw>(camera, pov + 0x0C);
    const float fov = BatLitNative::Read<float>(camera, pov + 0x18);
    if (!std::isfinite(fov) || fov < 1.0f || fov > 179.0f) return false;

    const float pitch = BatLitNative::RuuToDegrees(rr.pitch) * 3.14159265358979323846f / 180.0f;
    const float yaw   = BatLitNative::RuuToDegrees(rr.yaw)   * 3.14159265358979323846f / 180.0f;
    const float roll  = BatLitNative::RuuToDegrees(rr.roll)  * 3.14159265358979323846f / 180.0f;
    const float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
    const float cr = cosf(roll), sr = sinf(roll);
    out.pos = pos;
    out.forward = {cp*cy, cp*sy, sp};
    const BatLitNative::Vec3 baseRight{-sy, cy, 0.0f};
    const BatLitNative::Vec3 baseUp{-sp*cy, -sp*sy, cp};
    // UE3 FRotationMatrix: positive roll tilts camera right toward -world up.
    // Reversing this sign makes a world-anchored overlay slide under camera roll.
    out.right = {baseRight.x*cr - baseUp.x*sr, baseRight.y*cr - baseUp.y*sr, baseRight.z*cr - baseUp.z*sr};
    out.up = {baseUp.x*cr + baseRight.x*sr, baseUp.y*cr + baseRight.y*sr, baseUp.z*cr + baseRight.z*sr};

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    out.center = ImVec2(display.x * 0.5f, display.y * 0.5f);
    out.focal = display.x * 0.5f / tanf((fov * 0.5f) * 3.14159265358979323846f / 180.0f);
    return display.x > 1.0f && display.y > 1.0f;
}

static bool ProjectGizmoPoint(const GizmoCamera& cam, const BatLitNative::Vec3& world, ImVec2& screen, float* depthOut) {
    const BatLitNative::Vec3 d = Sub3(world, cam.pos);
    const float depth = Dot3(d, cam.forward);
    if (depth <= 1.0f) return false;
    const float sx = Dot3(d, cam.right);
    const float sy = Dot3(d, cam.up);
    screen.x = cam.center.x + (sx / depth) * cam.focal;
    screen.y = cam.center.y - (sy / depth) * cam.focal;
    if (depthOut) *depthOut = depth;
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

static void DrawOneLightGizmo(
    const GizmoCamera& cam,
    const BatLitNative::LightRecord& rec,
    ImDrawList* dl)
{
    if (!rec.gizmoVisible || !rec.valid || !rec.actor)
        return;

    if (!BatLitNative::IsReadableAddress(
            reinterpret_cast<uint8_t*>(rec.actor) + BatLitNative::kActorLocation,
            sizeof(BatLitNative::Vec3)))
        return;

    // Follow the accepted world position, never a camera-relative offset or a
    // UI preview that has not yet been applied by the game thread.
    const BatLitNative::Vec3 pos =
        BatLitNative::Read<BatLitNative::Vec3>(rec.actor, BatLitNative::kActorLocation);
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return;

    ImVec2 p{};
    float depth = 0.0f;
    if (!ProjectGizmoPoint(cam, pos, p, &depth))
        return;

    // Gizmo colors:
    //   RED   = forward direction / where the light points
    //   BLUE  = Spot angle / cone
    //   GRAY  = Point Light radius
    //   WHITE = light origin
    const ImU32 originCol = IM_COL32(245,245,245,255);
    const ImU32 axisCol   = IM_COL32(235,65,65,255);
    const ImU32 coneCol   = IM_COL32(70,145,255,245);
    const ImU32 radiusCol = IM_COL32(145,145,145,210);

    const float cross = 9.0f;
    dl->AddCircleFilled(p, 5.0f, originCol, 16);
    dl->AddLine(ImVec2(p.x-cross,p.y), ImVec2(p.x+cross,p.y), originCol, 2.0f);
    dl->AddLine(ImVec2(p.x,p.y-cross), ImVec2(p.x,p.y+cross), originCol, 2.0f);

    // Point Light only: spherical influence radius.
    if (!rec.isSpot && g_showGizmoRadius && rec.state.radius > 0.0f) {
        const BatLitNative::Vec3 wx{1.0f, 0.0f, 0.0f};
        const BatLitNative::Vec3 wy{0.0f, 1.0f, 0.0f};
        const BatLitNative::Vec3 wz{0.0f, 0.0f, 1.0f};

        DrawWorldCircle(dl, cam, pos, wx, wy, rec.state.radius, radiusCol, 48, 2.0f);
        DrawWorldCircle(dl, cam, pos, wx, wz, rec.state.radius, radiusCol, 48, 2.0f);
        DrawWorldCircle(dl, cam, pos, wy, wz, rec.state.radius, radiusCol, 48, 2.0f);
    }

    // Spot Light: direction + cone only. No radius sphere.
    if (rec.isSpot && (g_showGizmoAxis || g_showGizmoCone)) {
        float pitch = rec.state.pitch;
        float yaw = rec.state.yaw;

        if (!rec.state.absoluteRotation &&
            BatLitNative::IsReadableAddress(
                reinterpret_cast<uint8_t*>(rec.actor) + BatLitNative::kActorRotation,
                sizeof(BatLitNative::RotatorRaw)))
        {
            const BatLitNative::RotatorRaw ar =
                BatLitNative::Read<BatLitNative::RotatorRaw>(
                    rec.actor, BatLitNative::kActorRotation);
            pitch += BatLitNative::RuuToDegrees(ar.pitch);
            yaw   += BatLitNative::RuuToDegrees(ar.yaw);
        }

        const BatLitNative::Vec3 dir = Normalize3(DegreesForward(pitch, yaw));
        const float axisLen =
            (std::max)(100.0f, (std::min)(rec.state.radius, 5000.0f));
        const BatLitNative::Vec3 end = AddScaled3(pos, dir, axisLen);

        ImVec2 e{};
        if (ProjectGizmoPoint(cam, end, e)) {
            if (g_showGizmoAxis)
                dl->AddLine(p, e, axisCol, 3.0f);

            if (g_showGizmoCone) {
                const float angle =
                    std::clamp(rec.state.outerConeAngle, 0.1f, 89.0f) *
                    3.14159265358979323846f / 180.0f;
                const float coneRadius = tanf(angle) * axisLen;

                BatLitNative::Vec3 coneRight{}, coneUp{};
                MakePerpendicularBasis(dir, coneRight, coneUp);

                DrawWorldCircle(
                    dl, cam, end, coneRight, coneUp,
                    coneRadius, coneCol, 48, 2.5f);

                const BatLitNative::Vec3 rim0 =
                    AddScaled3(end, coneRight,  coneRadius);
                const BatLitNative::Vec3 rim1 =
                    AddScaled3(end, coneRight, -coneRadius);
                const BatLitNative::Vec3 rim2 =
                    AddScaled3(end, coneUp,     coneRadius);
                const BatLitNative::Vec3 rim3 =
                    AddScaled3(end, coneUp,    -coneRadius);

                for (const auto& rim : {rim0, rim1, rim2, rim3}) {
                    ImVec2 rp{};
                    if (ProjectGizmoPoint(cam, rim, rp))
                        dl->AddLine(p, rp, coneCol, 2.0f);
                }
            }
        }
    }

    char label[96]{};
    if (rec.isSpot)
        snprintf(label, sizeof(label), "%s", rec.name.c_str());
    else
        snprintf(label, sizeof(label), "%s  R %.0f", rec.name.c_str(), rec.state.radius);

    dl->AddText(ImVec2(p.x + 10.0f, p.y + 8.0f), originCol, label);
}

bool HasVisibleLightGizmos() {
    if (!g_showLightGizmo)
        return false;

    std::lock_guard<std::mutex> lock(BatLitNative::g_lightMutex);
    for (const auto& rec : BatLitNative::g_lights) {
        if (rec.gizmoVisible && rec.valid && rec.actor)
            return true;
    }
    return false;
}

void DrawLightGizmos() {
    if (!g_showLightGizmo)
        return;

    GizmoCamera cam{};
    if (!GetGizmoCamera(cam))
        return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    std::lock_guard<std::mutex> lock(BatLitNative::g_lightMutex);
    for (const auto& rec : BatLitNative::g_lights)
        DrawOneLightGizmo(cam, rec, dl);
}



void DrawPoseTab() {
    ImGui::TextUnformatted("Pose Inspector");
    ImGui::TextDisabled("Pawn, attached skeletal meshes, pause-bit readback and per-tick pose-change diagnostics.");
    ImGui::Spacing();

    bool keepRunningUnfocused = BatLitNative::g_poseKeepRunningUnfocused.load();
    if (BatLitNative::g_enginePauseOnLossOfFocusOffset >= 0) {
        if (ImGui::Checkbox("Keep game running when unfocused", &keepRunningUnfocused))
            BatLitNative::g_poseKeepRunningUnfocused.store(keepRunningUnfocused);
        ImGui::TextDisabled("Enabled automatically. Turn it off to restore the original focus-pause setting.");
    } else {
        ImGui::TextDisabled("Keep game running when unfocused is unavailable for this game build.");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Player-controlled Pawn = active character from the loaded save; the number is only its list index.");

    std::vector<BatLitNative::PoseTarget> targets;
    {
        std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
        targets = BatLitNative::g_poseTargets;
    }
    int targetIndex = BatLitNative::g_poseSelectedTarget.load();
    char targetPreview[96] = "Refresh character list";
    if (targetIndex >= 0 && targetIndex < static_cast<int>(targets.size())) {
        const auto& target = targets[static_cast<size_t>(targetIndex)];
        snprintf(targetPreview, sizeof(targetPreview), "%s #%d  [Pawn 0x%llX]",
            target.isPlayer ? "Active player Pawn" : "World character Pawn", targetIndex,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target.pawn)));
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("Character", targetPreview)) {
        for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
            const auto& target = targets[static_cast<size_t>(i)];
            char label[96]{};
            snprintf(label, sizeof(label), "%s #%d  [Pawn 0x%llX]",
                target.isPlayer ? "Active player Pawn" : "World character Pawn", i,
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target.pawn)));
            if (ImGui::Selectable(label, targetIndex == i)) {
                BatLitNative::g_poseSelectedTarget.store(i);
                BatLitNative::g_poseRefreshRequested.store(true);
                std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
                BatLitNative::g_poseStatus = L"Refreshing selected character skeleton...";
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Refresh Characters / Skeleton")) {
        BatLitNative::g_poseRefreshRequested.store(true);
        std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
        BatLitNative::g_poseStatus = L"Resolving world Pawns and reading the selected skeleton...";
    }

    std::wstring status;
    std::wstring diagnosticStatus;
    void* pawn = nullptr;
    void* mesh = nullptr;
    std::vector<BatLitNative::PoseMeshDiagnostic> meshDiagnostics;
    int parentCursor = -1;
    int boneCount = 0;
    {
        std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
        status = BatLitNative::g_poseStatus;
        pawn = BatLitNative::g_posePawn;
        mesh = BatLitNative::g_poseMesh;
        diagnosticStatus = BatLitNative::g_poseDiagnosticStatus;
        meshDiagnostics = BatLitNative::g_poseMeshDiagnostics;
        boneCount = static_cast<int>(BatLitNative::g_poseBones.size());
    }
    parentCursor = BatLitNative::g_poseParentCursor.load();
    ImGui::SameLine();
    ImGui::TextDisabled("%ls", status.c_str());
    ImGui::TextDisabled("Pawn: 0x%llX   SkeletalMesh: 0x%llX",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pawn)),
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mesh)));
    if (parentCursor >= 0)
        ImGui::TextDisabled("Parent scan: %d / %d", parentCursor, boneCount);

    ImGui::Separator();
    std::vector<BatLitNative::PoseBoneInfo> bones;
    BatLitNative::PoseBoneTransform transform{};
    {
        std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
        bones = BatLitNative::g_poseBones;
        transform = BatLitNative::g_poseSelectedTransform;
    }

    ImGui::TextUnformatted("Bones");
    const float listHeight = (std::max)(160.0f * g_uiScale, ImGui::GetContentRegionAvail().y - 300.0f * g_uiScale);
    if (ImGui::BeginChild("##PoseBoneList", ImVec2(0.0f, listHeight), true)) {
        for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
            const auto& bone = bones[static_cast<size_t>(i)];
            char boneFallback[40]{};
            char parentFallback[40]{};
            if (!bone.resolvedName)
                snprintf(boneFallback, sizeof(boneFallback), "FName %d:%d", bone.name.index, bone.name.number);
            if (!bone.parentIndex || bone.parentIndex < 0) {
                if (bone.parent.index || bone.parent.number)
                    snprintf(parentFallback, sizeof(parentFallback), "FName %d:%d", bone.parent.index, bone.parent.number);
                else
                    snprintf(parentFallback, sizeof(parentFallback), "root");
            }
            const char* boneName = bone.resolvedName ? bone.resolvedName : boneFallback;
            const char* parentName = bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(bones.size())
                ? (bones[static_cast<size_t>(bone.parentIndex)].resolvedName
                    ? bones[static_cast<size_t>(bone.parentIndex)].resolvedName : "FName")
                : parentFallback;
            char parentLabel[80]{};
            if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(bones.size())) {
                const auto& parent = bones[static_cast<size_t>(bone.parentIndex)];
                if (parent.resolvedName)
                    snprintf(parentLabel, sizeof(parentLabel), "%03d %s", bone.parentIndex, parentName);
                else
                    snprintf(parentLabel, sizeof(parentLabel), "%03d FName %d:%d", bone.parentIndex,
                        parent.name.index, parent.name.number);
            } else {
                snprintf(parentLabel, sizeof(parentLabel), "%s", parentFallback);
            }
            char label[256]{};
            snprintf(label, sizeof(label), "%03d  [%s] %s   parent %s", i,
                bone.category ? bone.category : "Unmapped", boneName, parentLabel);
            const bool selected = BatLitNative::g_poseSelectedBone.load() == i;
            if (ImGui::Selectable(label, selected)) {
                BatLitNative::g_poseSelectedBone.store(i);
                BatLitNative::g_poseTransformRequested.store(true);
                BatLitNative::RebuildPoseSelectedSubtreeIndices();
            }
        }
    }
    ImGui::EndChild();

    const int selected = BatLitNative::g_poseSelectedBone.load();
    static int poseInputSelectedTarget = -1;
    static int poseInputSelectedBone = -1;
    static bool poseInputReady = false;
    static float poseAbsoluteInput[3]{};
    static float poseAbsoluteRotationInput[3]{};
    static float poseReadPosition[3]{};
    static float poseReadRotation[3]{};
    if (poseInputSelectedTarget != targetIndex || poseInputSelectedBone != selected) {
        poseInputSelectedTarget = targetIndex;
        poseInputSelectedBone = selected;
        poseInputReady = false;
    }
    if (selected >= 0 && selected < static_cast<int>(bones.size())) {
        ImGui::Text("Selected bone: %d", selected);
        bool anySpaceValid = false;
        for (int space = 0; space < 3; ++space) {
            if (!transform.validBySpace[space]) continue;
            anySpaceValid = true;
            const auto& p = transform.locationBySpace[space];
            const auto* q = transform.quaternionBySpace[space];
            const char* label = space == 0 ? "World (0)" : space == 1 ? "Component (1)" : "Space 2";
            ImGui::Text("%s  P(%.2f, %.2f, %.2f)  Q(%.4f, %.4f, %.4f, %.4f)",
                label, p.x, p.y, p.z, q[0], q[1], q[2], q[3]);
        }
        if (transform.validBySpace[1]) {
            const float* q = transform.quaternionBySpace[1];
            constexpr float kRadiansToDegrees = 57.29577951308232f;
            const float pitchSin = (std::max)(-1.0f, (std::min)(1.0f,
                2.0f * (q[3] * q[1] - q[2] * q[0])));
            const float pitch = asinf(pitchSin) * kRadiansToDegrees;
            const float yaw = atan2f(2.0f * (q[3] * q[2] + q[0] * q[1]),
                1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2])) * kRadiansToDegrees;
            const float roll = atan2f(2.0f * (q[3] * q[0] + q[1] * q[2]),
                1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1])) * kRadiansToDegrees;
            const auto& p = transform.locationBySpace[1];
            poseReadPosition[0] = p.x; poseReadPosition[1] = p.y; poseReadPosition[2] = p.z;
            poseReadRotation[0] = pitch; poseReadRotation[1] = yaw; poseReadRotation[2] = roll;
            if (!poseInputReady && transform.selectedBone == selected) {
                memcpy(poseAbsoluteInput, poseReadPosition, sizeof(poseAbsoluteInput));
                memcpy(poseAbsoluteRotationInput, poseReadRotation, sizeof(poseAbsoluteRotationInput));
                for (int axis = 0; axis < 3; ++axis) {
                    BatLitNative::g_poseAbsolutePosition[axis].store(poseAbsoluteInput[axis]);
                    BatLitNative::g_poseAbsoluteRotationDegrees[axis].store(poseAbsoluteRotationInput[axis]);
                    BatLitNative::g_poseTranslationOffset[axis].store(0.0f);
                    BatLitNative::g_poseRotationOffsetDegrees[axis].store(0.0f);
                }
                BatLitNative::g_poseAbsoluteTargetEnabled.store(true);
                BatLitNative::g_poseAbsoluteTargetBone.store(selected);
                poseInputReady = true;
            }
            ImGui::TextDisabled("Current Component XYZ: X %.2f   Y %.2f   Z %.2f", p.x, p.y, p.z);
            ImGui::TextDisabled("Current Component PYR: Pitch %.1f   Yaw %.1f   Roll %.1f deg", pitch, yaw, roll);
            if (transform.spaceBases.valid && selected < transform.spaceBases.count) {
                const uintptr_t atomAddress = transform.spaceBases.data + static_cast<uintptr_t>(selected) * 0x20;
                ImGui::TextDisabled("SpaceBases read target: 0x%llX  (array 0x%llX + bone %d x 0x20)",
                    static_cast<unsigned long long>(atomAddress),
                    static_cast<unsigned long long>(transform.spaceBases.data), selected);
            }
        }
        if (transform.spaceBases.valid) {
            ImGui::TextDisabled("SpaceBases n=%d  T(%.2f, %.2f, %.2f)  Q(%.3f, %.3f, %.3f, %.3f)",
                transform.spaceBases.count, transform.spaceBases.values[4],
                transform.spaceBases.values[5], transform.spaceBases.values[6],
                transform.spaceBases.values[0], transform.spaceBases.values[1],
                transform.spaceBases.values[2], transform.spaceBases.values[3]);
        }
        if (transform.localAtoms.valid) {
            ImGui::TextDisabled("LocalAtoms n=%d  T(%.2f, %.2f, %.2f)  Q(%.3f, %.3f, %.3f, %.3f)",
                transform.localAtoms.count, transform.localAtoms.values[4],
                transform.localAtoms.values[5], transform.localAtoms.values[6],
                transform.localAtoms.values[0], transform.localAtoms.values[1],
                transform.localAtoms.values[2], transform.localAtoms.values[3]);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Pose tooling prototype — Rocksteady Pawn pose output");
        bool freezeDesired = BatLitNative::g_poseFreezeDesired.load();
        if (ImGui::Checkbox("Freeze all Pawn skeletal meshes", &freezeDesired)) {
            BatLitNative::g_poseFreezeDesired.store(freezeDesired);
            if (!freezeDesired) {
                BatLitNative::g_poseEditDesired.store(false);
                BatLitNative::g_poseEditActive.store(false);
            }
        }
        ImGui::TextDisabled("Locks attached meshes after EngineTick. Unfreezing also disables pose edits and diagnostic hooks to restore normal frame rate.");
        if (BatLitNative::g_posePauseAnimsOffset >= 0 && BatLitNative::g_posePauseAnimsMask) {
            const int writeBit = BatLitNative::g_poseAnimPauseWriteReadback.load();
            const int tickBit = BatLitNative::g_poseAnimPauseReadback.load();
            const char* writeText = writeBit < 0 ? "?" : writeBit ? "SET" : "CLEAR";
            const char* tickText = tickBit < 0 ? "?" : tickBit ? "SET" : "CLEAR";
            ImGui::TextDisabled("bPauseAnims readback: last write %s / after game tick %s  |  unchanged by pose freeze",
                writeText, tickText);
        } else {
            ImGui::TextDisabled("bPauseAnims reflection/readback: unavailable on this build");
        }
        ImGui::TextDisabled("%ls", diagnosticStatus.c_str());
        for (const auto& diag : meshDiagnostics) {
            const char* source = diag.sourceList == 0 ? "Components" : diag.sourceList == 1 ? "AllComponents" : "Pawn.Mesh";
            const char* pause = diag.pauseBit < 0 ? "?" : diag.pauseBit ? "SET" : "CLEAR";
            ImGui::TextDisabled("%s[%d] 0x%llX  bones %d  pause %s  pose changes %llu/%llu",
                source, diag.sourceIndex, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.mesh)),
                diag.boneCount, pause, static_cast<unsigned long long>(diag.buffers[0].changedSamples),
                static_cast<unsigned long long>(diag.buffers[0].samples));
            ImGui::TextDisabled("  LocalAtoms %d %llu/%llu | CachedSpaceBases %d %llu/%llu",
                diag.buffers[1].boneCount,
                static_cast<unsigned long long>(diag.buffers[1].changedSamples), static_cast<unsigned long long>(diag.buffers[1].samples),
                diag.buffers[2].boneCount,
                static_cast<unsigned long long>(diag.buffers[2].changedSamples), static_cast<unsigned long long>(diag.buffers[2].samples));
            ImGui::TextDisabled("  CachedLocalAtoms %d %llu/%llu | ParentAnim 0x%llX mode %d map %d | Attached 0x%llX",
                diag.buffers[3].boneCount,
                static_cast<unsigned long long>(diag.buffers[3].changedSamples), static_cast<unsigned long long>(diag.buffers[3].samples),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.parentAnimComponent)), diag.parentAnimComponentMode,
                diag.parentBoneMapCount, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.attachedToSkelComponent)));
            ImGui::TextDisabled("  AnimTree 0x%llX Anim 0x%llX PoseNode 0x%llX | Tick %d/%d weights %d controls %d postfix %d",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.animTreeTemplate)),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.animations)),
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(diag.animNodePose)),
                diag.animTickCount, diag.animAlwaysTickCount, diag.animTickWeightsCount,
                diag.skelControlCount, diag.postFixerCount);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Selected bone target — Component space");
        const bool targetEditable = poseInputReady && transform.selectedBone == selected && transform.validBySpace[1];
        ImGui::BeginDisabled(!targetEditable);
        if (ImGui::InputFloat3("Position XYZ", poseAbsoluteInput, "%.2f")) {
            for (int axis = 0; axis < 3; ++axis)
                BatLitNative::g_poseAbsolutePosition[axis].store(
                    (std::max)(-100000.0f, (std::min)(100000.0f, poseAbsoluteInput[axis])));
            if (BatLitNative::g_poseFreezeDesired.load())
                BatLitNative::g_poseEditDesired.store(true);
        }
        if (ImGui::InputFloat3("Rotation Pitch / Yaw / Roll (deg)", poseAbsoluteRotationInput, "%.1f")) {
            for (int axis = 0; axis < 3; ++axis)
                BatLitNative::g_poseAbsoluteRotationDegrees[axis].store(
                    (std::max)(-360.0f, (std::min)(360.0f, poseAbsoluteRotationInput[axis])));
            if (BatLitNative::g_poseFreezeDesired.load())
                BatLitNative::g_poseEditDesired.store(true);
        }
        if (ImGui::Button("Reset target to read pose")) {
            memcpy(poseAbsoluteInput, poseReadPosition, sizeof(poseAbsoluteInput));
            memcpy(poseAbsoluteRotationInput, poseReadRotation, sizeof(poseAbsoluteRotationInput));
            for (int axis = 0; axis < 3; ++axis) {
                BatLitNative::g_poseAbsolutePosition[axis].store(poseAbsoluteInput[axis]);
                BatLitNative::g_poseAbsoluteRotationDegrees[axis].store(poseAbsoluteRotationInput[axis]);
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Edit the displayed component-space coordinates directly; changes update live while Freeze is on.");
        bool editDesired = BatLitNative::g_poseEditDesired.load();
        bool applyToSubtree = BatLitNative::g_poseApplyToSubtreeDesired.load();
        if (ImGui::Checkbox("Include descendants (move whole branch)", &applyToSubtree)) {
            BatLitNative::g_poseApplyToSubtreeDesired.store(applyToSubtree);
        }
        int branchCount = 0;
        {
            std::lock_guard<std::mutex> lock(BatLitNative::g_poseMutex);
            branchCount = BatLitNative::g_poseSelectedSubtreeRoot == selected
                ? BatLitNative::g_poseSelectedSubtreeCount : 0;
        }
        ImGui::TextDisabled("Selected branch: %d bones", branchCount);
        if (ImGui::Checkbox("Apply pose target to selected bone / branch", &editDesired)) {
            BatLitNative::g_poseEditDesired.store(editDesired);
            if (!editDesired) BatLitNative::g_poseEditActive.store(false);
        }
        ImGui::TextDisabled("Post-tick calls %llu | Pawn matches %llu | valid arrays %llu | frozen meshes %d / bones %d | writes %llu",
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizeCallCount.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizeMatchCount.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseArrayReadyCount.load()),
                BatLitNative::g_poseFrozenComponentCount.load(),
                BatLitNative::g_poseFrozenBoneCount.load(),
                static_cast<unsigned long long>(BatLitNative::g_poseFreezeWriteCount.load()));
        ImGui::TextDisabled("Rocksteady +3288A0 calls / tracked buffers / combines skipped: %llu / %llu / %llu",
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesCombineCalls.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesCombineMatches.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesCombineWrites.load()));
        ImGui::TextDisabled("Rocksteady LocalAtoms +2F7370 calls / target matches / snapshot writes: %llu / %llu / %llu",
                static_cast<unsigned long long>(BatLitNative::g_poseLocalAtomsUpdateCalls.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseLocalAtomsUpdateMatches.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseLocalAtomsUpdateWrites.load()));
        ImGui::TextDisabled("SpaceBases reader +328CB0 calls / Pawn-array matches: %llu / %llu | in 0x%llX -> out 0x%llX",
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesReaderCalls.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesReaderMatches.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesReaderLastInput.load()),
                static_cast<unsigned long long>(BatLitNative::g_poseSpaceBasesReaderLastOutput.load()));
        for (size_t i = 0; i < BatLitNative::g_poseSpaceBasesReaderStack.size(); ++i) {
            const uintptr_t address = BatLitNative::g_poseSpaceBasesReaderStack[i].load(std::memory_order_relaxed);
            if (!address) continue;
            const uintptr_t rva = address >= BatLitNative::g_gameBase ? address - BatLitNative::g_gameBase : 0;
            ImGui::TextDisabled("+328CB0 caller frame %llu: 0x%llX | RVA +0x%llX",
                static_cast<unsigned long long>(i), static_cast<unsigned long long>(address),
                static_cast<unsigned long long>(rva));
        }
        ImGui::TextDisabled("Handlers 1 / 2 / 5: %llu / %llu / %llu | dispatcher: %llu   Hook mask: 0x%X   Bone edits: %llu",
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizerVariantCalls[0].load()),
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizerVariantCalls[1].load()),
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizerVariantCalls[2].load()),
                static_cast<unsigned long long>(BatLitNative::g_poseFinalizerVariantCalls[3].load()),
                BatLitNative::g_poseFinalizerHookMask.load(),
                static_cast<unsigned long long>(BatLitNative::g_poseEditAppliedCount.load()));
        ImGui::TextDisabled("ParentBoneMap branch edits: %d child meshes / %d bones",
            BatLitNative::g_poseMappedChildComponentCount.load(),
            BatLitNative::g_poseMappedChildBoneCount.load());
        ImGui::TextDisabled("Dispatcher modes all 0..5/? %llu / %llu / %llu / %llu / %llu / %llu / %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[1].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[2].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[3].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[4].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[5].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherModeCalls[6].load()));
        ImGui::TextDisabled("Dispatcher modes selected mesh 0..5/? %llu / %llu / %llu / %llu / %llu / %llu / %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[1].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[2].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[3].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[4].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[5].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherModeCalls[6].load()));
        ImGui::TextDisabled("Evaluator +304340 arg 0/1 calls all: %llu / %llu | selected: %llu / %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseControlEvaluatorCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseControlEvaluatorCalls[1].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedControlEvaluatorCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedControlEvaluatorCalls[1].load()));
        ImGui::TextDisabled("Evaluator selected SpaceBases changed inside call, arg 0/1: %llu / %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedControlEvaluatorChangedCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedControlEvaluatorChangedCalls[1].load()));
        ImGui::TextDisabled("Selected dispatcher buffer changes: engine update %llu | pose writer %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherOriginalChanges.load()),
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherWriterChanges.load()));
        ImGui::TextDisabled("Selected post-writer hash changes between calls: %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseSelectedDispatcherPostWriterChanges.load()));
        ImGui::TextDisabled("Dispatcher callers [E37DC / 34DF53 / 95B9E7 / 95F1C4 / other]: %llu / %llu / %llu / %llu / %llu",
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallerCalls[0].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallerCalls[1].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallerCalls[2].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallerCalls[3].load()),
            static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallerCalls[4].load()));
        for (int i = 0; i < 8; ++i) {
            const uintptr_t address = BatLitNative::g_poseDispatcherCallers[i].address.load(std::memory_order_relaxed);
            if (!address) continue;
            const uintptr_t rva = address >= BatLitNative::g_gameBase ? address - BatLitNative::g_gameBase : 0;
            ImGui::TextDisabled("Live caller %d: return 0x%llX | RVA +0x%llX | calls %llu", i + 1,
                static_cast<unsigned long long>(address), static_cast<unsigned long long>(rva),
                static_cast<unsigned long long>(BatLitNative::g_poseDispatcherCallers[i].calls.load()));
        }
        const uint64_t callerOverflow = BatLitNative::g_poseDispatcherCallerOverflow.load();
        if (callerOverflow) ImGui::TextDisabled("Untracked dispatcher caller entries: %llu", static_cast<unsigned long long>(callerOverflow));
        const int writerFailure = BatLitNative::g_poseFinalizeFailure.load();
        if (writerFailure == 1) ImGui::TextDisabled("Pose writer diagnostic: selected mesh is unavailable or not a SkeletalMeshComponent.");
        else if (writerFailure == 2) ImGui::TextDisabled("Pose writer diagnostic: no valid writable SpaceBases arrays were found on the Pawn.");
        else if (writerFailure == 3) ImGui::TextDisabled("Pose writer diagnostic: SpaceBases memory is not writable.");
        else if (writerFailure == 4) ImGui::TextDisabled("Pose writer diagnostic: selected bone is outside SpaceBases.");
        else if (writerFailure == 5) ImGui::TextDisabled("Pose writer diagnostic: no verified Steam SpaceBases handler signature matched.");
        else if (writerFailure == 6) ImGui::TextDisabled("Pose writer diagnostic: a SpaceBases handler hook could not be installed.");
        else if (writerFailure == 7) ImGui::TextDisabled("Pose writer diagnostic: finalizer hooks currently support the Steam game build.");
        if (freezeDesired || BatLitNative::g_poseEditDesired.load())
        ImGui::TextWrapped("Freeze restores captured LocalAtoms after Rocksteady's control update helper (+0x2F7370). Tracing now records when the Rocksteady +0x328CB0 helper reads a frozen Pawn SpaceBases buffer, the matching output array, and its runtime caller stack. Pose targets remain applied after EngineTick until the correct render reader is confirmed.");
        if (!anySpaceValid && transform.rotationValid) {
                constexpr float kRotatorUnitsToDegrees = 360.0f / 65536.0f;
                ImGui::Text("Matrix fallback rotation  Pitch %.2f   Yaw %.2f   Roll %.2f degrees",
                    transform.rotation.pitch * kRotatorUnitsToDegrees,
                    transform.rotation.yaw * kRotatorUnitsToDegrees,
                    transform.rotation.roll * kRotatorUnitsToDegrees);
        } else if (!anySpaceValid) {
            ImGui::TextDisabled("No valid transform returned for spaces 0, 1, or 2.");
        }
    }
    ImGui::TextWrapped("Pawn pose inspection. World (0) and Component (1) match observed bone-space results. The opt-in freeze/edit controls operate on the evaluated SpaceBases array; turning them off restores animation.");
}

void DrawAboutTab() {
    ImGui::TextUnformatted("Shortcuts");
    ImGui::Separator();

    bool hotkeysChanged = false;
    hotkeysChanged |= DrawHotkeyCapture("Open / Close UI", 1, g_hotkeyToggleUI);
    hotkeysChanged |= DrawHotkeyCapture("Small Lightning", 2, g_hotkeySmallLightning);
    hotkeysChanged |= DrawHotkeyCapture("Vertical / Big Lightning", 3, g_hotkeyVerticalLightning);

    if (hotkeysChanged)
        SaveHotkeyConfig();

    if (g_hotkeyToggleUI == g_hotkeySmallLightning ||
        g_hotkeyToggleUI == g_hotkeyVerticalLightning ||
        g_hotkeySmallLightning == g_hotkeyVerticalLightning) {
        ImGui::TextDisabled("Warning: two actions currently use the same key.");
    }

    ImGui::TextDisabled("Click Change, then press any keyboard key.");
    ImGui::TextDisabled("Saved automatically to BAKTweaks.ini");
    ImGui::Spacing();

    ImGui::Text("BAK Tweaks %s", kBAKTweaksVersion);
    if (BatLitNative::g_buildProfile) ImGui::TextDisabled("%s", BatLitNative::g_buildProfile->name);
    ImGui::Separator();
    ImGui::TextWrapped("Batman: Arkham Knight runtime visual and physics tweaks.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Cape Controls");
    ImGui::Separator();

    ImGui::TextUnformatted("Wind Direction X / Y / Z");
    ImGui::TextDisabled("Overrides the local-space wind direction applied to the cape.");

    ImGui::TextUnformatted("Minimum Wind Speed");
    ImGui::TextDisabled("Lower bound of the wind speed used by the cape simulation.");

    ImGui::TextUnformatted("Maximum Wind Speed");
    ImGui::TextDisabled("Upper bound of the wind speed used by the cape simulation.");

    ImGui::TextUnformatted("Upward Wind Scale");
    ImGui::TextDisabled("Scales the upward component of the cape wind response.");

    ImGui::TextUnformatted("Force Strength");
    ImGui::TextDisabled("Global multiplier applied to cape forces.");

    ImGui::TextUnformatted("Maximum Force");
    ImGui::TextDisabled("Caps the maximum force applied to the cape.");

    ImGui::TextUnformatted("Minimum Chain Force");
    ImGui::TextDisabled("Minimum positional-force multiplier used along the cape chain.");

    ImGui::TextUnformatted("Chain Force Along Cape");
    ImGui::TextDisabled("Controls how positional force changes over the length of the cape.");

    ImGui::TextUnformatted("Overstretch Distance");
    ImGui::TextDisabled("Overrides the distance threshold used by overstretch handling.");

    ImGui::Spacing();
    ImGui::TextUnformatted("BatLit / Lighting");
    ImGui::Separator();

    ImGui::TextWrapped("BatLit creates independent runtime Point and Spot lights. Each created light gets its own editor, so color, intensity, radius and placement can be adjusted without changing the other BatLit lights.");

    ImGui::TextUnformatted("Create Spot / Create Point");
    ImGui::TextDisabled("Spot lights are directional and use cone angles. Point lights emit in every direction and do not use Spot cone controls.");

    ImGui::TextUnformatted("Enabled");
    ImGui::TextDisabled("Turns the selected light on or off without deleting its BatLit record.");

    ImGui::TextUnformatted("Color / Intensity");
    ImGui::TextDisabled("Color sets the RGB light color. Intensity controls the brightness of the selected light.");

    ImGui::TextUnformatted("Radius");
    ImGui::TextDisabled("Controls the maximum area reached by the light. Larger values illuminate a wider part of the scene.");

    ImGui::TextUnformatted("Light Bulb Radius");
    ImGui::TextDisabled("Controls the apparent size of the light source used by the light component.");

    ImGui::TextUnformatted("Falloff Exponent / Quadratic Falloff");
    ImGui::TextDisabled("Controls how quickly the light loses intensity with distance. Quadratic Falloff switches to the engine's inverse-square style falloff behaviour.");

    ImGui::TextUnformatted("Position and Rotation");
    ImGui::TextDisabled("X / Y / Z move the light. Spot lights also expose Pitch, Yaw and Roll to aim the light, plus Inner, Outer and Light Shaft cone angles.");

    ImGui::TextUnformatted("Move to Camera");
    ImGui::TextDisabled("Places the selected light at the current camera position and orientation for quick photo-light placement.");

    ImGui::TextUnformatted("Live editing / Reset");
    ImGui::TextDisabled("Edits update the light automatically, including while world time is paused. Reset restores the captured values.");

    ImGui::TextWrapped("Shadow and other advanced lighting controls are intentionally hidden for now because they are not yet reliable in Arkham Knight.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("Original game values are captured automatically when BAK Tweaks initializes. Reset buttons restore those captured session values.");
}

void ConfigureBAKTweaksStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.ChildRounding = 4.0f;
    st.FrameRounding = 4.0f;
    st.PopupRounding = 4.0f;
    st.ScrollbarRounding = 6.0f;
    st.GrabRounding = 4.0f;
    st.TabRounding = 4.0f;
    st.WindowBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.TabBorderSize = 0.0f;
    st.ItemSpacing = ImVec2(8.0f, 6.0f);
    st.FramePadding = ImVec2(8.0f, 5.0f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.88f,0.88f,0.88f,1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.52f,0.52f,0.52f,1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.025f,0.025f,0.025f,0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.045f,0.045f,0.045f,0.82f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.035f,0.035f,0.035f,0.98f);
    c[ImGuiCol_Border]               = ImVec4(0.33f,0.33f,0.33f,0.55f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.10f,0.10f,0.10f,0.95f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.16f,0.16f,0.16f,1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.21f,0.21f,0.21f,1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.025f,0.025f,0.025f,1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.055f,0.055f,0.055f,1.00f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.05f,0.05f,0.05f,1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.03f,0.03f,0.03f,0.80f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f,0.22f,0.22f,0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f,0.30f,0.30f,1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f,0.38f,0.38f,1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.86f,0.86f,0.86f,1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.45f,0.45f,0.45f,1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.62f,0.62f,0.62f,1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.12f,0.12f,0.12f,1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.22f,0.22f,0.22f,1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.30f,0.30f,0.30f,1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.11f,0.11f,0.11f,0.92f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.20f,0.20f,0.20f,1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.28f,0.28f,0.28f,1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.33f,0.33f,0.33f,0.42f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.45f,0.45f,0.45f,0.70f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.58f,0.58f,0.58f,0.85f);
    c[ImGuiCol_Tab]                  = ImVec4(0.08f,0.08f,0.08f,1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.24f,0.24f,0.24f,1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.33f,0.33f,0.33f,1.00f);
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.78f,0.78f,0.78f,1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.06f,0.06f,0.06f,1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.18f,0.18f,0.18f,1.00f);
}

void ApplyUiScale() {
    if (!g_baseStyleCaptured) return;
    if (g_lastUiScale == g_uiScale) return;

    ImGui::GetStyle() = g_baseStyle;
    ImGui::GetStyle().ScaleAllSizes(g_uiScale);
    ImGui::GetIO().FontGlobalScale = g_uiScale;
    g_lastUiScale = g_uiScale;
}

void DrawUi() {
    if (!g_uiOpen.load()) return;
    ApplyUiScale();
    ImGui::SetNextWindowSize(ImVec2(760.0f * g_uiScale, 650.0f * g_uiScale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.96f);
    char windowTitle[96]{};
    snprintf(windowTitle, sizeof(windowTitle), "BAK Tweaks %s###BAKTweaks", kBAKTweaksVersion);
    if (ImGui::Begin(windowTitle, nullptr, ImGuiWindowFlags_NoCollapse)) {
        {
        const std::string toggleName = VirtualKeyName(g_hotkeyToggleUI);
        ImGui::TextDisabled("%s - Show / Hide", toggleName.c_str());
    }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f * g_uiScale);
        ImGui::SliderFloat("UI Scale", &g_uiScaleDraft, 1.0f, 3.0f, "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            g_uiScale = g_uiScaleDraft;
            ApplyUiScale();
        }
        ImGui::SameLine(ImGui::GetWindowWidth() - (150.0f * g_uiScale));
        if (ImGui::Button("Reset Global")) ResetGlobal();
        ImGui::Separator();
        if (ImGui::BeginTabBar("##BAKTweaksTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem("Cape")) { DrawCapeTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Pose")) { DrawPoseTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Environment")) { DrawEnvironmentTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("BatLit")) { DrawBatLitTab(); ImGui::EndTabItem(); }

        if (ImGui::BeginTabItem("About"))
        {
            DrawAboutTab();
            ImGui::EndTabItem();
        }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    DrawBatLitEditors();
}

HRESULT __stdcall HookPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags) {
    if (!g_presentSeen.exchange(true)) Log(L"HookPresent: first in-game Present hit");

    // The configured UI-toggle shortcut has a single owner: async polling in Present.
    // This is intentionally independent of the game's WndProc/raw-input path,
    // which is not reliable for this shortcut in Arkham Knight and can change after a
    // resolution/fullscreen switch.
    static bool f10WasDown = false;
    const bool f10Down = (GetAsyncKeyState(g_hotkeyToggleUI) & 0x8000) != 0;
    if (g_hotkeyCaptureTarget == 0 && f10Down && !f10WasDown) {
        const bool wasOpen = g_uiOpen.load();
        const bool nowOpen = !wasOpen;
        g_uiOpen.store(nowOpen);

        if (!nowOpen) {
            // Win32/API hooks stop filtering immediately because g_uiOpen is now false.
            // UE3 ProcessEvent calls must stay on the game thread, so request a hard
            // IgnoreMove/Look/Button(false) + ResetPlayerMovementInput on the next tick.
            BatLitNative::g_forceInputRelease.store(true);
            ReleaseCapture();
        }

        if (g_imguiReady) MaintainUiInputCapture();
        Log(nowOpen ? L"Shortcut: UI opened" : L"Shortcut: UI closed - input release requested");
    }
    f10WasDown = f10Down;

    // Lightning shortcuts remain available while the interface is hidden.
    // F6 = Small Lightning, F7 = Vertical / Big Lightning.
    // They use the exact values currently configured in the Lightning panel.
    static bool f6WasDown = false;
    static bool f7WasDown = false;

    const bool uiClosed = !g_uiOpen.load();
    const bool rebindingHotkey = (g_hotkeyCaptureTarget != 0);
    const bool f6Down = (GetAsyncKeyState(g_hotkeySmallLightning) & 0x8000) != 0;
    const bool f7Down = (GetAsyncKeyState(g_hotkeyVerticalLightning) & 0x8000) != 0;

    if (!rebindingHotkey && uiClosed && f6Down && !f6WasDown) {
        BatLitNative::QueueLightningTrigger(
            false,
            BatLitNative::g_smallLightningDuration,
            BatLitNative::g_smallLightningDistance,
            BatLitNative::g_smallLightningInstances,
            BatLitNative::g_smallLightningSalvoCount,
            BatLitNative::g_smallLightningSalvoInterval,
            BatLitNative::g_smallLightningClusters,
            BatLitNative::g_smallLightningClusterSpread,
            BatLitNative::g_lightningRandomizePosition,
            BatLitNative::g_lightningSpreadX,
            BatLitNative::g_lightningSpreadY,
            BatLitNative::g_lightningDistanceJitter);
        Log(L"Shortcut: Small Lightning triggered");
    }

    if (!rebindingHotkey && uiClosed && f7Down && !f7WasDown) {
        BatLitNative::QueueLightningTrigger(
            true,
            BatLitNative::g_verticalLightningDuration,
            BatLitNative::g_verticalLightningDistance,
            BatLitNative::g_verticalLightningInstances,
            BatLitNative::g_verticalLightningSalvoCount,
            BatLitNative::g_verticalLightningSalvoInterval,
            BatLitNative::g_verticalLightningClusters,
            BatLitNative::g_verticalLightningClusterSpread,
            BatLitNative::g_lightningRandomizePosition,
            BatLitNative::g_lightningSpreadX,
            BatLitNative::g_lightningSpreadY,
            BatLitNative::g_lightningDistanceJitter);
        Log(L"Shortcut: Vertical Lightning triggered");
    }

    f6WasDown = f6Down;
    f7WasDown = f7Down;

    if (!g_imguiReady) {
        if (SUCCEEDED(swap->GetDevice(IID_PPV_ARGS(&g_device)))) {
            g_device->GetImmediateContext(&g_context);
            DXGI_SWAP_CHAIN_DESC desc{};
            swap->GetDesc(&desc);
            g_hwnd = desc.OutputWindow;
            CreateRenderTarget(swap);
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            ConfigureBAKTweaksStyle();
            g_baseStyle = ImGui::GetStyle();
            g_baseStyleCaptured = true;
            ApplyUiScale();
            ImGui_ImplWin32_Init(g_hwnd);
            ImGui_ImplDX11_Init(g_device, g_context);
            g_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            g_imguiReady = true;
            MaintainUiInputCapture();
            wchar_t loadedMessage[128]{};
            swprintf_s(loadedMessage, L"BAK Tweaks %hs POSE FOCUS Steam/Epic loaded", kBAKTweaksVersion);
            Log(loadedMessage);
            Log(L"ImGui initialized on game swap chain");
        }
    }

    if (g_imguiReady) {
        // Do not build/render an ImGui frame while the interface is hidden unless
        // the user explicitly wants the light gizmo visible in-game.
        const bool needOverlayFrame =
            g_uiOpen.load() || HasVisibleLightGizmos();

        if (needOverlayFrame) {
            if (g_uiOpen.load()) MaintainUiInputCapture();

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            DrawUi();
            DrawLightGizmos();

            ImGui::Render();
            if (g_rtv) {
                g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            if (g_uiOpen.load()) MaintainUiInputCapture();
        }
    }
    return g_originalPresent(swap, syncInterval, flags);
}


HRESULT __stdcall HookResizeBuffers(IDXGISwapChain* swap, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
    if (g_imguiReady && g_context) {
        g_context->OMSetRenderTargets(0, nullptr, nullptr);
        ImGui_ImplDX11_InvalidateDeviceObjects();
    }
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }

    const HRESULT hr = g_originalResizeBuffers(swap, bufferCount, width, height, newFormat, swapChainFlags);
    if (SUCCEEDED(hr) && g_imguiReady && g_device) {
        CreateRenderTarget(swap);
        ImGui_ImplDX11_CreateDeviceObjects();
        RebindGameWindowAfterResize(swap);
    }
    return hr;
}

bool HookD3D11Present() {
    Log(L"HookD3D11Present: starting");
    // Hidden helper HWND used only once to discover the D3D11 swap-chain vtable.
    // It is never shown. The actual UI is rendered inside BatmanAK.exe Present().
    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"BAKTweaksHiddenD3D11Probe", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, -32000, -32000, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL level{};
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; IDXGISwapChain* sc = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &level, &ctx);
    if (FAILED(hr)) hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &level, &ctx);
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vtbl = *reinterpret_cast<void***>(sc);
        const MH_STATUS initStatus = MH_Initialize();
        if (initStatus == MH_OK || initStatus == MH_ERROR_ALREADY_INITIALIZED) {
            const MH_STATUS presentStatus = MH_CreateHook(vtbl[8], &HookPresent, reinterpret_cast<void**>(&g_originalPresent));
            const MH_STATUS resizeStatus  = MH_CreateHook(vtbl[13], &HookResizeBuffers, reinterpret_cast<void**>(&g_originalResizeBuffers));
            const bool presentCreated = (presentStatus == MH_OK || presentStatus == MH_ERROR_ALREADY_CREATED);
            const bool resizeCreated  = (resizeStatus == MH_OK || resizeStatus == MH_ERROR_ALREADY_CREATED);
            if (presentCreated && resizeCreated && MH_EnableHook(MH_ALL_HOOKS) == MH_OK) { ok = true; Log(L"D3D11 hooks enabled"); }
        }
    }
    if (ctx) ctx->Release(); if (dev) dev->Release(); if (sc) sc->Release();
    DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    Log(ok ? L"HookD3D11Present: success" : L"HookD3D11Present: FAILED");
    return ok;
}

DWORD WINAPI WorkerThread(void*) {
    ResetSessionLog();
    LoadHotkeyConfig();
    Log(L"BAK Tweaks worker started");
    BatLitNative::LoadPoseGlobalFNameDump();
    InstallHardInputBlockHooks();
    InstallCapeResolverHook();
    Log(L"Cape resolver hook attempted");
    if (BatLitNative::Init() && BatLitNative::HookTick()) Log(L"BatLit SAFE: game-thread hook enabled");
    else Log(L"BatLit SAFE: game-thread hook FAILED");
    HookD3D11Present();
    DWORD lastEnvRefresh = 0;
    DWORD lastWeatherParticleRefresh = 0;
    while (g_running.load()) {
        CaptureCapeDefaults();
        const DWORD now = GetTickCount();
        if (now - lastWeatherParticleRefresh >= 1000) {
            RefreshWeatherParticleComponentsFast();
            lastWeatherParticleRefresh = now;
        }
        if (now - lastEnvRefresh >= 5000) {
            RefreshLocationDependentParticles();
            lastEnvRefresh = now;
        }
        Sleep(50);
    }
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) uintptr_t __stdcall BAKTweaks_GetCapeBase() { return g_capeBase.load(); }
extern "C" __declspec(dllexport) uintptr_t __stdcall BAKTweaks_GetRainEngineAddress() { return g_rainEngine.load(); }
extern "C" __declspec(dllexport) uintptr_t __stdcall BAKTweaks_GetRainConsoleAddress() { return 0; }
extern "C" __declspec(dllexport) void __stdcall BAKTweaks_ResetCape() { ResetCape(); }
extern "C" __declspec(dllexport) void __stdcall BAKTweaks_ResetRain() { ResetRain(); }
extern "C" __declspec(dllexport) void __stdcall BAKTweaks_ResetGlobal() { ResetGlobal(); }
extern "C" __declspec(dllexport) BOOL __stdcall BAKTweaks_RequestPoseProbe() {
    BatLitNative::g_poseProbeRequested.store(true);
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
