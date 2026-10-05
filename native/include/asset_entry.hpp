#pragma once
#include "asset_store.hpp"

namespace rebirths::assets {
// Independently validated on the four original x86 executable baselines.
struct Entry {
    uint32_t id;
    HANDLE handle;
    unsigned char valid,padding[3];
    unsigned char metadata[272];
    uint32_t packed,size,compression,relative,start,cursor;
    void* huffmanMetadata;
    void* decodedBlock;
    uint32_t blockIndex;
};
static_assert(sizeof(void*)==4,"Native game entry adapter is x86 only");
static_assert(sizeof(Entry)==0x140&&offsetof(Entry,start)==0x12c&&offsetof(Entry,size)==0x120);
struct Manager {
    unsigned char prefix[12];
    Entry** begin;
    Entry** end;
    Entry** capacity;
    unsigned char unknown[24];
    const char* name;
    uint32_t error;
    CRITICAL_SECTION lock;
};
static_assert(offsetof(Manager,name)==0x30&&offsetof(Manager,lock)==0x38);
struct NativeApi {
    using Open=bool(__thiscall*)(Manager*,const char*,uint32_t*);
    using Allocate=void*(__cdecl*)(size_t);
    using Delete=void(__cdecl*)(void*);
    using Append=void(__thiscall*)(void*,Entry**);
    Open resolve=nullptr;
    Allocate allocate=nullptr;
    Delete release=nullptr;
    Append append=nullptr;
};
// Only publishes output after full initialization; false leaves original Open available.
bool OpenPrepared(const Store& store,const NativeApi& api,Manager* manager,
                  const char* name,uint32_t* output,bool verify,bool& dlc);
}
