#include "prepare_json.hpp"
#include "platform_util.hpp"
#include <stdexcept>
using rebirths::platform::Wide;

namespace rebirths::prepare::assetprep {
void Codepoint(std::string& out, uint32_t cp) {
    Need(cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff), "Invalid JSON Unicode");
    if (cp < 0x80)
        out += char(cp);
    else if (cp < 0x800) {
        out += char(0xc0 | (cp >> 6));
        out += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += char(0xe0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 63));
        out += char(0x80 | (cp & 63));
    } else {
        out += char(0xf0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 63));
        out += char(0x80 | ((cp >> 6) & 63));
        out += char(0x80 | (cp & 63));
    }
}
class JsonReader {
    const std::string& s;
    size_t p = 0;
    void White() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
            p++;
    }
    char Take() {
        Need(p < s.size(), "Truncated JSON");
        return s[p++];
    }
    uint32_t Quad() {
        uint32_t n = 0;
        for (int i = 0; i < 4; i++) {
            char c = Take();
            n <<= 4;
            if (c >= '0' && c <= '9')
                n |= c - '0';
            else if (c >= 'a' && c <= 'f')
                n |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                n |= c - 'A' + 10;
            else
                throw std::runtime_error("Invalid JSON escape");
        }
        return n;
    }
    std::string String() {
        Need(Take() == '"', "JSON quote expected");
        std::string out;
        while (true) {
            char c = Take();
            if (c == '"') {
                Wide(out);
                return out;
            }
            Need(static_cast<unsigned char>(c) >= 32, "JSON control character");
            if (c != '\\') {
                out += c;
                continue;
            }
            c = Take();
            if (c == 'u') {
                uint32_t cp = Quad();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    Need(Take() == '\\' && Take() == 'u', "JSON surrogate pair expected");
                    uint32_t lo = Quad();
                    Need(lo >= 0xdc00 && lo <= 0xdfff, "Invalid JSON surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                }
                Codepoint(out, cp);
            } else if (c == '"' || c == '\\' || c == '/')
                out += c;
            else if (c == 'n')
                out += '\n';
            else if (c == 'r')
                out += '\r';
            else if (c == 't')
                out += '\t';
            else if (c == 'b')
                out += '\b';
            else if (c == 'f')
                out += '\f';
            else
                throw std::runtime_error("Invalid JSON escape");
        }
    }
    J Value(unsigned depth) {
        Need(depth < 32, "JSON nesting limit");
        White();
        Need(p < s.size(), "Truncated JSON");
        if (s[p] == '"')
            return J(String());
        if (s[p] == '{') {
            p++;
            J::Object o;
            White();
            if (p < s.size() && s[p] == '}') {
                p++;
                return J(o);
            }
            while (true) {
                White();
                auto key = String();
                White();
                Need(Take() == ':', "JSON colon expected");
                Need(o.emplace(key, Value(depth + 1)).second, "Duplicate JSON key");
                White();
                char c = Take();
                if (c == '}')
                    return J(o);
                Need(c == ',', "JSON comma expected");
            }
        }
        if (s[p] == '[') {
            p++;
            J::Array a;
            White();
            if (p < s.size() && s[p] == ']') {
                p++;
                return J(a);
            }
            while (true) {
                a.push_back(Value(depth + 1));
                White();
                char c = Take();
                if (c == ']')
                    return J(a);
                Need(c == ',', "JSON comma expected");
            }
        }
        if (s.compare(p, 4, "true") == 0) {
            p += 4;
            return J(true);
        }
        if (s.compare(p, 5, "false") == 0) {
            p += 5;
            return J(false);
        }
        if (s.compare(p, 4, "null") == 0) {
            p += 4;
            return J();
        }
        Need(s[p] >= '0' && s[p] <= '9', "JSON unsigned integer expected");
        if (s[p] == '0') {
            p++;
            Need(p == s.size() || s[p] < '0' || s[p] > '9', "JSON leading zero");
            return J(0u);
        }
        uint64_t n = 0;
        do {
            auto d = unsigned(s[p++] - '0');
            Need(n <= (UINT64_MAX - d) / 10, "JSON integer overflow");
            n = n * 10 + d;
        } while (p < s.size() && s[p] >= '0' && s[p] <= '9');
        return J(n);
    }

  public:
    explicit JsonReader(const std::string& text) : s(text) {}
    J Parse() {
        auto result = Value(0);
        White();
        Need(p == s.size(), "Trailing JSON text");
        return result;
    }
};
std::string Quote(const std::string& s) {
    const char* h = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 32) {
            out += "\\u00";
            out += h[c >> 4];
            out += h[c & 15];
        } else
            out += char(c);
    }
    return out + '"';
}
std::string JsonText(const J& j) {
    if (std::holds_alternative<std::nullptr_t>(j.value))
        return "null";
    if (auto b = std::get_if<bool>(&j.value))
        return *b ? "true" : "false";
    if (auto n = std::get_if<uint64_t>(&j.value))
        return std::to_string(*n);
    if (auto s = std::get_if<std::string>(&j.value))
        return Quote(*s);
    if (auto a = std::get_if<J::Array>(&j.value)) {
        std::string out = "[";
        for (const auto& x : *a) {
            if (out.size() > 1)
                out += ',';
            out += JsonText(x);
        }
        return out + ']';
    }
    std::string out = "{";
    for (const auto& [k, v] : std::get<J::Object>(j.value)) {
        if (out.size() > 1)
            out += ',';
        out += Quote(k) + ':' + JsonText(v);
    }
    return out + '}';
}
J ParseJson(const std::string& text) {
    return JsonReader(text).Parse();
}
} // namespace rebirths::prepare::assetprep
