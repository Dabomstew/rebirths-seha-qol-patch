// Divided-Huffman primitive adapted from the local VII Speedrun Patch.
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
[[noreturn]] void Fail(const char* text) { throw std::runtime_error(text); }
class Bits {
    const unsigned char* data; size_t bytes, cursor = 0; bool sentinel = false;
public:
    Bits(const unsigned char* p, size_t n) : data(p), bytes(n) {}
    size_t Remaining() const { return cursor < bytes * 8 ? bytes * 8 - cursor : 0; }
    void AllowZeroSentinel() { sentinel = true; }
    unsigned Get(unsigned n) {
        if (n > 16 || cursor + n > bytes * 8 + (sentinel ? 8 : 0)) Fail("Truncated Huffman bitstream");
        unsigned result = 0;
        while (n--) { result = (result << 1) | (cursor < bytes * 8 ? ((data[cursor / 8] >> (7 - cursor % 8)) & 1) : 0); ++cursor; }
        return result;
    }
    unsigned Peek10() const {
        const size_t i = cursor / 8; const unsigned shift = unsigned(cursor % 8);
        unsigned value = unsigned(data[i]) << 16;
        if (i + 1 < bytes) value |= unsigned(data[i + 1]) << 8;
        if (i + 2 < bytes) value |= data[i + 2];
        return (value >> (14 - shift)) & 1023;
    }
    void Advance(unsigned n) { cursor += n; }
};
struct Node { int left = -1, right = -1; };
int Tree(Bits& bits, std::array<Node, 511>& nodes, int& next, unsigned depth) {
    if (depth > 255) Fail("Huffman tree is too deep");
    if (!bits.Get(1)) return int(bits.Get(8));
    if (next >= int(nodes.size())) Fail("Huffman tree has too many branches");
    const int index = next++;
    nodes[index].left = Tree(bits, nodes, next, depth + 1);
    nodes[index].right = Tree(bits, nodes, next, depth + 1);
    return index;
}
std::vector<unsigned char> DecodeHuffman(const unsigned char* data, size_t bytes, size_t outputBytes) {
    if (bytes > 16 * 1024 * 1024 || outputBytes > 16 * 1024 * 1024) Fail("Huffman block exceeds bound");
    if (!outputBytes) return {};
    Bits bits(data, bytes); std::array<Node, 511> nodes{}; int next = 256;
    const int root = Tree(bits, nodes, next, 0);
    // All four native readers supply one zero byte at the declared input end.
    // A shipped RB2 block needs its bits for the final symbol. Keep tree parsing
    // strict and reject any payload requiring more than that single sentinel.
    bits.AllowZeroSentinel();
    struct Fast { int node; unsigned used; };
    std::array<Fast, 1024> table{};
    for (unsigned i = 0; i < table.size(); ++i) {
        int node = root; unsigned used = 0;
        while (node > 255 && used < 10) { node = ((i >> (9 - used)) & 1) ? nodes[node].right : nodes[node].left; ++used; }
        table[i] = {node, used};
    }
    std::vector<unsigned char> output(outputBytes);
    for (auto& byte : output) {
        int node = root;
        if (bits.Remaining() >= 10) { const auto f = table[bits.Peek10()]; bits.Advance(f.used); node = f.node; }
        while (node > 255) node = bits.Get(1) ? nodes[node].right : nodes[node].left;
        byte = static_cast<unsigned char>(node);
    }
    return output;
}

}
extern "C" __declspec(dllexport) int __cdecl RebirthsDecodeHuffman(const unsigned char* src, uint32_t length, unsigned char* dst, uint32_t size) noexcept {
    try { if ((!src && length) || (!dst && size)) return 0; auto result=DecodeHuffman(src,length,size); if(size) std::memcpy(dst,result.data(),size); return 1; } catch (...) { return 0; }
}
