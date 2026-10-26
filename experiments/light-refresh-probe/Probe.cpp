#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstddef>
#include <cwchar>
#include "MinHook.h"

// One-shot, Steam-only experiment. It chains the existing BAKTweaks tick hook,
// operates only on its own temporary actors, then restores the previous hook.
struct Vec { float x,y,z; };
struct Rot { int32_t pitch,yaw,roll; };
struct Name { int32_t index,number; };
using FindFn=void*(__cdecl*)(void*,void*,const wchar_t*,int);
using EventFn=void(__fastcall*)(void*,void*,void*,void*);
using DefaultFn=void*(__fastcall*)(void*,int);
using SpawnFn=void*(__fastcall*)(void*,void*,Name,Vec*,Rot*,void*,int,int,void*,void*,int,void*);
using TickFn=intptr_t(__fastcall*)(void*);
uintptr_t base{}; HMODULE module{}; wchar_t logPath[MAX_PATH]{};
FindFn findObject{}; EventFn eventCall{}; DefaultFn defaultObject{}; SpawnFn spawn{}; TickFn previousTick{};
void* functionClass{}; void* world{}; void* queueFn{}; void* forceFn{}; void* locationFn{}; void* destroyFn{};
void* actors[2]{}; void* components[2]{};
std::atomic<bool> finished{false}; int stage=0; thread_local bool inside=false;

void Log(const char* format,...) {
    FILE* f{}; _wfopen_s(&f,logPath,L"a"); if(!f)return;
    va_list args; va_start(args,format); vfprintf(f,format,args); va_end(args); fputc('\n',f); fclose(f);
}
bool Readable(const void* p,size_t size) {
    MEMORY_BASIC_INFORMATION m{};
    return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&
        reinterpret_cast<uintptr_t>(p)+size<=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
}
template<typename T>T Read(void* p,ptrdiff_t off) {T v{};if(Readable(static_cast<char*>(p)+off,sizeof(v)))memcpy(&v,static_cast<char*>(p)+off,sizeof(v));return v;}
template<typename T>void Write(void* p,ptrdiff_t off,const T& v) {memcpy(static_cast<char*>(p)+off,&v,sizeof(v));}
void* Find(const wchar_t* name){return findObject(nullptr,reinterpret_cast<void*>(-1),name,0);}
void* Function(const wchar_t* name){return findObject(functionClass,reinterpret_cast<void*>(-1),name,1);}
bool Call(void* object,void* fn,void* params) {
    if(!object||!Readable(fn,0xAA))return false;
    uint32_t flags=Read<uint32_t>(fn,0xA4);uint16_t native=Read<uint16_t>(fn,0xA8);
    __try {
        Write(fn,0xA4,(flags&~0x400u)|2u);Write<uint16_t>(fn,0xA8,0);
        eventCall(object,fn,params,nullptr);Write(fn,0xA4,flags);Write(fn,0xA8,native);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Write(fn,0xA4,flags);Write(fn,0xA8,native);Log("CALL_EXCEPTION fn=%p code=%08lX",fn,GetExceptionCode());return false;
    }
}
bool Refresh(void* c,bool transformOnly) {
    int32_t p=transformOnly?1:0;
    bool q=Call(c,queueFn,&p);
    Log("QUEUE transformOnly=%d accepted=%d flags=%08X",transformOnly,q,Read<uint32_t>(c,0x74));
    bool f=Call(c,forceFn,&p);
    Log("FORCE transformOnly=%d accepted=%d flags=%08X sceneInfo=%p",transformOnly,f,Read<uint32_t>(c,0x74),Read<void*>(c,0x110));
    return q&&f;
}
void Cleanup() {
    for(int i=0;i<2;++i)if(actors[i]){
        void* a=actors[i];void* c=components[i];
        // Only our own spawned/recovered actors are accepted by this experiment.
        if(!Readable(a,0x104)||!Readable(c,0x334)||Read<void*>(c,0x6C)!=a){Log("CLEANUP_INVALID_OWNER");continue;}
        uint32_t flags=Read<uint32_t>(a,0x100);
        int32_t off=0;bool disabled=Call(c,Function(L"Engine.LightComponent.SetEnabled"),&off);Refresh(c,false);
        if(flags&0x10){Log("CLEANUP_STATIC_REFUSED disabled=%d",disabled);continue;}
        // Movable Light defaults can still carry bNoDelete. Clear it only on our
        // temporary actor, never on the class default or any existing game light.
        Write(a,0x100,flags&~0x80u);
        uint32_t result{};bool ok=Call(a,destroyFn,&result);
        Log("CLEANUP type=%s actor=%p before=%08X disabled=%d call=%d result=%u deleteMe=%d",i?"spot":"point",a,flags,disabled,ok,result,(Read<uint32_t>(a,0x100)&0x100)!=0);
        if(!result)Write(a,0x100,Read<uint32_t>(a,0x100)|(flags&0x80u));
        actors[i]=nullptr;
    }
}
bool Close(Vec a,Vec b){return std::fabs(a.x-b.x)<0.1f&&std::fabs(a.y-b.y)<0.1f&&std::fabs(a.z-b.z)<0.1f;}
bool IsTempMatch(void* object,void* cls,const Vec& pos){
    __try {
        return object&&*reinterpret_cast<void**>(static_cast<char*>(object)+0x44)==cls&&
            !(*reinterpret_cast<uint32_t*>(static_cast<char*>(object)+0x08)&0x80)&&
            Close(*reinterpret_cast<Vec*>(static_cast<char*>(object)+0xC4),pos);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Snapshot(const char* label,int i) {
    Vec a=Read<Vec>(actors[i],0xC4),m=Read<Vec>(components[i],0x190);
    Log("%s type=%s actor=(%.3f,%.3f,%.3f) matrix=(%.3f,%.3f,%.3f) flags=%08X sceneInfo=%p ownerMatch=%d",label,i?"spot":"point",a.x,a.y,a.z,m.x,m.y,m.z,Read<uint32_t>(components[i],0x74),Read<void*>(components[i],0x110),Read<void*>(components[i],0x6C)==actors[i]);
}
bool Prepare() {
    functionClass=Find(L"Core.Function");
    queueFn=Function(L"Engine.ActorComponent.QueueReattach");forceFn=Function(L"Engine.ActorComponent.ForceUpdate");
    locationFn=Function(L"Engine.Actor.SetLocation");destroyFn=Function(L"Engine.Actor.Destroy");
    void* qp=Find(L"Engine.ActorComponent.QueueReattach.bTransformOnly");
    void* fp=Find(L"Engine.ActorComponent.ForceUpdate.bTransformOnly");
    Log("METHODS queue=%p force=%p queueSize=%u forceSize=%u paramOffsets=%u,%u",queueFn,forceFn,Read<uint16_t>(queueFn,120),Read<uint16_t>(forceFn,120),Read<uint16_t>(qp,0x6A),Read<uint16_t>(fp,0x6A));
    if(!queueFn||!forceFn||!locationFn||!destroyFn||!qp||!fp||Read<uint16_t>(queueFn,120)!=4||Read<uint16_t>(forceFn,120)!=4||Read<uint16_t>(qp,0x6A)!=0||Read<uint16_t>(fp,0x6A)!=0)return false;
    void* wi{};void* wiClass=Find(L"Engine.WorldInfo");
    if(!wiClass||!Call(defaultObject(wiClass,0),Function(L"Engine.WorldInfo.GetWorldInfo"),&wi)||!wi)return false;
    world=Read<void*>(Read<void*>(wi,0x34),0x34);if(!Readable(world,0x98))return false;
    // Same UWorld::IsPaused function whose interior IGCS intercepts at +A9FBD8.
    int paused=reinterpret_cast<int(__fastcall*)(void*)>(base+0xA9FAD0)(world);
    Log("CONTEXT thread=%lu world=%p worldInfo=%p isPaused=%d",GetCurrentThreadId(),world,wi,paused);
#ifdef PROBE_CLEANUP_ONLY
    const Vec positions[]={{-74389.234f,-55031.773f,6955.578f},{-74289.234f,-55031.773f,6955.578f}};
    void* types[]={Find(L"Engine.PointLightMovable"),Find(L"Engine.SpotLightMovable")};
    auto** objects=reinterpret_cast<void**>(base+0x340CBE4);
    int count=*reinterpret_cast<int*>(base+0x3A09F28);if(count<=0||count>785000)return false;
    for(int i=0;i<2;++i){
        int matches=0;for(int j=0;j<count;++j)if(IsTempMatch(objects[j],types[i],positions[i])){actors[i]=objects[j];++matches;}
        Log("RECOVER type=%s matches=%d actor=%p",i?"spot":"point",matches,actors[i]);
        if(matches>1){actors[i]=nullptr;return false;}
        if(actors[i])components[i]=Read<void*>(actors[i],0x29C);
    }
    return true;
#endif
    void* engine{};void* engineClass=Find(L"Engine.Engine");
    if(!engineClass||!Call(defaultObject(engineClass,0),Function(L"Engine.Engine.GetEngine"),&engine)||!engine)return false;
    struct LocalParams {int32_t id;char result[8];char spare[4];}lp{};
    if(!Call(Read<void*>(engine,700),Function(L"Engine.GameViewportClient.FindPlayerByControllerId"),&lp))return false;
    void* localPlayer{};memcpy(&localPlayer,lp.result,8);void* pc=Read<void*>(localPlayer,92);
    struct View {Vec position;Rot rotation;char spare[8];}view{};
    if(!Call(pc,Function(L"Engine.PlayerController.GetPlayerViewPoint"),&view))return false;
    void* lightTypes[]={Find(L"Engine.PointLightMovable"),Find(L"Engine.SpotLightMovable")};
    for(int i=0;i<2;++i){
        if(!lightTypes[i])return false;
        Vec position=view.position;position.z+=100;position.x+=200+100*i;Name name{};
        actors[i]=spawn(world,lightTypes[i],name,&position,&view.rotation,nullptr,1,0,nullptr,nullptr,1,nullptr);
        if(!Readable(actors[i],0x2A4))return false;components[i]=Read<void*>(actors[i],0x29C);
        if(!Readable(components[i],i?0x334:0x2DC)||Read<void*>(components[i],0x6C)!=actors[i])return false;
        Snapshot("SPAWN",i);
    }
    return true;
}
void RunTests() {
#ifdef PROBE_CLEANUP_ONLY
    Cleanup();Log("CLEANUP_COMPLETE");return;
#endif
    Log("TESTS_BEGIN isPaused=%d",reinterpret_cast<int(__fastcall*)(void*)>(base+0xA9FAD0)(world));
    for(int i=0;i<2;++i){
        void* c=components[i];void* a=actors[i];
        if(!Readable(c,i?0x334:0x2DC)||!Readable(a,0xE0)){Log("FAIL invalid temporary actor");continue;}
        if(!(Read<uint32_t>(c,0x74)&1)||!Read<void*>(c,0x110))Refresh(c,false);
        Vec original=Read<Vec>(a,0xC4),baseline=Read<Vec>(c,0x190);
        Vec desired{original.x+64,original.y-32,original.z+16};
        Vec expected{baseline.x+64,baseline.y-32,baseline.z+16};
        struct SetLocation {Vec location;uint32_t result;}p{desired,0};
        Snapshot("BASELINE",i);bool called=Call(a,locationFn,&p);
        Log("SET_LOCATION type=%s call=%d result=%u",i?"spot":"point",called,p.result);Snapshot("AFTER_SET_LOCATION",i);
        bool before=Close(Read<Vec>(c,0x190),expected);bool forceOk=Refresh(c,true);
        bool after=Close(Read<Vec>(c,0x190),expected);
        Log("RESULT_SET_LOCATION type=%s matrixBeforeForce=%d matrixAfterForce=%d calls=%d",i?"spot":"point",before,after,forceOk);Snapshot("AFTER_FORCE_TRANSFORM",i);
        // Independent test of the established BAC approach, on OUR OWN actor only.
        Write(a,0xC4,original);Refresh(c,true);Write(a,0xC4,desired);
        bool directBefore=Close(Read<Vec>(c,0x190),expected);bool directOk=Refresh(c,true);
        bool directAfter=Close(Read<Vec>(c,0x190),expected);
        Log("RESULT_DIRECT type=%s matrixBeforeForce=%d matrixAfterForce=%d calls=%d",i?"spot":"point",directBefore,directAfter,directOk);Snapshot("AFTER_DIRECT_FORCE",i);
        float radius=Read<float>(c,0x268),brightness=Read<float>(c,0x1F0);void* sceneBefore=Read<void*>(c,0x110);
        if(std::isfinite(radius)&&std::isfinite(brightness)){
            Write(c,0x268,radius+16.0f);Write(c,0x1F0,brightness+0.25f);bool full=Refresh(c,false);
            Log("RESULT_FULL type=%s calls=%d attached=%d pending=%d sceneBefore=%p sceneAfter=%p",i?"spot":"point",full,(Read<uint32_t>(c,0x74)&1)!=0,(Read<uint32_t>(c,0x74)&12)!=0,sceneBefore,Read<void*>(c,0x110));
            Write(c,0x268,radius);Write(c,0x1F0,brightness);
        }
        Write(a,0xC4,original);Refresh(c,false);
    }
    Cleanup();Log("TESTS_COMPLETE cleanup attempted; no exposure changes");
}
void SafeStep() {
    __try {
        if(stage==0){bool ok=Prepare();stage=ok?1:2;if(!ok){Log("PREPARE_FAILED");Cleanup();}}
        else if(stage==1){RunTests();stage=2;}
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("PROBE_EXCEPTION code=%08lX",GetExceptionCode());Cleanup();stage=2;}
}
intptr_t __fastcall HookTick(void* self) {
    if(!inside&&stage<2){inside=true;SafeStep();inside=false;}
    intptr_t result=previousTick?previousTick(self):0;
    if(stage==2)finished.store(true);
    return result;
}
DWORD WINAPI Worker(void*) {
    GetModuleFileNameW(module,logPath,MAX_PATH);wchar_t* dot=wcsrchr(logPath,L'.');if(dot)wcscpy_s(dot,static_cast<size_t>(MAX_PATH-(dot-logPath)),L".log");
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"BatmanAK.exe"));if(!base)return 0;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x5F8AA2F9||nt->OptionalHeader.SizeOfImage!=0x4398000){Log("UNSUPPORTED_BUILD");return 0;}
    findObject=reinterpret_cast<FindFn>(base+0xF3B7F0);eventCall=reinterpret_cast<EventFn>(base+0xF90CD0);
    defaultObject=reinterpret_cast<DefaultFn>(base+0xF92870);spawn=reinterpret_cast<SpawnFn>(base+0xACBC70);
    void* tick=reinterpret_cast<void*>(base+0x5B1DA0);
    MH_STATUS init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){Log("HOOK_INIT_FAILED %d",init);return 0;}
    MH_STATUS create=MH_CreateHook(tick,HookTick,reinterpret_cast<void**>(&previousTick));
    if(create!=MH_OK){Log("HOOK_CREATE_FAILED %d",create);return 0;}
    Log("PROBE_READY tick=%p previous=%p",tick,reinterpret_cast<void*>(previousTick));
    if(MH_EnableHook(tick)!=MH_OK){Log("HOOK_ENABLE_FAILED");return 0;}
    for(int i=0;!finished.load();++i){Sleep(100);if(i==299)Log("WAITING_FOR_GAME_TICK");}
    Log("HOOK_RESTORED status=%d",MH_DisableHook(tick));
    // Keep DLL/trampoline mapped: an in-flight call may still be returning.
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);HANDLE h=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);if(h)CloseHandle(h);}return TRUE;
}
