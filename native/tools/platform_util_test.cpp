#include "platform_util.hpp"
#include <cstdio>
#include <type_traits>

namespace util = rebirths::platform;
namespace {
enum class Fault { None, Open, OpenAfterAcquire, Create, CreateAfterAcquire, Update, Finish };
Fault fault;
int opens, creates, updates, destroys, closes;
bool releaseOrder;
constexpr NTSTATUS Failed = static_cast<NTSTATUS>(0xc0000001);
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected, "invalid input/fault was accepted");
}
NTSTATUS WINAPI Open(BCRYPT_ALG_HANDLE* out, LPCWSTR name, LPCWSTR provider, ULONG flags) {
    if (fault == Fault::Open) return Failed;
    auto result = BCryptOpenAlgorithmProvider(out, name, provider, flags);
    if (result >= 0) ++opens;
    return fault == Fault::OpenAfterAcquire ? Failed : result;
}
NTSTATUS WINAPI Create(BCRYPT_ALG_HANDLE algorithm, BCRYPT_HASH_HANDLE* out,
                       PUCHAR object, ULONG objectSize, PUCHAR secret, ULONG secretSize, ULONG flags) {
    if (fault == Fault::Create) return Failed;
    auto result = BCryptCreateHash(algorithm, out, object, objectSize, secret, secretSize, flags);
    if (result >= 0) ++creates;
    return fault == Fault::CreateAfterAcquire ? Failed : result;
}
NTSTATUS WINAPI Update(BCRYPT_HASH_HANDLE hash, PUCHAR input, ULONG size, ULONG flags) {
    ++updates;
    return fault == Fault::Update ? Failed : BCryptHashData(hash, input, size, flags);
}
NTSTATUS WINAPI Finish(BCRYPT_HASH_HANDLE hash, PUCHAR output, ULONG size, ULONG flags) {
    return fault == Fault::Finish ? Failed : BCryptFinishHash(hash, output, size, flags);
}
NTSTATUS WINAPI Destroy(BCRYPT_HASH_HANDLE hash) { ++destroys; return BCryptDestroyHash(hash); }
NTSTATUS WINAPI Close(BCRYPT_ALG_HANDLE algorithm, ULONG flags) {
    releaseOrder = releaseOrder && destroys == creates;
    ++closes;
    return BCryptCloseAlgorithmProvider(algorithm, flags);
}
void ShaContracts() {
    static_assert(!std::is_copy_constructible_v<util::Sha>);
    static_assert(!std::is_move_constructible_v<util::Sha>);
    const util::ShaApi api{Open, Create, Update, Finish, Destroy, Close};
    for (auto stage : {Fault::None, Fault::Open, Fault::OpenAfterAcquire, Fault::Create,
                       Fault::CreateAfterAcquire, Fault::Update, Fault::Finish}) {
        fault = stage; opens = creates = updates = destroys = closes = 0; releaseOrder = true;
        auto run = [&] {
            util::Sha sha(api);
            sha.Add("a", 1); sha.Add("bc", 2);
            Require(util::Hex(sha.Finish()) ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                "split abc SHA-256 mismatch");
        };
        if (stage == Fault::None) run(); else Reject(run);
        Require(opens == closes && creates == destroys && releaseOrder,
                "SHA failure leaked resources or released them in the wrong order");
        Require(opens == (stage == Fault::Open ? 0 : 1), "provider fault not exercised");
        Require(creates == (stage == Fault::None || stage == Fault::CreateAfterAcquire ||
                            stage == Fault::Update || stage == Fault::Finish ? 1 : 0),
                "hash fault not exercised");
    }
    fault = Fault::None;
    {
        util::Sha empty;
        Require(util::Hex(empty.Finish()) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "empty SHA-256 mismatch");
    }
    if constexpr (sizeof(size_t) > sizeof(ULONG)) {
        util::Sha sha(api);
        const int before = updates;
        Reject([&] { sha.Add("x", size_t(ULONG_MAX) + 1); });
        Require(updates == before, "oversize SHA input reached BCrypt");
    }
}
void EncodingContracts() {
    Require(util::Wide("").empty() && util::Utf8(L"").empty(), "empty Unicode mismatch");
    const std::string utf8 = "\xe6\x97\xa5\xf0\x9f\x98\x80";
    const std::wstring wide = L"\x65e5\xd83d\xde00";
    Require(util::Wide(utf8) == wide && util::Utf8(wide) == utf8, "Unicode round trip mismatch");
    const std::string nul("a\0b", 3);
    Require(util::Utf8(util::Wide(nul)) == nul, "embedded NUL lost");
    for (const std::string text : {"\xc0\xaf", "\x80", "\xe2\x82", "\xed\xa0\x80", "\xf4\x90\x80\x80"})
        Reject([&] { util::Wide(text); });
    for (const std::wstring text : {L"\xd800", L"\xdc00", L"\xd800x"})
        Reject([&] { util::Utf8(text); });
    util::Digest bytes{};
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<unsigned char>(i * 7);
    Require(util::Unhex(util::Hex(bytes)) == bytes, "digest text round trip mismatch");
    for (const std::string text : {std::string(63, '0'), std::string(65, '0'),
                                  std::string(64, 'g'), std::string(64, 'A'), std::string(64, '\0')})
        Reject([&] { util::Unhex(text); });
    Require(util::Hex(std::array<unsigned char, 2>{0, 255}) == "00ff", "metadata hex mismatch");
}
bool Live(HANDLE handle) { DWORD flags = 0; return GetHandleInformation(handle, &flags) != FALSE; }
void HandleContracts() {
    static_assert(!std::is_copy_constructible_v<util::Handle>);
    static_assert(std::is_nothrow_move_constructible_v<util::Handle>);
    static_assert(std::is_nothrow_move_assignable_v<util::Handle>);
    util::Handle invalid, null(nullptr);
    Require(!invalid && !null, "invalid handle accepted");
    invalid.Reset(); null.Reset();
    const HANDLE first = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const HANDLE second = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(first && second, "event fixture failed");
    {
        util::Handle a(first), b(second);
        util::Handle moved(std::move(a));
        Require(!a && moved.value == first && Live(first), "handle move failed");
        b = std::move(moved);
        Require(!moved && b.value == first && !Live(second), "move assignment leaked old handle");
        auto& alias = b;
        b = std::move(alias);
        Require(b.value == first && Live(first), "self move lost handle");
        const HANDLE released = b.Release();
        Require(!b && Live(released), "released handle closed");
        util::Handle owned(released);
        owned.Reset(); owned.Reset();
        Require(!Live(first), "reset did not close handle");
    }
    const HANDLE unwound = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(unwound != nullptr, "unwind event fixture failed");
    Reject([&] { util::Handle owned(unwound); throw std::runtime_error("fixture unwind"); });
    Require(!Live(unwound), "exception leaked owned handle");
}
}
int main() {
    try {
        ShaContracts(); EncodingContracts(); HandleContracts();
        std::puts("Platform utility contracts passed: SHA faults/digests, strict encoding, handle ownership");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
