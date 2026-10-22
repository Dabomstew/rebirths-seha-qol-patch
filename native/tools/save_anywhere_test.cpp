#include "../src/save_anywhere.cpp"
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace rebirths {
void Log(const char*, ...) noexcept {}
const char* GameIdName(GameId) noexcept { return "fixture"; }
PatchResult injected{PatchOutcome::Rejected, 1};
size_t siteChecks = 0;
PatchResult RetargetCallsResult(const Context& context, const CallSite* sites, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        if (std::memcmp(reinterpret_cast<unsigned char*>(context.game) + sites[i].rva,
                        sites[i].expected.data(), 5)) return {PatchOutcome::Rejected, 1};
        ++siteChecks;
    }
    return injected;
}
}
using namespace rebirths::save_anywhere;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
unsigned adds=0,forwarded=0,availableCalls=0;
uint32_t lastAction=0;
uint8_t availability=1;
alignas(4) unsigned char catalogData[0x200]{};
void __cdecl Add(uint32_t list,const char* label,uint32_t action){Require(list==0x1234&&std::strcmp(label,"Save")==0,"classic native add arguments");++adds;lastAction=action;}
void __cdecl AddThird(uint32_t list,uint32_t action){Require(list==0x1234,"third native list");++adds;lastAction=action;}
void __cdecl Gate(unsigned char* menu,uint32_t list,const char* label,uint32_t action,uint8_t bit){
    ++forwarded;if(menu&&!(*reinterpret_cast<uint32_t*>(menu+0x14)&(1u<<(bit&31))))Add(list,label,action);
}
void __cdecl GateThird(unsigned char* menu,uint32_t list,uint32_t action,uint8_t bit){
    ++forwarded;if(menu&&!(*reinterpret_cast<uint32_t*>(menu+0x14)&(1u<<(bit&31))))AddThird(list,action);
}
unsigned char* __cdecl Data(uint32_t catalog){Require(catalog==0x1234,"native catalog argument");return catalogData;}
uint8_t __cdecl Available(uint32_t action){Require(action==0xf,"native Save availability argument");++availableCalls;return availability;}
uint8_t __cdecl SegaNative(uint32_t catalog,uint32_t action){
    ++forwarded;if(catalog&&((catalogData[0x170]&&action-0xf<2)||(catalogData[0x171]&&action==0xf)))return 0;
    return availability;
}
void InvokeClassic(unsigned char* menu){
    uint32_t before,after,saved,preserved;const char* label="Save";
    __asm{
        mov saved,edi
        mov before,esp
        mov edi,menu
        push 1bh
        push 0
        push label
        push 1234h
        push menu
        call Classic
        add esp,14h
        mov after,esp
        mov preserved,edi
        mov edi,saved
    }
    Require(before==after&&preserved==reinterpret_cast<uint32_t>(menu),"classic native five-argument cdecl stack/EDI");
}
void InvokeThird(unsigned char* menu){
    uint32_t before,after,saved,preserved;
    __asm{
        mov saved,edi
        mov before,esp
        mov edi,menu
        push 1bh
        push 1ah
        push 1234h
        push menu
        call Third
        add esp,10h
        mov after,esp
        mov preserved,edi
        mov edi,saved
    }
    Require(before==after&&preserved==reinterpret_cast<uint32_t>(menu),"third native four-argument cdecl stack/EDI");
}
int main(){try{
    alignas(4) unsigned char menu[0x200]{};
    classicGate=Gate;thirdGate=GateThird;addClassic=Add;addId=AddThird;
    segaGate=SegaNative;segaAvailable=Available;widgetData=Data;
    for(unsigned game=0;game<3;++game){
        profile=&profiles[game];
        auto invoke=game<2?InvokeClassic:InvokeThird;
        for(bool active:{false,true})for(uint8_t mode:{uint8_t{0},uint8_t{1}})for(bool suppressed:{false,true}){
            enabled=active;menu[profile->modeOffset]=mode;*reinterpret_cast<uint32_t*>(menu+0x14)=suppressed?1u<<27:0;
            const auto before=adds,prior=forwarded;invoke(menu);
            const bool bypass=active&&mode==0;
            Require(adds-before==((bypass||!suppressed)?1u:0u),"native mode0/offpoint Save matrix");
            Require(forwarded-prior==(bypass?0u:1u),"restricted mode or disabled retains original adapter");
            Require(menu[profile->modeOffset]==mode&&*reinterpret_cast<uint32_t*>(menu+0x14)==(suppressed?1u<<27:0),"native mask/mode unchanged");
        }
        enabled=true;menu[profile->modeOffset]=0;*reinterpret_cast<uint32_t*>(menu+0x14)=0xffffffff;
        const auto prior=forwarded,before=adds;
        if(game<2){Classic(menu,0x1234,"Save",1,27);Classic(menu,0x1234,"Save",0,1);Classic(nullptr,0x1234,"Save",0,27);}
        else{Third(menu,0x1234,0x1b,27);Third(menu,0x1234,0x1a,1);Third(nullptr,0x1234,0x1a,27);}
        Require(forwarded==prior+3&&adds==before,"Load/other bits/null owner always forward");
    }
    profile=&profiles[3];
    for(bool active:{false,true})for(uint8_t mode:{uint8_t{0},uint8_t{1}})for(uint8_t offpoint:{uint8_t{0},uint8_t{1}})for(uint8_t allowed:{uint8_t{0},uint8_t{1}}){
        enabled=active;catalogData[0x170]=mode;catalogData[0x171]=offpoint;availability=allowed;
        const auto prior=forwarded,priorAvailable=availableCalls;
        const uint8_t result=Sega(0x1234,0xf);
        const bool bypass=active&&mode==0&&offpoint!=0;
        Require(result==((bypass||(!mode&&!offpoint))?allowed:0),"Sega actual mode/offpoint/catalog-availability matrix");
        Require(forwarded-prior==(bypass?0u:1u)&&availableCalls-priorAvailable==(bypass?1u:0u),"Sega native helper forwarding");
        Require(catalogData[0x170]==mode&&catalogData[0x171]==offpoint,"Sega native catalog state unchanged");
    }
    enabled=true;availability=1;catalogData[0x170]=0;catalogData[0x171]=1;
    auto prior=forwarded;Require(Sega(0x1234,0x10)==1&&Sega(0,0xf)==1&&forwarded==prior+2,"Sega Load/null preserve native eligibility");
    std::vector<unsigned char> image(0x300000,0);
    for(const auto& candidate:profiles){
        for(auto site:candidate.calls)if(site){const auto bytes=Expected(site,candidate.original);std::memcpy(image.data()+site,bytes.data(),5);}
        const rebirths::GameSpec spec{candidate.game,L"fixture",{},{},{},0x400000,0x1000,0x300000};
        const rebirths::Context context{reinterpret_cast<HMODULE>(image.data()),spec,L"fixture",L"fixture.ini"};
        for(auto outcome:{rebirths::PatchOutcome::Rejected,rebirths::PatchOutcome::Installed,rebirths::PatchOutcome::IncompleteRecovery}){
            rebirths::injected={outcome,1};Require(!rebirths::InstallSaveAnywhere(context)&&!enabled.load(),"failed/partial adapter remains disabled");
            Require(classicGate==reinterpret_cast<ClassicGate>(image.data()+candidate.original),"retained original forwarding target");
        }
        rebirths::injected={rebirths::PatchOutcome::Installed,0};Require(rebirths::InstallSaveAnywhere(context)&&enabled.load(),"only successful transaction activates");
    }
    Require(rebirths::siteChecks==28,"four independent seven-site transaction profiles");
    std::puts("SaveAnywhere native mode/offpoint matrix, exact ABI, eligibility and partial-installation checks pass");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
