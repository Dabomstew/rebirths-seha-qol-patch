#include "save_anywhere.hpp"
#include <atomic>
#include <cstring>

namespace rebirths {
namespace save_anywhere {
struct Profile {
    GameId game;
    std::array<uint32_t,2> calls;
    uint32_t original, add, modeOffset;
};
constexpr Profile profiles[] = {
    {GameId::Rebirth1,{0xc2d6b,0xc33ac},0xc3920,0xc4940,0x20},
    {GameId::Rebirth2,{0x12903b,0x129693},0x129c10,0x12ad20,0x20},
    {GameId::Rebirth3,{0x13a669,0x13ab2d},0x13b080,0x137b10,0x24},
    {GameId::SegaHardGirls,{0x11869f,0},0x116d20,0x1784b0,0x170},
};
const Profile* profile=nullptr;
std::atomic<bool> enabled{false};
using ClassicGate=void (__cdecl*)(unsigned char*,uint32_t,const char*,uint32_t,uint8_t);
using ThirdGate=void (__cdecl*)(unsigned char*,uint32_t,uint32_t,uint8_t);
using AddClassic=void (__cdecl*)(uint32_t,const char*,uint32_t);
using AddId=void (__cdecl*)(uint32_t,uint32_t);
using SegaGate=uint8_t (__cdecl*)(uint32_t,uint32_t);
using SegaAvailable=uint8_t (__cdecl*)(uint32_t);
using WidgetData=unsigned char* (__cdecl*)(uint32_t);
ClassicGate classicGate=nullptr;
ThirdGate thirdGate=nullptr;
AddClassic addClassic=nullptr;
AddId addId=nullptr;
SegaGate segaGate=nullptr;
SegaAvailable segaAvailable=nullptr;
WidgetData widgetData=nullptr;
bool NormalMode(const unsigned char* menu) noexcept {
    return enabled.load(std::memory_order_acquire)&&menu&&menu[profile->modeOffset]==0;
}
void __cdecl Classic(unsigned char* menu,uint32_t list,const char* label,uint32_t action,uint8_t bit) {
    // Only the native System Save gated-add CALLs redirect here. Mode1 is a
    // restricted/special menu, not a dungeon flag. Never mutate its mask or mode.
    if(NormalMode(menu)&&action==0&&bit==27) addClassic(list,label,action);
    else classicGate(menu,list,label,action,bit);
}
void __cdecl Third(unsigned char* menu,uint32_t list,uint32_t action,uint8_t bit) {
    if(NormalMode(menu)&&action==0x1a&&bit==27) addId(list,action);
    else thirdGate(menu,list,action,bit);
}
uint8_t __cdecl Sega(uint32_t catalog,uint32_t action) {
    // The native gated-add helper still inserts/selects the row in its original
    // position. Bypass only its off-savepoint byte, keeping restricted mode and
    // the native action-availability test. Load and other actions forward.
    if(enabled.load(std::memory_order_acquire)&&catalog&&action==0xf) {
        const auto data=widgetData(catalog);
        if(data&&data[0x170]==0&&data[0x171]!=0) return segaAvailable(action);
    }
    return segaGate(catalog,action);
}
std::array<unsigned char,5> Expected(uint32_t site,uint32_t target) {
    std::array<unsigned char,5> value{0xe8,0,0,0,0};
    const uint32_t relative=target-site-5;std::memcpy(value.data()+1,&relative,4);return value;
}
}
bool InstallSaveAnywhere(const Context& context) noexcept {
    using namespace save_anywhere;
    const Profile* selected=nullptr;
    for(const auto& candidate:profiles)if(candidate.game==context.spec.id)selected=&candidate;
    if(!selected){Log("SaveAnywhere unsupported target");return false;}
    profile=selected;enabled.store(false,std::memory_order_release);
    auto base=reinterpret_cast<unsigned char*>(context.game);
    classicGate=reinterpret_cast<ClassicGate>(base+profile->original);
    thirdGate=reinterpret_cast<ThirdGate>(base+profile->original);
    addClassic=reinterpret_cast<AddClassic>(base+profile->add);
    addId=reinterpret_cast<AddId>(base+profile->add);
    segaGate=reinterpret_cast<SegaGate>(base+profile->original);
    segaAvailable=reinterpret_cast<SegaAvailable>(base+profile->add);
    widgetData=reinterpret_cast<WidgetData>(base+0x239680);
    void* adapter=profile->game==GameId::Rebirth3?reinterpret_cast<void*>(Third):
        profile->game==GameId::SegaHardGirls?reinterpret_cast<void*>(Sega):reinterpret_cast<void*>(Classic);
    CallSite sites[2]{};size_t count=0;
    for(auto site:profile->calls)if(site)sites[count++]={site,Expected(site,profile->original),adapter};
    const auto result=RetargetCallsResult(context,sites,count);
    if(!result.Succeeded()){Log("SaveAnywhere installation failed; adapters retain native behavior");return false;}
    enabled.store(true,std::memory_order_release);
    Log("SaveAnywhere installed game=%s; native System Save off savepoints",GameIdName(profile->game));return true;
}
}
