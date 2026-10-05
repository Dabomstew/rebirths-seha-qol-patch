#include "voice_tail_stream.hpp"
#include "voice_tail.hpp"
#include "rebirth1_voice_tail_table.hpp"
#include "rebirth2_voice_tail_table.hpp"
#include <cassert>
#include <cstdio>
#include <iterator>
#include <set>
#include <vector>

// Qualification is never run by these fixtures; link the real adapter and
// transaction/identity implementations without loading the rest of the proxy.
namespace rebirths {
void Log(const char*,...) noexcept {}
int Option(const Context&,const wchar_t*,const wchar_t*,int fallback){return fallback;}
}

unsigned char* observedParameters=nullptr;
bool expectCallbacks=false;
HRESULT WINAPI FakeInitialize(void*,void* parameters) {
    auto* bytes=static_cast<unsigned char*>(parameters);
    assert(*reinterpret_cast<DWORD*>(bytes)==250);
    const auto read=*reinterpret_cast<void**>(bytes+20);
    const auto overlapped=*reinterpret_cast<void**>(bytes+24);
    if(expectCallbacks)assert(read && overlapped==reinterpret_cast<void*>(&GetOverlappedResult));
    else assert(parameters==observedParameters);
    return 0x08675309;
}

void CheckAdapter(void* engine,unsigned char* parameters) {
    void* adapter=rebirths::testing::VoiceInitializeAdapterAddress();
    DWORD before=0,after=0,returned=0,esiResult=0;
    __asm {
        push esi
        mov before,esp
        push parameters
        push engine
        call adapter
        mov returned,eax
        mov esiResult,esi
        mov after,esp
        pop esi
    }
    assert(before==after && returned==0x08675309 && esiResult==returned);
}

void CheckReadAndAbi() {
    using namespace rebirths;
    wchar_t directory[MAX_PATH]{},path[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH,directory) && GetTempFileNameW(directory,L"rbv",0,path));
    std::vector<unsigned char> bytes(4096),original(1024),changed;
    for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<unsigned char>(i);
    original.assign(bytes.begin(),bytes.begin()+1024);changed=original;changed[513]=42;
    HANDLE writer=CreateFileW(path,GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY,nullptr);
    DWORD done=0;
    assert(writer!=INVALID_HANDLE_VALUE && WriteFile(writer,bytes.data(),4096,&done,nullptr) && done==4096);
    assert(CloseHandle(writer));
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    HANDLE other=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    assert(file!=INVALID_HANDLE_VALUE && other!=INVALID_HANDLE_VALUE);
    testing::ConfigureVoiceRead(file,original.data(),changed.data(),original.size(),true);
    for(DWORD offset:{0u,513u,1000u,2048u}) {
        unsigned char buffer[512]{};OVERLAPPED ov{};ov.Offset=offset;ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        assert(ov.hEvent);
        BOOL ok=testing::ReadVoiceTest(file,buffer,512,&done,&ov);
        assert(ok || GetLastError()==ERROR_IO_PENDING);
        assert(GetOverlappedResult(file,&ov,&done,TRUE) && done==512);
        for(size_t i=0;i<512;++i)assert(buffer[i]==(offset+i<1024?changed[offset+i]:bytes[offset+i]));
        assert(WaitForSingleObject(ov.hEvent,0)==WAIT_OBJECT_0 && CloseHandle(ov.hEvent));
    }
    // An unregistered handle and disabled setting preserve the original bytes.
    for(HANDLE selected:{other,file}) {
        if(selected==file)testing::ConfigureVoiceRead(file,original.data(),changed.data(),1024,false);
        unsigned char buffer[1024]{};OVERLAPPED ov{};
        BOOL ok=testing::ReadVoiceTest(selected,buffer,1024,&done,&ov);
        assert(ok || GetLastError()==ERROR_IO_PENDING);
        assert(GetOverlappedResult(selected,&ov,&done,TRUE) && done==1024);
        assert(!std::memcmp(buffer,original.data(),1024));
    }
    // Independent x86 runtime-parameter layout and native stdcall oracle.
    void* methods[7]{};methods[6]=reinterpret_cast<void*>(&FakeInitialize);void** engine=methods;
    alignas(4) unsigned char parameters[44]{};*reinterpret_cast<DWORD*>(parameters)=250;
    observedParameters=parameters;expectCallbacks=false;CheckAdapter(&engine,parameters);
    testing::ConfigureVoiceRead(file,original.data(),changed.data(),1024,true);
    expectCallbacks=true;CheckAdapter(&engine,parameters);
    assert(!*reinterpret_cast<void**>(parameters+20) && !*reinterpret_cast<void**>(parameters+24));
    *reinterpret_cast<void**>(parameters+20)=reinterpret_cast<void*>(&ReadFile);
    expectCallbacks=false;CheckAdapter(&engine,parameters);
    assert(CloseHandle(file) && CloseHandle(other) && DeleteFileW(path));
}

void Check(const rebirths::audio_tail::Word* words,size_t count,size_t prefix) {
    using namespace rebirths::audio_tail;
    std::vector<unsigned char> original(prefix),corrected;
    std::set<uint32_t> offsets;
    for(size_t i=0;i<count;++i) {
        assert(words[i].offset+4<=prefix && words[i].offset%4==0);
        assert(words[i].replacement<words[i].expected && offsets.insert(words[i].offset).second);
    }
    Write(original.data(),words,count,true);corrected=original;
    assert(Validate(original.data(),prefix,words,count));Write(corrected.data(),words,count);
    // Exercise arbitrary read boundaries, including every split-word position,
    // an audio-crossing read and offsets beyond UINT32_MAX. Never mutate source.
    for(size_t step:{1u,3u,511u,2048u,65536u}) {
        auto delivered=original;delivered.resize(prefix+32,0x7f);
        for(size_t offset=0;offset<delivered.size();offset+=step)
            assert(OverlayVoiceMetadata(delivered.data()+offset,(std::min)(step,delivered.size()-offset),offset,
                original.data(),corrected.data(),prefix));
        assert(!std::memcmp(delivered.data(),corrected.data(),prefix));
        for(size_t i=prefix;i<delivered.size();++i)assert(delivered[i]==0x7f);
    }
    unsigned char bad[8]{};
    const auto offset=words[0].offset;
    std::memcpy(bad,original.data()+offset,8);bad[0]^=1;
    const auto before=std::vector<unsigned char>(bad,bad+8);
    assert(!OverlayVoiceMetadata(bad,8,offset,original.data(),corrected.data(),prefix));
    assert(!std::memcmp(bad,before.data(),8));
    assert(OverlayVoiceMetadata(bad,8,uint64_t{1}<<32,original.data(),corrected.data(),prefix));
    assert(Validate(original.data(),prefix,words,count));
}

int main() {
    using namespace rebirths::audio_tail;
    static_assert(rebirth1::CorrectedVoices==13376 && rebirth2::CorrectedVoices==9615);
    Check(rebirth1::VoiceWords,std::size(rebirth1::VoiceWords),399360);
    Check(rebirth2::VoiceWords,std::size(rebirth2::VoiceWords),262144);
    CheckReadAndAbi();
    std::puts("RB1/RB2 voice bounds, split/overlapped reads, payload/off preservation, rejection and native init ABI passed");
}
