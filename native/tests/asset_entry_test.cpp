#include "asset_entry.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace rebirths::assets;
namespace {
bool failAllocate=false,failAppend=false,failResolve=false;
int allocations=0,deletions=0;
void Check(bool ok){if(!ok)throw std::runtime_error("Entry test assertion");}
void* __cdecl Allocate(size_t size){if(failAllocate)throw std::bad_alloc();++allocations;return ::operator new(size);}
void __cdecl Delete(void* p){++deletions;::operator delete(p);}
bool __fastcall Resolve(Manager* manager,void*,const char* name,uint32_t* id){
    manager->error=77;if(failResolve||std::strcmp(name,"logical"))return false;*id=0x20003;return true;
}
void __fastcall Append(void* vector,void*,Entry** entry){
    if(failAppend)throw std::bad_alloc();
    auto** begin=static_cast<Entry***>(vector);auto** end=begin+1;auto** capacity=begin+2;
    Check(*end<*capacity);**end=*entry;++*end;
}
void Close(Manager& manager,uint32_t index){auto* entry=manager.begin[index];Check(entry->valid);Check(CloseHandle(entry->handle)!=0);entry->valid=0;}
}
int main(){
    try{
        wchar_t temp[MAX_PATH],path[MAX_PATH];Check(GetTempPathW(MAX_PATH,temp)&&GetTempFileNameW(temp,L"rba",0,path));
        struct Cleanup {const wchar_t* path;~Cleanup(){DeleteFileW(path);}} cleanup{path};
        {
            Handle h(CreateFileW(path,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
            DWORD wrote;Check(WriteFile(h.value,"HEADabc",7,&wrote,nullptr)&&wrote==7);
        }
        Store store;store.root=std::filesystem::path(path).parent_path();
        File file;file.path=std::filesystem::path(path).filename().string();file.size=3;file.storageSize=7;file.offset=4;file.id=0x20003;
        {auto h=OpenRead(path);file.hash=HashRange(h.value,4,3);}
        store.groups["data/game"].emplace(file.id,file);
        Manager manager{};Entry* slots[8]{};manager.begin=manager.end=slots;manager.capacity=slots+8;manager.name="data/GAME";manager.error=19;
        InitializeCriticalSection(&manager.lock);
        NativeApi api{reinterpret_cast<NativeApi::Open>(&Resolve),Allocate,Delete,reinterpret_cast<NativeApi::Append>(&Append)};
        uint32_t a=99,b=99;bool dlc=false;
        Check(OpenPrepared(store,api,&manager,"logical",&a,true,dlc)&&a==0&&manager.error==0);
        Check(OpenPrepared(store,api,&manager,"logical",&b,true,dlc)&&b==1&&slots[0]->handle!=slots[1]->handle);
        auto* entry=slots[a];Check(entry->size==3&&entry->packed==3&&!entry->compression&&entry->start==4&&entry->blockIndex==UINT32_MAX);
        DWORD got=0;char bytes[4]{};Check(SetFilePointer(entry->handle,entry->start,nullptr,FILE_BEGIN)==4);
        Check(ReadFile(entry->handle,bytes,2,&got,nullptr)&&got==2&&!std::memcmp(bytes,"ab",2));
        Check(SetFilePointer(slots[b]->handle,0,nullptr,FILE_CURRENT)==0);
        Check(ReadFile(entry->handle,bytes,3,&got,nullptr)&&got==1&&bytes[0]=='c');
        Close(manager,a);Check(OpenPrepared(store,api,&manager,"logical",&a,false,dlc)&&a==0&&allocations==2);
        Close(manager,a);Close(manager,b);
        manager.error=42;failResolve=true;a=99;
        Check(!OpenPrepared(store,api,&manager,"logical",&a,false,dlc)&&manager.error==42&&a==99);failResolve=false;
        // Force allocation and insertion failures; both temporary handle and scalar are reclaimed.
        slots[0]->valid=slots[1]->valid=1;
        DWORD before=0,after=0;Check(GetProcessHandleCount(GetCurrentProcess(),&before)!=0);
        for(int mode=0;mode<2;mode++){
            failAllocate=mode==0;failAppend=mode==1;bool threw=false;
            try{OpenPrepared(store,api,&manager,"logical",&a,false,dlc);}catch(...){threw=true;}
            Check(threw&&manager.end==slots+2&&manager.error==42&&a==99);
        }
        Check(GetProcessHandleCount(GetCurrentProcess(),&after)&&before==after);
        failAllocate=failAppend=false;slots[0]->valid=slots[1]->valid=0;
        for(auto p=manager.begin;p!=manager.end;++p)Delete(*p);
        Check(allocations==deletions);DeleteCriticalSection(&manager.lock);
        std::cout<<"Entry ABI, raw ranges, independent handles, slot reuse and failure ownership passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
