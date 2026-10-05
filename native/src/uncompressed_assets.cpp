#include "uncompressed_assets.hpp"
#include "asset_entry.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace rebirths {
namespace {
struct Target {uint32_t open,resolve,allocate,append,deleteSlot,openSeh,resolveSeh;};
constexpr Target targets[]={
    {0x264e20,0x2645d0,0x299df4,0x267bb0,0x32f34c,0x6ee205,0x6ee17e},
    {0x2ad660,0x2ad000,0x2eac98,0x2b0ab0,0x33b378,0x718b72,0x718aeb},
    {0x2ef390,0x2eed30,0x32d9f8,0x2f27e0,0x383378,0x75e792,0x75e70b},
    {0x2b9ea0,0x2b9840,0x2f8e20,0x2bd300,0x346384,0x7259c2,0x72593b},
};
struct State {assets::Store store;assets::NativeApi api;assets::NativeApi::Open original=nullptr;bool verify=false;std::atomic<bool> enabled{false};};
std::atomic<State*> active{nullptr};
// Internal diagnostic marker; no new proxy exports, no per-request disk logging.
struct Diagnostics {char marker[16];uint32_t version;volatile LONG hits,dlcHits,successfulFallbacks,failedProbes,errors;};
Diagnostics diagnostics{"RBAST-STATS-001",1,0,0,0,0,0};

bool __fastcall OpenHook(assets::Manager* manager,void*,const char* name,uint32_t* output){
    auto* state=active.load(std::memory_order_acquire);
    if(state->enabled.load(std::memory_order_acquire)){
        try{
            bool dlc=false;
            if(assets::OpenPrepared(state->store,state->api,manager,name,output,state->verify,dlc)){
                InterlockedIncrement(&diagnostics.hits);if(dlc)InterlockedIncrement(&diagnostics.dlcHits);return true;
            }
        }catch(...){InterlockedIncrement(&diagnostics.errors);}
    }
    const bool result=state->original(manager,name,output);
    InterlockedIncrement(result?&diagnostics.successfulFallbacks:&diagnostics.failedProbes);return result;
}
void Need(bool value,const char* error){if(!value)throw std::runtime_error(error);}
void CheckEntry(uintptr_t address,uint32_t seh,uintptr_t base){
    unsigned char expected[]={0x55,0x8b,0xec,0x6a,0xff,0x68,0,0,0,0,0x64,0xa1,0,0,0,0};
    uint32_t relocated=static_cast<uint32_t>(base+seh-0x400000);std::memcpy(expected+6,&relocated,4);
    MEMORY_BASIC_INFORMATION info{};
    Need(VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))==sizeof(info)&&
         info.State==MEM_COMMIT&&info.Type==MEM_IMAGE&&info.Protect==PAGE_EXECUTE_READ&&
         !std::memcmp(reinterpret_cast<void*>(address),expected,sizeof(expected)),"Asset ABI signature conflict");
}
}
void InstallUncompressedAssets(const Context& context) noexcept {
    try{
        Need(!active.load(),"Asset hook already initialized");
        const auto* identified=IdentifyGame(HashFile(ModulePath(context.game)));
        Need(identified&&identified->id==context.spec.id,"Asset loader executable identity conflict");
        const auto start=GetTickCount64();const auto id=static_cast<uint32_t>(context.spec.id);Need(id<4,"Unknown asset target");
        const auto& t=targets[id];const auto base=reinterpret_cast<uintptr_t>(context.game);
        CheckEntry(base+t.open,t.openSeh,base);CheckEntry(base+t.resolve,t.resolveSeh,base);
        if(id==0){
            const unsigned char append[]={0x55,0x8b,0xec,0x56,0x8b,0xf1,0x57,0x8b,0x46,0x04,0x8b,0x7d,0x08,0x3b,0xf8,0x73};
            Need(!std::memcmp(reinterpret_cast<void*>(base+t.append),append,16),"Asset slot append conflict");
        }else{
            constexpr uint32_t appendSeh[]={0,0x719271,0x75ee91,0x7260c1};
            CheckEntry(base+t.append,appendSeh[id],base);
        }
        auto* allocate=reinterpret_cast<unsigned char*>(base+t.allocate);
        uint32_t slot=0;std::memcpy(&slot,allocate+2,4);
        Need(allocate[0]==0xff&&allocate[1]==0x25&&slot==base+t.deleteSlot+4,"Asset allocator thunk conflict");
        auto crt=GetModuleHandleW(id==0?L"MSVCR110.dll":L"MSVCR120.dll");
        Need(crt&&*reinterpret_cast<FARPROC*>(slot)==GetProcAddress(crt,"??2@YAPAXI@Z")&&
             *reinterpret_cast<FARPROC*>(base+t.deleteSlot)==GetProcAddress(crt,"??3@YAXPAX@Z"),"Asset allocator IAT conflict");
        wchar_t configured[4096]{};
        auto length=GetPrivateProfileStringW(L"UncompressedAssets",L"Directory",L"rebirths-speedrun-patch\\assets",configured,4096,context.ini.c_str());
        Need(length&&length<4095,"Invalid asset directory setting");
        auto game=std::filesystem::path(ModulePath(context.game)).parent_path();
        auto root=std::filesystem::path(configured);if(root.is_relative())root=game/root;
        auto state=std::make_unique<State>();state->store=assets::Load(root,game,id);
        state->verify=Option(context,L"UncompressedAssets",L"Verify",0)!=0;
        state->api={reinterpret_cast<assets::NativeApi::Open>(base+t.resolve),
                    reinterpret_cast<assets::NativeApi::Allocate>(base+t.allocate),
                    *reinterpret_cast<assets::NativeApi::Delete*>(base+t.deleteSlot),
                    reinterpret_cast<assets::NativeApi::Append>(base+t.append)};
        auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        Need(trampoline!=nullptr,"Asset trampoline allocation failed");
        std::memcpy(trampoline,reinterpret_cast<void*>(base+t.open),5);trampoline[5]=0xe9;
        auto back=static_cast<uint32_t>(base+t.open+5-reinterpret_cast<uintptr_t>(trampoline)-10);std::memcpy(trampoline+6,&back,4);
        DWORD old=0;
        if(!VirtualProtect(trampoline,10,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),trampoline,10)){
            VirtualFree(trampoline,0,MEM_RELEASE);throw std::runtime_error("Asset trampoline protection failed");
        }
        state->original=reinterpret_cast<assets::NativeApi::Open>(trampoline);
        auto* published=state.release();active.store(published,std::memory_order_release);
        // Pin state/trampoline for process lifetime even if a transaction reports uncertain cleanup.
        unsigned char expected[]={0x55,0x8b,0xec,0x6a,0xff},patch[5]={0xe9};
        auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&OpenHook)-base-t.open-5);std::memcpy(patch+1,&relative,4);
        if(!RetargetBytes(context,t.open,expected,patch,5)){
            Log("UncompressedAssets transaction rejected error=%lu",GetLastError());return;
        }
        published->enabled.store(true,std::memory_order_release);
        Log("UncompressedAssets active backend=%s entries=%zu rejected_groups=%u verify=%u startup_ms=%llu",published->store.backend?"pac":"loose",published->store.count,published->store.rejectedGroups,published->verify,GetTickCount64()-start);
        for(const auto& message:published->store.diagnostics)Log("UncompressedAssets %s",message.c_str());
    }catch(const std::exception& e){Log("UncompressedAssets unavailable: %s; original loader retained",e.what());}
    catch(...){Log("UncompressedAssets unavailable; original loader retained");}
}
}
