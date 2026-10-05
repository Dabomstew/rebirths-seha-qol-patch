#include "asset_entry.hpp"
#include <cstring>
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
}
bool OpenPrepared(const Store& store,const NativeApi& api,Manager* manager,
                  const char* name,uint32_t* output,bool verify,bool& dlc){
    Lock lock(&manager->lock);
    const auto previousError=manager->error;
    struct Restore {Manager* manager;uint32_t error;bool success=false;~Restore(){if(!success)manager->error=error;}} restore{manager,previousError};
    auto group=store.groups.find(CanonicalPath(manager->name,true));
    if(group==store.groups.end())return false;
    uint32_t id=UINT32_MAX;
    if(!api.resolve(manager,name,&id))return false;
    auto found=group->second.find(id);if(found==group->second.end())return false;
    const auto& file=found->second;
    auto handle=OpenAsset(store,file,verify);
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
    entry->id=id;entry->handle=handle.Release();
    entry->packed=file.size;entry->size=file.size;entry->compression=0;
    entry->relative=0;entry->start=static_cast<uint32_t>(file.offset);
    entry->blockIndex=UINT32_MAX;entry->valid=1;
    *output=static_cast<uint32_t>(index);manager->error=0;dlc=file.dlc;restore.success=true;
    return true;
}
}
