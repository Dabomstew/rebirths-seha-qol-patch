#include "asset_entry.hpp"
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace rebirths::assets {
namespace {
struct Lock {
    CRITICAL_SECTION* value;
    explicit Lock(CRITICAL_SECTION* p):value(p){EnterCriticalSection(value);}
    ~Lock(){LeaveCriticalSection(value);}
};
struct Allocation {
    Entry* value;NativeApi::Delete release;
    ~Allocation(){if(value)release(value);}
};
struct Restore {
    Manager* manager;uint32_t error;bool success=false;
    ~Restore(){if(!success)manager->error=error;}
};
void Publish(const File& file,Handle& handle,const NativeApi& api,Manager* manager,uint32_t* output){
    auto begin=reinterpret_cast<uintptr_t>(manager->begin),end=reinterpret_cast<uintptr_t>(manager->end);
    if(end<begin||(end-begin)%4||(end-begin)/4>100000)throw std::runtime_error("Invalid native slot vector");
    const auto count=(end-begin)/4;size_t index=0;
    for(;index<count;index++)if(!manager->begin[index]->valid)break;
    Entry* entry=nullptr;
    if(index==count){
        Allocation fresh{static_cast<Entry*>(api.allocate(sizeof(Entry))),api.release};
        if(!fresh.value)throw std::bad_alloc();
        std::memset(fresh.value,0,sizeof(Entry));
        api.append(&manager->begin,&fresh.value);
        entry=fresh.value;fresh.value=nullptr; // Native vector owns the scalar allocation.
    }else{
        entry=manager->begin[index];
        if(entry->huffmanMetadata||entry->decodedBlock)throw std::runtime_error("Unclosed native slot");
    }
    // No throwing operations from ownership transfer through publication.
    std::memset(entry,0,sizeof(*entry));
    std::memcpy(reinterpret_cast<unsigned char*>(entry)+0x0c,file.metadata.data(),288);
    entry->id=file.id;entry->handle=handle.Release();
    entry->packed=file.size;entry->size=file.size;entry->compression=0;
    entry->relative=0;entry->start=static_cast<uint32_t>(file.offset);
    entry->blockIndex=UINT32_MAX;entry->valid=1;
    *output=static_cast<uint32_t>(index);manager->error=0;
}
std::wstring RelativeName(const char* name){
    if(!name)throw std::runtime_error("Null loose name");
    // Decode before processing separators/case: DBCS trail bytes may be '\\'.
    const auto length=MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,name,-1,nullptr,0);
    if(length<=1||length>4096)throw std::runtime_error("Invalid loose name");
    std::wstring value(length,L'\0');
    if(!MultiByteToWideChar(CP_ACP,MB_ERR_INVALID_CHARS,name,-1,value.data(),length))
        throw std::runtime_error("Invalid loose name encoding");
    value.pop_back();std::replace(value.begin(),value.end(),L'\\',L'/');
    while(value.rfind(L"./",0)==0)value.erase(0,2);
    while(!value.empty()&&value.front()==L'/')value.erase(0,1);
    return value;
}
std::filesystem::path LoosePath(const std::filesystem::path& root,const char* group,const char* name){
    const auto relative=RelativeName(group)+L"/"+RelativeName(name);
    auto result=std::filesystem::absolute(root).lexically_normal();
    size_t start=0;
    while(start<relative.size()){
        auto end=relative.find(L'/',start);if(end==std::wstring::npos)end=relative.size();
        auto part=relative.substr(start,end-start);
        if(part.empty()||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' '||
           std::any_of(part.begin(),part.end(),[](wchar_t c){return c<32||std::wcschr(L"\\:*?\"<>|",c);}))
            throw std::runtime_error("Unsafe loose path");
        auto stem=part.substr(0,part.find(L'.'));
        for(auto& c:stem)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
        if(stem==L"con"||stem==L"prn"||stem==L"aux"||stem==L"nul"||
           (stem.size()==4&&(stem.substr(0,3)==L"com"||stem.substr(0,3)==L"lpt")&&stem[3]>=L'1'&&stem[3]<=L'9'))
            throw std::runtime_error("Reserved loose path");
        result/=part;
        start=end+1;
    }
    if(relative.back()==L'/')throw std::runtime_error("Empty loose filename");
    return result;
}
}
bool OpenPrepared(const Store& store,const NativeApi& api,Manager* manager,
                  const char* name,uint32_t* output,bool verify,bool& dlc){
    Lock lock(&manager->lock);
    Restore restore{manager,manager->error};
    auto group=store.groups.find(CanonicalPath(manager->name,true));
    if(group==store.groups.end())return false;
    uint32_t id=UINT32_MAX;
    if(!api.resolve(manager,name,&id))return false;
    auto found=group->second.find(id);if(found==group->second.end())return false;
    const auto& file=found->second;
    auto handle=OpenAsset(store,file,verify);
    Publish(file,handle,api,manager,output);
    dlc=file.dlc;restore.success=true;
    return true;
}
bool OpenLoose(const std::filesystem::path& root,const NativeApi& api,Manager* manager,
               const char* name,uint32_t* output){
    Lock lock(&manager->lock);
    Restore restore{manager,manager->error};
    const auto path=LoosePath(root,manager->name,name);
    // Mod files are trusted as supplied, including links managed by mod tools.
    // Do not use OpenAsset/OpenRead: their prepared-data integrity policy does
    // not apply here. Ordinary OS open failure selects the next provider.
    Handle handle(CreateFileW(Extended(path).c_str(),GENERIC_READ,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!handle)return false;
    const auto size=FileSize(handle.value);
    // Native seeks pass a signed 32-bit distance with no high word.
    if(size>INT32_MAX)throw std::runtime_error("Loose file exceeds native seek range");
    File file;file.id=UINT32_MAX;file.size=static_cast<uint32_t>(size);
    Publish(file,handle,api,manager,output);
    restore.success=true;
    return true;
}
Source OpenOverride(const std::filesystem::path* loose,const Store* prepared,
                    const NativeApi& api,Manager* manager,const char* name,
                    uint32_t* output,bool verify,bool& dlc,unsigned& errors) noexcept {
    if(loose)try{if(OpenLoose(*loose,api,manager,name,output))return Source::Loose;}
        catch(...){++errors;}
    if(prepared)try{if(OpenPrepared(*prepared,api,manager,name,output,verify,dlc))return Source::Prepared;}
        catch(...){++errors;}
    return Source::Original;
}
}
