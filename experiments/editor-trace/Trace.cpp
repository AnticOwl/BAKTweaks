#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <vector>
#include <mutex>
#include "MinHook.h"
#include "TraceTypes.h"
#include "Offsets.h"
using namespace BakTrace;
using ApplyFn=void(*)(); using EditorFn=void(*)(LightRecord&);
ApplyFn originalApply{};EditorFn originalEditor{};
uintptr_t bak{},game{};HMODULE self{};wchar_t logPath[MAX_PATH]{};
std::mutex logMutex;
std::vector<LightRecord>& Lights(){return *reinterpret_cast<std::vector<LightRecord>*>(bak+k_lights);}
std::mutex& LightMutex(){return *reinterpret_cast<std::mutex*>(bak+k_mutex);}
void Log(const char* format,...){
    std::lock_guard<std::mutex> lock(logMutex);FILE* f{};_wfopen_s(&f,logPath,L"a");if(!f)return;
    fprintf(f,"%llu T%lu ",GetTickCount64(),GetCurrentThreadId());
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fputc('\n',f);fclose(f);
}
bool Readable(void* p,size_t size){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&reinterpret_cast<uintptr_t>(p)+size<=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;}
template<typename T>T Read(void* p,ptrdiff_t off){T v{};if(p&&Readable(static_cast<char*>(p)+off,sizeof(T)))memcpy(&v,static_cast<char*>(p)+off,sizeof(T));return v;}
struct Before {uint32_t id,dirty;Vec3 desired;float intensity,radius;};
void TraceApply(){
    std::vector<Before> before;
    {
        std::lock_guard<std::mutex> lock(LightMutex());
        for(auto& r:Lights())if(r.liveDirty){
            if(before.size()>=64)break;
            before.push_back({r.id,r.liveDirty,r.state.translation,r.state.intensity,r.state.radius});
            int paused=-1;
            if(Readable(r.spawnWorld,0x98))paused=reinterpret_cast<int(__fastcall*)(void*)>(game+0xA9FAD0)(r.spawnWorld);
            Log("ENGINE_IN id=%u name=%s mask=%08X valid=%d pending=%d paused=%d actor=%p component=%p desired=(%.3f,%.3f,%.3f)",r.id,r.name.c_str(),r.liveDirty,r.valid,r.statePending,paused,r.actor,r.component,r.state.translation.x,r.state.translation.y,r.state.translation.z);
        }
    }
    originalApply();
    if(before.empty())return;
    std::lock_guard<std::mutex> lock(LightMutex());
    for(auto& b:before)for(auto& r:Lights())if(r.id==b.id){
        Vec3 a=Read<Vec3>(r.actor,0xC4),m=Read<Vec3>(r.component,0x190);
        Log("ENGINE_OUT id=%u mask=%08X failed=%d state=(%.3f,%.3f,%.3f) actor=(%.3f,%.3f,%.3f) matrix=(%.3f,%.3f,%.3f) intensity=%.3f/%.3f radius=%.3f/%.3f flags=%08X scene=%p",r.id,r.liveDirty,r.liveEditFailed,r.state.translation.x,r.state.translation.y,r.state.translation.z,a.x,a.y,a.z,m.x,m.y,m.z,b.intensity,Read<float>(r.component,0x1F0),b.radius,Read<float>(r.component,0x268),Read<uint32_t>(r.component,0x74),Read<void*>(r.component,0x110));
    }
}
void TraceEditor(LightRecord& r){
    // DrawBatLitEditors already owns the BAKTweaks light mutex here.
    Vec3 xyz=r.state.translation;uint32_t dirty=r.liveDirty;float intensity=r.state.intensity,radius=r.state.radius;
    originalEditor(r);
    if(dirty!=r.liveDirty||xyz.x!=r.state.translation.x||xyz.y!=r.state.translation.y||xyz.z!=r.state.translation.z||intensity!=r.state.intensity||radius!=r.state.radius)
        Log("UI_EDIT id=%u name=%s mask=%08X->%08X valid=%d pending=%d old=(%.3f,%.3f,%.3f) desired=(%.3f,%.3f,%.3f) intensity=%.3f radius=%.3f",r.id,r.name.c_str(),dirty,r.liveDirty,r.valid,r.statePending,xyz.x,xyz.y,xyz.z,r.state.translation.x,r.state.translation.y,r.state.translation.z,r.state.intensity,r.state.radius);
}
DWORD WINAPI Worker(void*){
    GetModuleFileNameW(self,logPath,MAX_PATH);wchar_t* dot=wcsrchr(logPath,L'.');if(dot)wcscpy_s(dot,static_cast<size_t>(MAX_PATH-(dot-logPath)),L".log");
    bak=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"BAKTweaks.dll"));game=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"BatmanAK.exe"));
    if(!bak||!game){Log("REQUIRED_MODULE_MISSING");return 0;}
    auto* d=reinterpret_cast<IMAGE_DOS_HEADER*>(bak);auto* n=reinterpret_cast<IMAGE_NT_HEADERS64*>(bak+d->e_lfanew);
    auto* gd=reinterpret_cast<IMAGE_DOS_HEADER*>(game);auto* gn=reinterpret_cast<IMAGE_NT_HEADERS64*>(game+gd->e_lfanew);
    if(n->FileHeader.TimeDateStamp!=k_timestamp||n->OptionalHeader.SizeOfImage!=k_imageSize||gn->FileHeader.TimeDateStamp!=0x5F8AA2F9){Log("BUILD_MISMATCH");return 0;}
    void* apply=reinterpret_cast<void*>(bak+k_apply);void* editor=reinterpret_cast<void*>(bak+k_editor);
    if(memcmp(apply,k_applyBytes,sizeof(k_applyBytes))||memcmp(editor,k_editorBytes,sizeof(k_editorBytes))){Log("CODE_MISMATCH");return 0;}
    MH_STATUS init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){Log("INIT_FAILED");return 0;}
    if(MH_CreateHook(apply,TraceApply,reinterpret_cast<void**>(&originalApply))!=MH_OK||MH_CreateHook(editor,TraceEditor,reinterpret_cast<void**>(&originalEditor))!=MH_OK){Log("CREATE_FAILED");return 0;}
    if(MH_EnableHook(apply)!=MH_OK||MH_EnableHook(editor)!=MH_OK){MH_DisableHook(apply);MH_DisableHook(editor);Log("ENABLE_FAILED");return 0;}
    {
        std::lock_guard<std::mutex> lock(LightMutex());
        Log("TRACE_READY records=%zu recordSize=%zu stateSize=%zu dirtyOffset=%zu stateOffset=%zu",Lights().size(),sizeof(LightRecord),sizeof(LightState),offsetof(LightRecord,liveDirty),offsetof(LightRecord,state));
        for(auto& r:Lights())Log("EXISTING id=%u name=%s valid=%d pending=%d editorOpen=%d",r.id,r.name.c_str(),r.valid,r.statePending,r.editOpen);
    }
    // Passive trace only: never create actors, edit values or change exposure.
    Sleep(120000);
    auto a=MH_DisableHook(apply),e=MH_DisableHook(editor);Log("TRACE_STOPPED apply=%d editor=%d",a,e);
    // Leave trampolines mapped for any in-flight callback.
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){self=instance;DisableThreadLibraryCalls(instance);HANDLE h=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(h)CloseHandle(h);}return TRUE;}
