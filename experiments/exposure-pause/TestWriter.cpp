#include <atomic>
#include <cstdint>
#include <cstdio>
#include "MinHook.h"
extern "C" {
std::atomic<uintptr_t> g_bakExposureLevelTarget{}, g_bakExposureCurrentTarget{};
std::atomic<float> g_bakExposureDesired{0.35f};
void BakTestStore();
void BakExposureWriterHook();
uintptr_t g_bakExposureWriterContinue = reinterpret_cast<uintptr_t>(&BakTestStore) + 8;
int BakTestWrite(void*, float);
}
struct Cache { unsigned char padding[0x164]{}; float value{}; };
int main() {
    void* trampoline{};
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(&BakTestStore, &BakExposureWriterHook, &trampoline) != MH_OK ||
        MH_EnableHook(&BakTestStore) != MH_OK) return 10;
    Cache level{}, current{}, unrelated{};
    g_bakExposureLevelTarget = reinterpret_cast<uintptr_t>(&level.value);
    g_bakExposureCurrentTarget = reinterpret_cast<uintptr_t>(&current.value);
    if (!BakTestWrite(&level, 9.0f) || level.value != 0.35f) return 1;
    if (!BakTestWrite(&current, -2.0f) || current.value != 0.35f) return 2;
    if (!BakTestWrite(&unrelated, 9.0f) || unrelated.value != 9.0f) return 3;
    g_bakExposureDesired = -1.5f;
    if (!BakTestWrite(&current, 8.0f) || current.value != -1.5f) return 4;
    g_bakExposureLevelTarget = 0; g_bakExposureCurrentTarget = 0;
    if (!BakTestWrite(&level, 7.0f) || level.value != 7.0f) return 5;
    if (MH_DisableHook(&BakTestStore) != MH_OK) return 11;
    if (!BakTestWrite(&current, 6.0f) || current.value != 6.0f) return 6;
    MH_Uninitialize();
    puts("PASS: real MinHook install/restore, LevelPP, CurrentPP, unrelated stores, value change, disabled targets; RAX/RDX/RFLAGS/XMM1 preserved.");
}
