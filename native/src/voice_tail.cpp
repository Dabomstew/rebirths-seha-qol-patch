#include "voice_tail.hpp"
#include "voice_tail_stream.hpp"
#include "rebirth1_voice_tail_table.hpp"
#include "rebirth2_voice_tail_table.hpp"
#include "platform_util.hpp"
#include "feature_catalog.hpp"
#include <atomic>
#include <intrin.h>
#include <iterator>
#include <mutex>
#include <vector>

namespace rebirths {
namespace {
struct Profile {
    GameId game; const wchar_t* executable;
    uint32_t openIat, settingsOpen, streamOpen, initialize;
    size_t bankSize; const char* bankHash;
    const audio_tail::Word* words; size_t count, voices;
};
// Fresh per-game headless validation; these are RVAs, with openIat an IAT slot.
constexpr Profile profiles[] = {
    {GameId::Rebirth1, L"NeptuniaReBirth1.exe", 0x32f094, 0x2629e8, 0x2627d4, 0x262aa4,
     2092007424, "e71e3cb32b1eb538e8c20add44036f47a9fcebf473eb435e3997d532e2334e8c",
     audio_tail::rebirth1::VoiceWords, std::size(audio_tail::rebirth1::VoiceWords), audio_tail::rebirth1::CorrectedVoices},
    {GameId::Rebirth2, L"NeptuniaReBirth2.exe", 0x33b094, 0x2ab328, 0x2ab0ad, 0x2ab3da,
     1565433856, "af36264cafb306dacca8570f568e5978d26d866692bfeea8a838f994dddfe7e3",
     audio_tail::rebirth2::VoiceWords, std::size(audio_tail::rebirth2::VoiceWords), audio_tail::rebirth2::CorrectedVoices},
};
const Profile* profile = nullptr;
HMODULE gameModule = nullptr, proxyModule = nullptr;
using Open = HANDLE (WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
Open originalOpen = nullptr;
void** openSlot = nullptr;
bool bootstrapUsable = false;
std::once_flag qualifyOnce;
std::atomic<bool> enabled{false};
std::atomic<HANDLE> streamingFile{INVALID_HANDLE_VALUE};
platform::Handle sourceFile;
BY_HANDLE_FILE_INFORMATION sourceIdentity{};
std::vector<unsigned char> pristine, corrected;
std::atomic<const char*> result{"streaming not observed"};
std::atomic<size_t> metadataReads{0};

bool SameFile(HANDLE file) noexcept {
    BY_HANDLE_FILE_INFORMATION info{};
    return GetFileInformationByHandle(file,&info) &&
        info.dwVolumeSerialNumber == sourceIdentity.dwVolumeSerialNumber &&
        info.nFileIndexHigh == sourceIdentity.nFileIndexHigh && info.nFileIndexLow == sourceIdentity.nFileIndexLow &&
        info.nFileSizeHigh == sourceIdentity.nFileSizeHigh && info.nFileSizeLow == sourceIdentity.nFileSizeLow;
}

// XACT's documented callback has exactly the Win32 ReadFile ABI. Metadata
// requests complete before callback return, including ERROR_IO_PENDING. This
// prevents HasOverlappedIoCompleted from exposing an uncorrected header after
// the callback returns. Ordinary asynchronous payload reads pass through.
BOOL WINAPI ReadVoice(HANDLE file, LPVOID buffer, DWORD length, LPDWORD bytes, LPOVERLAPPED overlapped) noexcept {
    if (!enabled.load(std::memory_order_acquire) || file != streamingFile.load() || !overlapped ||
            overlapped->OffsetHigh || overlapped->Offset >= pristine.size())
        return ReadFile(file,buffer,length,bytes,overlapped);
    const DWORD incomingError = GetLastError();
    if (!SameFile(file)) {
        SetLastError(incomingError);
        return ReadFile(file,buffer,length,bytes,overlapped);
    }
    SetLastError(incomingError);
    const DWORD offset = overlapped->Offset;
    BOOL ok = ReadFile(file,buffer,length,bytes,overlapped);
    DWORD error = GetLastError(), transferred = 0;
    if (!ok && error != ERROR_IO_PENDING) { SetLastError(error); return ok; }
    ok = GetOverlappedResult(file,overlapped,&transferred,TRUE);
    error = GetLastError();
    if (!ok) { SetLastError(error); return FALSE; }
    if (!audio_tail::OverlayVoiceMetadata(buffer,transferred,offset,pristine.data(),corrected.data(),pristine.size())) {
        // Qualified source is held read-only without write/delete sharing.
        // A mismatched read cannot be delivered as mixed correction metadata.
        result = "stream metadata identity rejected";
        if (bytes) *bytes = 0;
        overlapped->Internal = 0xc000003e; // STATUS_DATA_ERROR
        overlapped->InternalHigh = 0;
        SetLastError(ERROR_CRC); return FALSE;
    }
    if (bytes) *bytes = transferred;
    ++metadataReads;
    SetLastError(incomingError); return TRUE;
}

struct RuntimeParameters {
    DWORD lookAhead; void* global; DWORD globalSize, globalFlags, allocation;
    decltype(&ReadFile) read; decltype(&GetOverlappedResult) overlapped;
    void* notification; void* renderer; void* xaudio; void* mastering;
};
static_assert(sizeof(RuntimeParameters)==44 && offsetof(RuntimeParameters,read)==20);

HRESULT __stdcall InitializeVoice(void* engine, RuntimeParameters* parameters) noexcept {
    using Initialize = HRESULT (__stdcall*)(void*,RuntimeParameters*);
    const auto initialize = reinterpret_cast<Initialize>((*static_cast<void***>(engine))[6]);
    if (!enabled.load(std::memory_order_acquire) || parameters->read || parameters->overlapped)
        return initialize(engine,parameters);
    auto copy = *parameters;
    copy.read = &ReadVoice; copy.overlapped = &GetOverlappedResult;
    return initialize(engine,&copy);
}

// Replaces CALL [ECX+18h]; MOV ESI,EAX. Forward the two stdcall arguments,
// retain HRESULT in both EAX and ESI, and preserve the following TEST ESI,ESI.
__declspec(naked) void InitializeAdapter() {
    __asm {
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        call InitializeVoice
        mov esi,eax
        ret 8
    }
}

void Qualify() noexcept {
    try {
        const GameSpec* spec = IdentifyGame(HashFile(ModulePath(gameModule)));
        if (!spec || spec->id != profile->game) { result="executable identity rejected"; return; }
        const auto proxy = ModulePath(proxyModule);
        const auto directory = proxy.substr(0,proxy.find_last_of(L"\\/"));
        Context context{gameModule,*spec,directory,directory+L"\\rebirths-patches.ini"};
        if (!ReadRuntimeFeature(spec->id,FeatureId::TrimSilentAudioTails,
                [&](const wchar_t* key,int fallback){return Option(context,L"Patches",key,fallback);})) {
            result="disabled"; return;
        }
        const auto executable = ModulePath(gameModule);
        const auto bank = executable.substr(0,executable.find_last_of(L"\\/"))+L"\\data\\SOUND.xwb";
        // Hold the exact qualified file open; no write or delete sharing.
        platform::Handle file(CreateFileW(bank.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        if (!file || !GetFileInformationByHandle(file.value,&sourceIdentity) || sourceIdentity.nFileSizeHigh ||
                sourceIdentity.nFileSizeLow != profile->bankSize) { result="voice bank size/open rejected"; return; }
        platform::Sha hash;
        std::vector<unsigned char> chunk(4*1024*1024);
        size_t total=0; DWORD done=0;
        for (;;) {
            if (!ReadFile(file.value,chunk.data(),static_cast<DWORD>(chunk.size()),&done,nullptr)) {
                result="voice bank read rejected"; return;
            }
            if (!done) break;
            hash.Add(chunk.data(),done); total+=done;
        }
        if (total!=profile->bankSize || hash.Finish()!=platform::Unhex(profile->bankHash)) {
            result="voice bank identity rejected"; return;
        }
        LARGE_INTEGER zero{};
        unsigned char header[52]{};
        if (!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN) ||
                !ReadFile(file.value,header,sizeof(header),&done,nullptr) || done!=sizeof(header) ||
                std::memcmp(header,"WBND",4)) { result="voice header rejected"; return; }
        uint32_t prefix=0; std::memcpy(&prefix,header+44,4);
        if (prefix<52 || prefix>1024*1024 || prefix>profile->bankSize) { result="voice metadata bounds rejected"; return; }
        pristine.resize(prefix);
        if (!SetFilePointerEx(file.value,zero,nullptr,FILE_BEGIN) ||
                !ReadFile(file.value,pristine.data(),prefix,&done,nullptr) || done!=prefix ||
                !audio_tail::Validate(pristine.data(),prefix,profile->words,profile->count)) {
            result="voice word guard rejected"; return;
        }
        corrected=pristine; audio_tail::Write(corrected.data(),profile->words,profile->count);
        HMODULE pinned=nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(proxyModule),&pinned)) {result="voice proxy pin rejected";return;}
        sourceFile=std::move(file);
        const unsigned char expected[]={0xff,0x51,0x18,0x8b,0xf0};
        unsigned char replacement[5]={0xe8};
        const uint32_t relative=reinterpret_cast<uint32_t>(&InitializeAdapter)-
            (reinterpret_cast<uint32_t>(gameModule)+profile->initialize+5);
        std::memcpy(replacement+1,&relative,4);
        const auto patch=RetargetBytesResult(context,profile->initialize,expected,replacement,5);
        if (!patch.Succeeded()) { result="voice initialization transaction rejected"; return; }
        result="private streaming metadata ready";
        enabled.store(true,std::memory_order_release);
    } catch (...) { result="voice qualification failed; original retained"; }
}

HANDLE WINAPI OpenVoice(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE templ) noexcept {
    const DWORD previous=GetLastError();
    const auto caller=reinterpret_cast<unsigned char*>(_ReturnAddress());
    if (bootstrapUsable && caller==reinterpret_cast<unsigned char*>(gameModule)+profile->settingsOpen+6) {
        try { std::call_once(qualifyOnce,Qualify); } catch (...) { result="voice synchronization rejected"; }
    }
    SetLastError(previous);
    const HANDLE file=originalOpen(path,access,share,security,creation,flags,templ);
    const DWORD error=GetLastError();
    // RB1 loads CreateFileW into ESI and uses a 2-byte CALL ESI; RB2 CALL is 6 bytes.
    const size_t callLength=profile->game==GameId::Rebirth1?2:6;
    if (enabled.load(std::memory_order_acquire) && caller==reinterpret_cast<unsigned char*>(gameModule)+profile->streamOpen+callLength &&
            file!=INVALID_HANDLE_VALUE && access==GENERIC_READ && creation==OPEN_EXISTING && flags==0x60000000 && SameFile(file)) {
        streamingFile.store(file); result="private streaming voice correction active";
    }
    SetLastError(error); return file;
}
}

void BootstrapVoiceTail(HMODULE proxy) noexcept {
    // Fixed kernel calls/guarded access only under loader lock. Hashing,
    // allocations, configuration and CALL transactions run at SOUND.xgs open.
    wchar_t filename[MAX_PATH]{};
    HMODULE game=GetModuleHandleW(nullptr);
    const DWORD length=GetModuleFileNameW(game,filename,MAX_PATH);
    if (!length || length>=MAX_PATH) return;
    const wchar_t* leaf=filename;
    for (const wchar_t* p=filename;*p;++p) if (*p==L'\\'||*p==L'/') leaf=p+1;
    for (const auto& candidate:profiles) if (!lstrcmpiW(leaf,candidate.executable)) {profile=&candidate;break;}
    if (!profile) return;
    __try {
        auto* bytes=reinterpret_cast<unsigned char*>(game);
        const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
        if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>4096) return;
        const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes+dos->e_lfanew);
        if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
                nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
                nt->OptionalHeader.SizeOfImage<=profile->openIat+sizeof(void*) ||
                nt->OptionalHeader.SizeOfImage<profile->settingsOpen+6 ||
                nt->OptionalHeader.SizeOfImage<profile->initialize+5) return;
        if (bytes[profile->settingsOpen]!=0xff || bytes[profile->settingsOpen+1]!=0x15 ||
                *reinterpret_cast<uint32_t*>(bytes+profile->settingsOpen+2)!=reinterpret_cast<uint32_t>(bytes+profile->openIat)) return;
        const unsigned char expectedCall[]={0xff,0x51,0x18,0x8b,0xf0};
        for (size_t i=0;i<5;++i) if(bytes[profile->initialize+i]!=expectedCall[i])return;
        auto** slot=reinterpret_cast<void**>(bytes+profile->openIat);
        const auto expected=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"CreateFileW");
        if (!expected || *slot!=reinterpret_cast<void*>(expected)) return;
        originalOpen=reinterpret_cast<Open>(expected); gameModule=game;proxyModule=proxy;
        DWORD previous=0;
        if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&previous))return;
        void* observed=InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&OpenVoice),reinterpret_cast<void*>(expected));
        DWORD ignored=0;
        const bool restored=VirtualProtect(slot,sizeof(void*),previous,&ignored)!=FALSE;
        if(observed==reinterpret_cast<void*>(expected)){openSlot=slot;bootstrapUsable=restored;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void DetachVoiceTail() noexcept {
    if (!openSlot) return;
    DWORD previous=0;
    if(VirtualProtect(openSlot,sizeof(void*),PAGE_READWRITE,&previous)) {
        InterlockedCompareExchangePointer(openSlot,reinterpret_cast<void*>(originalOpen),reinterpret_cast<void*>(&OpenVoice));
        DWORD ignored=0;VirtualProtect(openSlot,sizeof(void*),previous,&ignored);
    }
}

void LogVoiceTail(const Context& context) noexcept {
    if(profile && context.spec.id==profile->game)
        Log("TrimSilentAudioTails voices bootstrap=%d result=%s waves=%zu metadata_reads=%zu",bootstrapUsable,result.load(),
            enabled.load()?profile->voices:0,metadataReads.load());
}
#ifdef REBIRTHS_TEST_CONTRACTS
namespace testing {
void ConfigureVoiceRead(HANDLE file,const unsigned char* original,const unsigned char* changed,size_t prefix,bool active) {
    pristine.assign(original,original+prefix);corrected.assign(changed,changed+prefix);
    if(!GetFileInformationByHandle(file,&sourceIdentity))throw std::runtime_error("Fixture file identity failed");
    streamingFile=file;enabled=active;
}
BOOL WINAPI ReadVoiceTest(HANDLE file,LPVOID buffer,DWORD length,LPDWORD bytes,LPOVERLAPPED overlapped) noexcept {
    return ReadVoice(file,buffer,length,bytes,overlapped);
}
void* VoiceInitializeAdapterAddress() noexcept {return reinterpret_cast<void*>(&InitializeAdapter);}
}
#endif
}
