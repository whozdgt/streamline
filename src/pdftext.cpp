#include "pdftext.h"
#include "pdfcrypto.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace pdftext {
namespace {

#include "pdfglyphs.inc"

// ============================================================ inflate (after Mark Adler's puff)

struct Bits {
    const uint8_t *in;
    size_t len, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool over = false;
    int bits(int need)
    {
        uint32_t val = buf;
        while (cnt < need) {
            if (pos >= len) { over = true; return 0; }
            val |= uint32_t(in[pos++]) << cnt;
            cnt += 8;
        }
        buf = need >= 32 ? 0 : (val >> need);
        cnt -= need;
        return int(val & ((1u << need) - 1));
    }
};
struct Huff { short count[16]; short symbol[320]; };

int decodeSym(Bits &s, const Huff &h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; ++len) {
        code |= s.bits(1);
        if (s.over) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

int construct(Huff &h, const short *length, int n)
{
    for (int len = 0; len <= 15; ++len) h.count[len] = 0;
    for (int sym = 0; sym < n; ++sym) h.count[length[sym]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
    for (int sym = 0; sym < n; ++sym)
        if (length[sym] != 0) h.symbol[offs[length[sym]]++] = short(sym);
    return left;
}

const short kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
                             4097, 6145, 8193, 12289, 16385, 24577};
const short kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits &s, std::string &out, const Huff &lencode, const Huff &distcode)
{
    for (;;) {
        int sym = decodeSym(s, lencode);
        if (sym < 0) return false;
        if (sym < 256) { out.push_back(char(sym)); continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        const int len = kLenBase[sym] + s.bits(kLenExtra[sym]);
        const int dsym = decodeSym(s, distcode);
        if (dsym < 0 || dsym >= 30) return false;
        const size_t dist = size_t(kDistBase[dsym] + s.bits(kDistExtra[dsym]));
        if (s.over || dist > out.size()) return false;
        const size_t from = out.size() - dist;
        for (int i = 0; i < len; ++i) out.push_back(out[from + size_t(i)]);
    }
}

} // namespace

std::string inflate(const std::string &data, bool *complete)
{
    std::string out;
    if (complete) *complete = false;
    size_t start = 0;
    if (data.size() >= 2) {
        const uint8_t cmf = uint8_t(data[0]), flg = uint8_t(data[1]);
        if ((cmf & 0x0f) == 8 && ((cmf << 8) | flg) % 31 == 0) start = (flg & 0x20) ? 6 : 2;     // a zlib header
    }
    Bits s{reinterpret_cast<const uint8_t *>(data.data()) + start, data.size() - start};
    static Huff fixedLen, fixedDist;
    static bool fixedReady = false;
    if (!fixedReady) {
        short l[320];
        int i = 0;
        for (; i < 144; ++i) l[i] = 8;
        for (; i < 256; ++i) l[i] = 9;
        for (; i < 280; ++i) l[i] = 7;
        for (; i < 288; ++i) l[i] = 8;
        construct(fixedLen, l, 288);
        for (i = 0; i < 30; ++i) l[i] = 5;
        construct(fixedDist, l, 30);
        fixedReady = true;
    }
    for (;;) {
        const int last = s.bits(1);
        const int type = s.bits(2);
        if (s.over) return out;
        if (type == 0) {      // stored
            s.buf = 0;
            s.cnt = 0;
            if (s.pos + 4 > s.len) return out;
            const unsigned len = s.in[s.pos] | (s.in[s.pos + 1] << 8);
            s.pos += 4;
            const size_t n = std::min<size_t>(len, s.len - s.pos);
            out.append(reinterpret_cast<const char *>(s.in + s.pos), n);
            s.pos += n;
            if (n < len) return out;
        } else if (type == 1) {
            if (!codes(s, out, fixedLen, fixedDist)) return out;
        } else if (type == 2) {
            const int nlen = s.bits(5) + 257, ndist = s.bits(5) + 1, ncode = s.bits(4) + 4;
            if (s.over || nlen > 286 || ndist > 30) return out;
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            short lengths[320] = {0};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = short(s.bits(3));
            Huff lencode, distcode;
            if (construct(lencode, lengths, 19) != 0) return out;
            int index = 0;
            while (index < nlen + ndist) {
                int sym = decodeSym(s, lencode);
                if (sym < 0) return out;
                if (sym < 16) { lengths[index++] = short(sym); continue; }
                short len = 0;
                int rep;
                if (sym == 16) {
                    if (index == 0) return out;
                    len = lengths[index - 1];
                    rep = 3 + s.bits(2);
                } else if (sym == 17) rep = 3 + s.bits(3);
                else rep = 11 + s.bits(7);
                if (index + rep > nlen + ndist) return out;
                while (rep--) lengths[index++] = len;
            }
            if (lengths[256] == 0) return out;
            const int el = construct(lencode, lengths, nlen);
            if (el < 0 || (el > 0 && nlen - lencode.count[0] != 1)) return out;
            const int ed = construct(distcode, lengths + nlen, ndist);
            if (ed < 0 || (ed > 0 && ndist - distcode.count[0] != 1)) return out;
            if (!codes(s, out, lencode, distcode)) return out;
        } else {
            return out;
        }
        if (last) { if (complete) *complete = true; return out; }
    }
}

namespace {

// ============================================================ objects

struct Obj {
    enum Type : uint8_t { Null, Bool, Int, Real, Str, Name, Arr, Dict, Ref, Op };
    Type t = Null;
    bool b = false;
    double n = 0;
    int num = 0, gen = 0;
    std::string s;
    std::vector<Obj> a;
    std::vector<std::pair<std::string, Obj>> d;
    const Obj *get(const char *k) const
    {
        for (const auto &kv : d) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    bool isNum() const { return t == Int || t == Real; }
    bool isName(const char *x) const { return t == Name && s == x; }
};
const Obj kNull;

inline bool isWs(unsigned char c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
inline bool isDelim(unsigned char c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}
inline int hexVal(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Lexer {
    const std::string &src;
    size_t p, end;
    Lexer(const std::string &s, size_t from = 0, size_t to = std::string::npos) : src(s), p(from), end(std::min(to, s.size())) {}

    void ws()
    {
        while (p < end) {
            const unsigned char c = uint8_t(src[p]);
            if (isWs(c)) ++p;
            else if (c == '%') { while (p < end && src[p] != '\n' && src[p] != '\r') ++p; }
            else break;
        }
    }
    bool atEnd() { ws(); return p >= end; }
    std::string word()
    {
        const size_t s = p;
        while (p < end && !isWs(uint8_t(src[p])) && !isDelim(uint8_t(src[p]))) ++p;
        return src.substr(s, p - s);
    }
    static bool numeric(const std::string &w)
    {
        if (w.empty()) return false;
        size_t i = 0;
        if (w[0] == '+' || w[0] == '-') ++i;
        bool digit = false, dot = false;
        for (; i < w.size(); ++i) {
            if (std::isdigit(uint8_t(w[i]))) digit = true;
            else if (w[i] == '.' && !dot) dot = true;
            else return false;
        }
        return digit;
    }
    Obj next(int depth = 0)
    {
        ws();
        Obj o;
        if (p >= end || depth > 64) return o;
        const unsigned char c = uint8_t(src[p]);
        if (c == '/') {
            ++p;
            o.t = Obj::Name;
            while (p < end && !isWs(uint8_t(src[p])) && !isDelim(uint8_t(src[p]))) {
                if (src[p] == '#' && p + 2 < end && hexVal(uint8_t(src[p + 1])) >= 0 && hexVal(uint8_t(src[p + 2])) >= 0) {
                    o.s.push_back(char(hexVal(uint8_t(src[p + 1])) * 16 + hexVal(uint8_t(src[p + 2]))));
                    p += 3;
                } else o.s.push_back(src[p++]);
            }
            return o;
        }
        if (c == '(') {
            ++p;
            o.t = Obj::Str;
            int nest = 1;
            while (p < end) {
                const char ch = src[p++];
                if (ch == '\\') {
                    if (p >= end) break;
                    const char e = src[p++];
                    switch (e) {
                    case 'n': o.s.push_back('\n'); break;
                    case 'r': o.s.push_back('\r'); break;
                    case 't': o.s.push_back('\t'); break;
                    case 'b': o.s.push_back('\b'); break;
                    case 'f': o.s.push_back('\f'); break;
                    case '\r': if (p < end && src[p] == '\n') ++p; break;
                    case '\n': break;
                    default:
                        if (e >= '0' && e <= '7') {
                            int v = e - '0';
                            for (int k = 0; k < 2 && p < end && src[p] >= '0' && src[p] <= '7'; ++k) v = v * 8 + (src[p++] - '0');
                            o.s.push_back(char(v & 0xff));
                        } else o.s.push_back(e);
                    }
                } else if (ch == '(') { ++nest; o.s.push_back(ch); }
                else if (ch == ')') { if (--nest == 0) break; o.s.push_back(ch); }
                else o.s.push_back(ch);
            }
            return o;
        }
        if (c == '<') {
            if (p + 1 < end && src[p + 1] == '<') {
                p += 2;
                o.t = Obj::Dict;
                for (;;) {
                    ws();
                    if (p >= end) break;
                    if (src[p] == '>' && p + 1 < end && src[p + 1] == '>') { p += 2; break; }
                    Obj key = next(depth + 1);
                    if (key.t != Obj::Name) {
                        if (key.t == Obj::Op && (key.s == ">" || key.s == "endobj" || key.s == "stream")) break;
                        continue;
                    }
                    Obj val = next(depth + 1);
                    o.d.emplace_back(std::move(key.s), std::move(val));
                }
                return o;
            }
            ++p;
            o.t = Obj::Str;
            int hi = -1;
            while (p < end && src[p] != '>') {
                const int v = hexVal(uint8_t(src[p++]));
                if (v < 0) continue;
                if (hi < 0) hi = v;
                else { o.s.push_back(char(hi * 16 + v)); hi = -1; }
            }
            if (hi >= 0) o.s.push_back(char(hi * 16));
            if (p < end) ++p;
            return o;
        }
        if (c == '[') {
            ++p;
            o.t = Obj::Arr;
            for (;;) {
                ws();
                if (p >= end) break;
                if (src[p] == ']') { ++p; break; }
                Obj v = next(depth + 1);
                if (v.t == Obj::Op && (v.s == "endobj" || v.s == "stream")) break;
                o.a.push_back(std::move(v));
            }
            return o;
        }
        if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}') {
            ++p;
            o.t = Obj::Op;
            o.s = std::string(1, char(c));
            return o;
        }
        std::string w = word();
        if (w.empty()) { ++p; return o; }
        if (numeric(w)) {
            const bool integer = w.find('.') == std::string::npos;
            o.t = integer ? Obj::Int : Obj::Real;
            o.n = std::atof(w.c_str());
            if (integer && o.n >= 0) {      // perhaps a reference: "12 0 R"
                const size_t save = p;
                ws();
                const std::string w2 = word();
                if (!w2.empty() && std::all_of(w2.begin(), w2.end(), [](char ch) { return std::isdigit(uint8_t(ch)); })) {
                    ws();
                    if (p < end && src[p] == 'R' && (p + 1 >= end || isWs(uint8_t(src[p + 1])) || isDelim(uint8_t(src[p + 1])))) {
                        ++p;
                        o.t = Obj::Ref;
                        o.num = int(o.n);
                        o.gen = std::atoi(w2.c_str());
                        return o;
                    }
                }
                p = save;
            }
            return o;
        }
        if (w == "true" || w == "false") { o.t = Obj::Bool; o.b = w == "true"; return o; }
        if (w == "null") return o;
        o.t = Obj::Op;
        o.s = std::move(w);
        return o;
    }
};

// ============================================================ text helpers

void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp == 0xFEFF || cp == 0xFFFE || cp == 0xAD || cp == 0) return;      // byte-order marks, soft hyphens
    if (cp >= 0xFB00 && cp <= 0xFB06) {      // ligatures: "classiﬁcation" must read as "classification"
        static const char *lig[] = {"ff", "fi", "fl", "ffi", "ffl", "st", "st"};
        out += lig[cp - 0xFB00];
        return;
    }
    if (cp == 0xA0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x200A || cp == 0x202F) cp = ' ';
    if (cp == '\t' || cp == '\r' || cp == '\n') cp = ' ';
    if (cp < 0x20) return;
    if (cp < 0x80) out.push_back(char(cp));
    else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return;
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

std::string utf16beToUtf8(const std::string &b)
{
    std::string out;
    for (size_t i = 0; i + 1 < b.size(); i += 2) {
        uint32_t u = (uint32_t(uint8_t(b[i])) << 8) | uint8_t(b[i + 1]);
        if (u >= 0xD800 && u <= 0xDBFF && i + 3 < b.size()) {
            const uint32_t lo = (uint32_t(uint8_t(b[i + 2])) << 8) | uint8_t(b[i + 3]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        appendUtf8(out, u);
    }
    if (b.size() == 1) appendUtf8(out, uint8_t(b[0]));
    return out;
}

std::string glyphToUtf8(std::string name)
{
    const size_t dot = name.find('.');
    if (dot != std::string::npos && dot > 0) name.resize(dot);
    if (name.find('_') != std::string::npos) {      // "f_f_i": a ligature of its parts
        std::string out;
        size_t i = 0;
        while (i <= name.size()) {
            size_t j = name.find('_', i);
            if (j == std::string::npos) j = name.size();
            out += glyphToUtf8(name.substr(i, j - i));
            i = j + 1;
        }
        return out;
    }
    const auto *b = std::begin(kGlyphNames), *e = std::end(kGlyphNames);
    const auto *it = std::lower_bound(b, e, name, [](const GlyphName &g, const std::string &n) { return std::strcmp(g.name, n.c_str()) < 0; });
    std::string out;
    if (it != e && name == it->name) { appendUtf8(out, it->code); return out; }
    auto hexRun = [&](size_t from, size_t count) {
        uint32_t v = 0;
        for (size_t k = from; k < from + count; ++k) {
            const int h = hexVal(uint8_t(name[k]));
            if (h < 0) return uint32_t(0xFFFFFFFF);
            v = v * 16 + uint32_t(h);
        }
        return v;
    };
    if (name.size() >= 7 && name.compare(0, 3, "uni") == 0 && (name.size() - 3) % 4 == 0) {
        for (size_t k = 3; k < name.size(); k += 4) { const uint32_t v = hexRun(k, 4); if (v != 0xFFFFFFFF) appendUtf8(out, v); }
        return out;
    }
    if (name.size() >= 5 && name.size() <= 7 && name[0] == 'u') {
        const uint32_t v = hexRun(1, name.size() - 1);
        if (v != 0xFFFFFFFF) appendUtf8(out, v);
        return out;
    }
    if (name.size() == 1 && uint8_t(name[0]) < 0x80) return name;
    return out;
}

// ============================================================ matrices

struct M {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};
M mul(const M &x, const M &y)
{
    return {x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d, x.c * y.a + x.d * y.c,
            x.c * y.b + x.d * y.d, x.e * y.a + x.f * y.c + y.e, x.e * y.b + x.f * y.d + y.f};
}
M matrixOf(const Obj &arr)
{
    M m;
    if (arr.t == Obj::Arr && arr.a.size() == 6) {
        m.a = arr.a[0].n; m.b = arr.a[1].n; m.c = arr.a[2].n; m.d = arr.a[3].n; m.e = arr.a[4].n; m.f = arr.a[5].n;
    }
    return m;
}

// ============================================================ fonts

struct Font {
    std::string name, subtype, encoding;      // for the diagnostics (PDFTEXT_DEBUG)
    mutable long used = 0, unmapped = 0;
    bool composite = false;
    std::vector<std::array<uint32_t, 3>> spaces;           // code byte length, lowest, highest
    std::unordered_map<uint32_t, std::string> uni;         // ToUnicode
    std::array<std::string, 256> simple;                   // from the encoding (simple fonts)
    std::unordered_map<uint32_t, double> widths;           // glyph widths in glyph space
    double defaultWidth = 0;
    double scale = 0.001;                                  // glyph space to text space
    std::string text(uint32_t code) const
    {
        const auto it = uni.find(code);
        if (it != uni.end()) return it->second;
        if (!composite && code < 256) return simple[code];
        return {};
    }
    double width(uint32_t code) const
    {
        const auto it = widths.find(code);
        return (it != widths.end() ? it->second : defaultWidth) * scale;
    }
};

// x along the text's own direction and y across it, so rotated text (landscape tables) reads like any other
struct Glyph { double x, y, x2, size; int dir; std::string text; };

// ============================================================ the reader

class Reader {
public:
    explicit Reader(const std::string &pdf) : src(pdf) {}
    Document run();

private:
    struct Entry {
        Obj obj;
        size_t offset = 0;
        int gen = 0;
        bool stream = false;
        size_t sStart = 0, sLen = 0;
        bool inObjStm = false;
        bool ready = false;      // strings decrypted
    };
    const std::string &src;
    std::unordered_map<int, Entry> objs;
    std::unordered_map<int, std::string> streams;
    std::vector<std::pair<size_t, Obj>> trailers;
    // encryption
    bool enc = false;
    int encV = 0, encR = 0, encryptNum = -1;
    std::string fileKey, stmMethod = "V2", strMethod = "V2";
    // fonts, by font object number (or a made-up key for direct fonts)
    std::unordered_map<std::string, std::shared_ptr<Font>> fonts;
    Document doc;

    void scan();
    void expandObjStms();
    Entry *entry(int num);
    const Obj &resolve(const Obj &o, int depth = 0);
    std::string streamOf(int num);
    std::string streamOf(const Obj &ref) { return ref.t == Obj::Ref ? streamOf(ref.num) : std::string(); }
    std::string decode(const Obj &dict, std::string data);
    std::string objectKey(int num, int gen, bool aes) const;
    void decryptStrings(Obj &o, const std::string &key, const std::string &method);
    std::string decrypt(const std::string &method, const std::string &key, const std::string &data) const;
    bool setupEncryption(const Obj &encDict, const std::string &id0);
    std::shared_ptr<Font> loadFont(const Obj &fontRef, const std::string &fallbackKey);
    void parseToUnicode(Font &f, const std::string &cmap);
    void content(const std::string &data, const Obj &resources, M ctm, int depth, std::vector<Glyph> &out);
    void collectPages(const Obj &node, const Obj *res, int depth, std::set<const Obj *> &seen,
                      std::vector<std::pair<const Obj *, const Obj *>> &pages);
};

void Reader::scan()
{
    size_t p = 0;
    for (;;) {
        const size_t k = src.find("obj", p);
        if (k == std::string::npos) break;
        p = k + 3;
        if (k + 3 < src.size() && !isWs(uint8_t(src[k + 3])) && !isDelim(uint8_t(src[k + 3]))) continue;
        // walk back over "<num> <gen> "
        size_t q = k;
        if (q == 0 || !isWs(uint8_t(src[q - 1]))) continue;
        while (q > 0 && isWs(uint8_t(src[q - 1]))) --q;
        size_t ge = q;
        while (q > 0 && std::isdigit(uint8_t(src[q - 1]))) --q;
        if (q == ge) continue;
        const int gen = std::atoi(src.substr(q, ge - q).c_str());
        if (q == 0 || !isWs(uint8_t(src[q - 1]))) continue;
        while (q > 0 && isWs(uint8_t(src[q - 1]))) --q;
        const size_t ne = q;
        while (q > 0 && std::isdigit(uint8_t(src[q - 1]))) --q;
        if (q == ne || ne - q > 10) continue;
        if (q > 0 && !isWs(uint8_t(src[q - 1])) && !isDelim(uint8_t(src[q - 1]))) continue;
        const int num = std::atoi(src.substr(q, ne - q).c_str());

        Lexer lx(src, k + 3);
        Entry e;
        e.obj = lx.next();
        e.offset = q;
        e.gen = gen;
        size_t after = lx.p;
        lx.ws();
        if (e.obj.t == Obj::Dict && src.compare(lx.p, 6, "stream") == 0) {
            size_t s = lx.p + 6;
            if (s < src.size() && src[s] == '\r') ++s;
            if (s < src.size() && src[s] == '\n') ++s;
            size_t len = std::string::npos;
            const Obj *L = e.obj.get("Length");
            if (L && L->t == Obj::Int && L->n >= 0 && s + size_t(L->n) <= src.size()) {
                size_t z = s + size_t(L->n);
                while (z < src.size() && isWs(uint8_t(src[z]))) ++z;
                if (src.compare(z, 9, "endstream") == 0) len = size_t(L->n);
            }
            if (len == std::string::npos) {
                size_t z = src.find("endstream", s);
                if (z == std::string::npos) z = src.size();
                len = z - s;
                while (len > 0 && (src[s + len - 1] == '\n' || src[s + len - 1] == '\r')) --len;
            }
            e.stream = true;
            e.sStart = s;
            e.sLen = len;
            after = s + len;
        }
        p = std::max(p, after);
        if (e.obj.t == Obj::Dict && (e.obj.get("Type") && e.obj.get("Type")->isName("XRef")) && e.obj.get("Root"))
            trailers.emplace_back(q, e.obj);
        auto it = objs.find(num);
        if (it == objs.end() || it->second.offset <= e.offset) objs[num] = std::move(e);
    }
    // classic trailers
    for (size_t t = src.find("trailer"); t != std::string::npos; t = src.find("trailer", t + 7)) {
        Lexer lx(src, t + 7);
        Obj d = lx.next();
        if (d.t == Obj::Dict) trailers.emplace_back(t, std::move(d));
    }
}

Reader::Entry *Reader::entry(int num)
{
    auto it = objs.find(num);
    if (it == objs.end()) return nullptr;
    Entry &e = it->second;
    if (!e.ready) {
        e.ready = true;
        if (enc && !e.inObjStm && num != encryptNum) {
            const bool xref = e.obj.get("Type") && e.obj.get("Type")->isName("XRef");
            if (!xref) decryptStrings(e.obj, objectKey(num, e.gen, strMethod != "V2"), strMethod);
        }
    }
    return &e;
}

const Obj &Reader::resolve(const Obj &o, int depth)
{
    if (o.t != Obj::Ref) return o;
    if (depth > 16) return kNull;
    Entry *e = entry(o.num);
    if (!e) return kNull;
    return resolve(e->obj, depth + 1);
}

std::string Reader::objectKey(int num, int gen, bool aes) const
{
    if (encV >= 5) return fileKey;
    std::string s = fileKey;
    s.push_back(char(num & 0xff));
    s.push_back(char((num >> 8) & 0xff));
    s.push_back(char((num >> 16) & 0xff));
    s.push_back(char(gen & 0xff));
    s.push_back(char((gen >> 8) & 0xff));
    if (aes) s += "sAlT";
    return pdfcrypto::md5(s).substr(0, std::min<size_t>(fileKey.size() + 5, 16));
}

std::string Reader::decrypt(const std::string &method, const std::string &key, const std::string &data) const
{
    if (method == "None" || method == "Identity") return data;
    if (method == "AESV2" || method == "AESV3") return pdfcrypto::aesDecryptCbc(key, data);
    return pdfcrypto::rc4(key, data);
}

void Reader::decryptStrings(Obj &o, const std::string &key, const std::string &method)
{
    if (o.t == Obj::Str) o.s = decrypt(method, key, o.s);
    else if (o.t == Obj::Arr) for (Obj &x : o.a) decryptStrings(x, key, method);
    else if (o.t == Obj::Dict) for (auto &kv : o.d) decryptStrings(kv.second, key, method);
}

bool Reader::setupEncryption(const Obj &ed, const std::string &id0)
{
    const Obj *filter = ed.get("Filter");
    if (!filter || !filter->isName("Standard")) { doc.error = "This PDF uses a kind of protection Streamline can't open."; return false; }
    auto num = [&](const char *k, double def) { const Obj *o = ed.get(k); return o && o->isNum() ? o->n : def; };
    auto str = [&](const char *k) { const Obj *o = ed.get(k); return o && o->t == Obj::Str ? o->s : std::string(); };
    encV = int(num("V", 0));
    encR = int(num("R", 2));
    const int bits = int(num("Length", 40));
    const std::string O = str("O"), U = str("U");
    const int32_t P = int32_t(int64_t(num("P", 0)));
    const Obj *em = ed.get("EncryptMetadata");
    const bool encryptMetadata = !(em && em->t == Obj::Bool && !em->b);
    if (encV == 4 || encV == 5) {
        const Obj *cf = ed.get("CF");
        auto method = [&](const char *which) -> std::string {
            const Obj *n = ed.get(which);
            const std::string name = n && n->t == Obj::Name ? n->s : "Identity";
            if (name == "Identity") return "Identity";
            const Obj *f = cf ? cf->get(name.c_str()) : nullptr;
            const Obj *cfm = f ? f->get("CFM") : nullptr;
            return cfm && cfm->t == Obj::Name ? cfm->s : "V2";
        };
        stmMethod = method("StmF");
        strMethod = method("StrF");
    }
    static const unsigned char pad[32] = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                          0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
    const std::string padding(reinterpret_cast<const char *>(pad), 32);
    if (encR <= 4) {
        std::string in = padding + O.substr(0, 32);
        for (int i = 0; i < 4; ++i) in.push_back(char((uint32_t(P) >> (i * 8)) & 0xff));
        in += id0;
        if (encR >= 4 && !encryptMetadata) in += std::string(4, char(0xff));
        std::string h = pdfcrypto::md5(in);
        const size_t n = encR == 2 ? 5 : size_t(std::clamp(bits / 8, 5, 16));
        if (encR >= 3)
            for (int i = 0; i < 50; ++i) h = pdfcrypto::md5(h.substr(0, n));
        fileKey = h.substr(0, n);
        // does the empty password open it?
        bool ok;
        if (encR == 2) ok = pdfcrypto::rc4(fileKey, padding) == U.substr(0, 32);
        else {
            std::string x = pdfcrypto::rc4(fileKey, pdfcrypto::md5(padding + id0));
            for (int i = 1; i <= 19; ++i) {
                std::string k = fileKey;
                for (char &c : k) c = char(uint8_t(c) ^ uint8_t(i));
                x = pdfcrypto::rc4(k, x);
            }
            ok = U.size() >= 16 && x == U.substr(0, 16);
        }
        if (!ok) { doc.error = "This PDF needs a password to open."; return false; }
        if (encV < 4) stmMethod = strMethod = "V2";
        return true;
    }
    // AES-256: PDF 1.7 extension level 3 (R5) and PDF 2.0 (R6)
    if (U.size() < 48) { doc.error = "This PDF's protection couldn't be read."; return false; }
    auto hash = [&](const std::string &salt) {
        if (encR == 5) return pdfcrypto::sha256(salt);
        std::string K = pdfcrypto::sha256(salt), E;
        int i = 0;
        for (;;) {
            std::string K1;
            for (int r = 0; r < 64; ++r) K1 += K;      // password and user data are empty
            E = pdfcrypto::aesEncryptCbcNoPad(K.substr(0, 16), K.substr(16, 16), K1);
            int mod = 0;
            for (int j = 0; j < 16; ++j) mod = (mod * 256 + uint8_t(E[size_t(j)])) % 3;
            K = mod == 0 ? pdfcrypto::sha256(E) : mod == 1 ? pdfcrypto::sha384(E) : pdfcrypto::sha512(E);
            ++i;
            if (!(i < 64 || int(uint8_t(E.back())) > i - 32)) break;
        }
        return K.substr(0, 32);
    };
    if (hash(U.substr(32, 8)) != U.substr(0, 32)) { doc.error = "This PDF needs a password to open."; return false; }
    const std::string ik = hash(U.substr(40, 8));
    fileKey = pdfcrypto::aesDecryptCbcNoPad(ik, std::string(16, '\0'), str("UE").substr(0, 32));
    if (fileKey.size() != 32) { doc.error = "This PDF's protection couldn't be read."; return false; }
    if (stmMethod == "V2") stmMethod = "AESV3";
    if (strMethod == "V2") strMethod = "AESV3";
    return true;
}

std::string Reader::decode(const Obj &dict, std::string data)
{
    const Obj &filterObj = resolve(dict.get("Filter") ? *dict.get("Filter") : kNull);
    const Obj &parmsObj = resolve(dict.get("DecodeParms") ? *dict.get("DecodeParms") : kNull);
    std::vector<std::string> filters;
    std::vector<const Obj *> parms;
    if (filterObj.t == Obj::Name) { filters.push_back(filterObj.s); parms.push_back(&parmsObj); }
    else if (filterObj.t == Obj::Arr)
        for (size_t i = 0; i < filterObj.a.size(); ++i) {
            filters.push_back(filterObj.a[i].s);
            parms.push_back(parmsObj.t == Obj::Arr && i < parmsObj.a.size() ? &resolve(parmsObj.a[i]) : &kNull);
        }
    for (size_t i = 0; i < filters.size(); ++i) {
        const std::string &f = filters[i];
        if (f == "FlateDecode" || f == "Fl") data = inflate(data);
        else if (f == "ASCIIHexDecode" || f == "AHx") {
            std::string out;
            int hi = -1;
            for (char c : data) {
                if (c == '>') break;
                const int v = hexVal(uint8_t(c));
                if (v < 0) continue;
                if (hi < 0) hi = v; else { out.push_back(char(hi * 16 + v)); hi = -1; }
            }
            if (hi >= 0) out.push_back(char(hi * 16));
            data = out;
        } else if (f == "ASCII85Decode" || f == "A85") {
            std::string out;
            uint32_t tuple = 0;
            int count = 0;
            size_t i2 = 0;
            if (data.compare(0, 2, "<~") == 0) i2 = 2;
            for (; i2 < data.size(); ++i2) {
                const char c = data[i2];
                if (c == '~') break;
                if (isWs(uint8_t(c))) continue;
                if (c == 'z' && count == 0) { out.append(4, '\0'); continue; }
                if (c < '!' || c > 'u') continue;
                tuple = tuple * 85 + uint32_t(c - '!');
                if (++count == 5) {
                    for (int k = 3; k >= 0; --k) out.push_back(char((tuple >> (k * 8)) & 0xff));
                    tuple = 0;
                    count = 0;
                }
            }
            if (count > 1) {
                for (int k = count; k < 5; ++k) tuple = tuple * 85 + 84;
                for (int k = 3; k >= 5 - count; --k) out.push_back(char((tuple >> (k * 8)) & 0xff));
            }
            data = out;
        } else if (f == "LZWDecode" || f == "LZW") {
            const Obj *ec = parms[i]->get("EarlyChange");
            const int early = ec && ec->isNum() ? int(ec->n) : 1;
            std::string out;
            std::vector<std::string> table;
            auto reset = [&]() { table.clear(); for (int c = 0; c < 256; ++c) table.emplace_back(1, char(c)); table.emplace_back(); table.emplace_back(); };
            reset();
            int width = 9;
            uint32_t buf = 0;
            int nbits = 0;
            std::string prev;
            bool havePrev = false;
            for (unsigned char byte : data) {
                buf = (buf << 8) | byte;
                nbits += 8;
                while (nbits >= width) {
                    const int code = int((buf >> (nbits - width)) & ((1u << width) - 1));
                    nbits -= width;
                    if (code == 256) { reset(); width = 9; havePrev = false; continue; }
                    if (code == 257) { nbits = 0; break; }
                    std::string entryStr;
                    if (code < int(table.size()) && code != 256 && code != 257) entryStr = table[size_t(code)];
                    else if (havePrev && code == int(table.size())) entryStr = prev + prev[0];
                    else { nbits = 0; break; }
                    out += entryStr;
                    if (havePrev && table.size() < 4096) table.push_back(prev + entryStr[0]);
                    prev = entryStr;
                    havePrev = true;
                    const int next = int(table.size()) + early;
                    width = next >= 2048 ? 12 : next >= 1024 ? 11 : next >= 512 ? 10 : 9;
                }
            }
            data = out;
        } else if (f == "RunLengthDecode" || f == "RL") {
            std::string out;
            for (size_t k = 0; k < data.size();) {
                const int len = uint8_t(data[k++]);
                if (len == 128) break;
                if (len < 128) { out.append(data, k, size_t(len + 1)); k += size_t(len + 1); }
                else if (k < data.size()) { out.append(size_t(257 - len), data[k]); ++k; }
            }
            data = out;
        } else if (f == "Crypt") {
            continue;
        } else {
            return {};      // pictures (JPEG, JBIG2…): no text in them for us
        }
        // PNG predictors (object and cross-reference streams use them)
        const Obj *pred = parms[i]->get("Predictor");
        if (pred && pred->isNum() && pred->n >= 10) {
            auto pnum = [&](const char *k, int def) { const Obj *o = parms[i]->get(k); return o && o->isNum() ? int(o->n) : def; };
            const int colors = pnum("Colors", 1), bpc = pnum("BitsPerComponent", 8), columns = pnum("Columns", 1);
            const size_t bpp = size_t(std::max(1, colors * bpc / 8));
            const size_t row = size_t((columns * colors * bpc + 7) / 8);
            std::string out;
            std::string prior(row, '\0');
            for (size_t k = 0; k + 1 + row <= data.size(); k += row + 1) {
                const int type = uint8_t(data[k]);
                std::string cur = data.substr(k + 1, row);
                for (size_t x = 0; x < row; ++x) {
                    const int left = x >= bpp ? uint8_t(cur[x - bpp]) : 0;
                    const int up = uint8_t(prior[x]);
                    const int ul = x >= bpp ? uint8_t(prior[x - bpp]) : 0;
                    int v = uint8_t(cur[x]);
                    switch (type) {
                    case 1: v += left; break;
                    case 2: v += up; break;
                    case 3: v += (left + up) / 2; break;
                    case 4: {
                        const int pp = left + up - ul, pa = std::abs(pp - left), pb = std::abs(pp - up), pc = std::abs(pp - ul);
                        v += (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : ul);
                        break;
                    }
                    default: break;
                    }
                    cur[x] = char(v & 0xff);
                }
                out += cur;
                prior = cur;
            }
            data = out;
        }
    }
    return data;
}

std::string Reader::streamOf(int num)
{
    const auto cached = streams.find(num);
    if (cached != streams.end()) return cached->second;
    Entry *e = entry(num);
    if (!e || !e->stream) return {};
    std::string data = src.substr(e->sStart, e->sLen);
    const bool xref = e->obj.get("Type") && e->obj.get("Type")->isName("XRef");
    if (enc && !xref) data = decrypt(stmMethod, objectKey(num, e->gen, stmMethod != "V2"), data);
    data = decode(e->obj, std::move(data));
    if (data.size() < 4 * 1024 * 1024) streams[num] = data;
    return data;
}

void Reader::expandObjStms()
{
    std::vector<std::pair<int, size_t>> containers;
    for (auto &kv : objs)
        if (kv.second.stream && kv.second.obj.get("Type") && kv.second.obj.get("Type")->isName("ObjStm"))
            containers.emplace_back(kv.first, kv.second.offset);
    std::sort(containers.begin(), containers.end(), [](const auto &x, const auto &y) { return x.second < y.second; });
    for (const auto &[cnum, coff] : containers) {
        const Obj &dict = entry(cnum)->obj;
        const int n = dict.get("N") && dict.get("N")->isNum() ? int(dict.get("N")->n) : 0;
        const size_t first = dict.get("First") && dict.get("First")->isNum() ? size_t(dict.get("First")->n) : 0;
        const std::string data = streamOf(cnum);
        streams.erase(cnum);
        if (data.empty() || first >= data.size()) continue;
        Lexer head(data, 0, first);
        std::vector<std::pair<int, size_t>> list;
        for (int i = 0; i < n; ++i) {
            const Obj a = head.next(), b = head.next();
            if (!a.isNum() || !b.isNum()) break;
            list.emplace_back(int(a.n), size_t(b.n));
        }
        for (size_t i = 0; i < list.size(); ++i) {
            const size_t from = first + list[i].second;
            const size_t to = i + 1 < list.size() ? first + list[i + 1].second : data.size();
            if (from >= data.size()) continue;
            Lexer lx(data, from, to);
            Entry e;
            e.obj = lx.next();
            e.offset = coff;
            e.inObjStm = true;
            e.ready = true;
            auto it = objs.find(list[i].first);
            if (it == objs.end() || it->second.offset < coff) objs[list[i].first] = std::move(e);
        }
    }
}

void Reader::parseToUnicode(Font &f, const std::string &cmap)
{
    Lexer lx(cmap);
    std::vector<Obj> ops;
    auto code = [](const std::string &s) { uint32_t v = 0; for (unsigned char c : s) v = (v << 8) | c; return v; };
    while (!lx.atEnd()) {
        Obj o = lx.next();
        if (o.t != Obj::Op) { ops.push_back(std::move(o)); if (ops.size() > 4096) ops.erase(ops.begin(), ops.begin() + 1024); continue; }
        if (o.s == "endcodespacerange") {
            for (size_t i = 0; i + 1 < ops.size(); i += 2)
                if (ops[i].t == Obj::Str && ops[i + 1].t == Obj::Str && !ops[i].s.empty())
                    f.spaces.push_back({uint32_t(ops[i].s.size()), code(ops[i].s), code(ops[i + 1].s)});
        } else if (o.s == "endbfchar") {
            for (size_t i = 0; i + 1 < ops.size(); i += 2) {
                if (ops[i].t != Obj::Str) continue;
                if (ops[i + 1].t == Obj::Str) f.uni[code(ops[i].s)] = utf16beToUtf8(ops[i + 1].s);
                else if (ops[i + 1].t == Obj::Name) f.uni[code(ops[i].s)] = glyphToUtf8(ops[i + 1].s);
            }
        } else if (o.s == "endbfrange") {
            for (size_t i = 0; i + 2 < ops.size(); i += 3) {
                if (ops[i].t != Obj::Str || ops[i + 1].t != Obj::Str) continue;
                const uint32_t lo = code(ops[i].s), hi = code(ops[i + 1].s);
                if (hi < lo || hi - lo > 65535) continue;
                if (ops[i + 2].t == Obj::Str) {
                    std::string dst = ops[i + 2].s;
                    for (uint32_t c = lo; c <= hi; ++c) {
                        f.uni[c] = utf16beToUtf8(dst);
                        // the next code maps to the next character: add one to the last UTF-16 unit
                        if (dst.size() >= 2) {
                            uint32_t last = (uint32_t(uint8_t(dst[dst.size() - 2])) << 8) | uint8_t(dst.back());
                            ++last;
                            dst[dst.size() - 2] = char((last >> 8) & 0xff);
                            dst.back() = char(last & 0xff);
                        } else if (dst.size() == 1) dst[0] = char(uint8_t(dst[0]) + 1);
                    }
                } else if (ops[i + 2].t == Obj::Arr) {
                    for (uint32_t c = lo; c <= hi && c - lo < ops[i + 2].a.size(); ++c)
                        if (ops[i + 2].a[c - lo].t == Obj::Str) f.uni[c] = utf16beToUtf8(ops[i + 2].a[c - lo].s);
                }
            }
        }
        ops.clear();
    }
}

std::shared_ptr<Font> Reader::loadFont(const Obj &fontRef, const std::string &fallbackKey)
{
    const std::string key = fontRef.t == Obj::Ref ? std::to_string(fontRef.num) : fallbackKey;
    const auto cached = fonts.find(key);
    if (cached != fonts.end()) return cached->second;
    auto f = std::make_shared<Font>();
    fonts[key] = f;
    const Obj &fd = resolve(fontRef);
    if (fd.t != Obj::Dict) return f;
    const Obj *sub = fd.get("Subtype");
    f->composite = sub && sub->isName("Type0");
    if (const Obj *bf = fd.get("BaseFont")) f->name = bf->s;
    if (sub) f->subtype = sub->s;
    if (const Obj *e = fd.get("Encoding")) f->encoding = e->t == Obj::Name ? e->s : e->t == Obj::Ref ? "stream/dict" : "dict";
    const Obj *tu = fd.get("ToUnicode");
    if (tu && tu->t == Obj::Ref) parseToUnicode(*f, streamOf(tu->num));

    if (f->composite) {
        const Obj &descs = resolve(fd.get("DescendantFonts") ? *fd.get("DescendantFonts") : kNull);
        const Obj &desc = descs.t == Obj::Arr && !descs.a.empty() ? resolve(descs.a[0]) : kNull;
        f->defaultWidth = 1000;
        if (desc.t == Obj::Dict) {
            const Obj *dw = desc.get("DW");
            if (dw && dw->isNum()) f->defaultWidth = dw->n;
            const Obj &w = resolve(desc.get("W") ? *desc.get("W") : kNull);
            if (w.t == Obj::Arr) {
                for (size_t i = 0; i < w.a.size();) {
                    if (!w.a[i].isNum()) { ++i; continue; }
                    const uint32_t c0 = uint32_t(w.a[i].n);
                    if (i + 1 < w.a.size()) {
                        const Obj &nx = resolve(w.a[i + 1]);
                        if (nx.t == Obj::Arr) {
                            for (size_t k = 0; k < nx.a.size(); ++k) f->widths[c0 + uint32_t(k)] = resolve(nx.a[k]).n;
                            i += 2;
                            continue;
                        }
                        if (i + 2 < w.a.size() && nx.isNum() && w.a[i + 2].isNum()) {
                            const uint32_t c1 = uint32_t(nx.n);
                            for (uint32_t c = c0; c <= c1 && c - c0 < 65536; ++c) f->widths[c] = w.a[i + 2].n;
                            i += 3;
                            continue;
                        }
                    }
                    ++i;
                }
            }
        }
        // the font's own encoding: Identity-H (two bytes a character) unless it says otherwise
        const Obj *encObj = fd.get("Encoding");
        if (encObj && encObj->t == Obj::Ref) {
            Font tmp;
            parseToUnicode(tmp, streamOf(encObj->num));
            if (!tmp.spaces.empty()) f->spaces = tmp.spaces;
        } else if (encObj && encObj->t == Obj::Name && encObj->s.find("Identity") != std::string::npos) {
            f->spaces = {{2, 0, 0xFFFF}};
        }
        if (f->spaces.empty()) f->spaces = {{2, 0, 0xFFFF}};
        return f;
    }

    // a simple font: one byte a character
    const uint16_t *base = kWinAnsi;
    bool standard = false;
    const Obj &encObj = resolve(fd.get("Encoding") ? *fd.get("Encoding") : kNull);
    const Obj *baseName = encObj.t == Obj::Name ? &encObj : (encObj.t == Obj::Dict ? encObj.get("BaseEncoding") : nullptr);
    if (baseName && baseName->t == Obj::Name) {
        if (baseName->s == "MacRomanEncoding") base = kMacRoman;
        else if (baseName->s == "StandardEncoding") standard = true;
    }
    for (int c = 0; c < 256; ++c) {
        uint32_t u = base[c];
        if (standard && c == 0x27) u = 0x2019;
        if (standard && c == 0x60) u = 0x2018;
        std::string s;
        if (u) appendUtf8(s, u);
        f->simple[size_t(c)] = s;
    }
    if (encObj.t == Obj::Dict) {
        const Obj &diff = resolve(encObj.get("Differences") ? *encObj.get("Differences") : kNull);
        int c = 0;
        if (diff.t == Obj::Arr)
            for (const Obj &x : diff.a) {
                if (x.isNum()) c = int(x.n);
                else if (x.t == Obj::Name) {
                    if (c >= 0 && c < 256) f->simple[size_t(c)] = glyphToUtf8(x.s);
                    ++c;
                }
            }
    }
    if (sub && sub->isName("Type3")) {
        const Obj &fm = resolve(fd.get("FontMatrix") ? *fd.get("FontMatrix") : kNull);
        if (fm.t == Obj::Arr && fm.a.size() == 6 && fm.a[0].isNum()) f->scale = fm.a[0].n;
    }
    const Obj *fc = fd.get("FirstChar");
    const Obj &ws = resolve(fd.get("Widths") ? *fd.get("Widths") : kNull);
    const int first = fc && fc->isNum() ? int(fc->n) : 0;
    double sum = 0;
    int counted = 0;
    if (ws.t == Obj::Arr)
        for (size_t i = 0; i < ws.a.size(); ++i) {
            const double w = resolve(ws.a[i]).n;
            f->widths[uint32_t(first) + uint32_t(i)] = w;
            if (w > 0) { sum += w; ++counted; }
        }
    const Obj &desc = resolve(fd.get("FontDescriptor") ? *fd.get("FontDescriptor") : kNull);
    const Obj *mw = desc.t == Obj::Dict ? desc.get("MissingWidth") : nullptr;
    f->defaultWidth = mw && mw->isNum() && mw->n > 0 ? mw->n : (counted ? sum / counted : 500);
    if (sub && sub->isName("Type3") && f->defaultWidth == 500) f->defaultWidth = counted ? sum / counted : 0;
    return f;
}

void Reader::content(const std::string &data, const Obj &resources, M ctm, int depth, std::vector<Glyph> &out)
{
    struct GS {
        M ctm;
        std::shared_ptr<Font> font;
        double fs = 0, tc = 0, tw = 0, th = 1, tl = 0, rise = 0;
    };
    GS gs;
    gs.ctm = ctm;
    std::vector<GS> stack;
    M tm, tlm;
    const Obj &fontDict = resolve(resources.get("Font") ? *resources.get("Font") : kNull);
    const Obj &xobjDict = resolve(resources.get("XObject") ? *resources.get("XObject") : kNull);

    auto show = [&](const std::string &bytes) {
        if (!gs.font) return;
        const Font &f = *gs.font;
        size_t i = 0;
        while (i < bytes.size()) {
            uint32_t code = 0;
            size_t n = 1;
            if (f.composite) {
                n = 0;
                for (const auto &sp : f.spaces) {      // the shortest code length whose range it falls in
                    const size_t len = sp[0];
                    if (len == 0 || i + len > bytes.size()) continue;
                    uint32_t c = 0;
                    for (size_t k = 0; k < len; ++k) c = (c << 8) | uint8_t(bytes[i + k]);
                    if (c >= sp[1] && c <= sp[2] && (n == 0 || len < n)) { n = len; code = c; }
                }
                if (n == 0) {
                    n = std::min<size_t>(2, bytes.size() - i);
                    code = 0;
                    for (size_t k = 0; k < n; ++k) code = (code << 8) | uint8_t(bytes[i + k]);
                }
            } else {
                code = uint8_t(bytes[i]);
            }
            i += n;
            const M trm = mul(M{gs.fs * gs.th, 0, 0, gs.fs, 0, gs.rise}, mul(tm, gs.ctm));
            const double w0 = f.width(code);
            const double tx = (w0 * gs.fs + gs.tc + ((n == 1 && code == 32) ? gs.tw : 0)) * gs.th;
            tm = mul(M{1, 0, 0, 1, tx, 0}, tm);
            const M end = mul(M{gs.fs * gs.th, 0, 0, gs.fs, 0, gs.rise}, mul(tm, gs.ctm));
            double dx = trm.a, dy = trm.b;
            const double len = std::hypot(dx, dy);
            if (len > 1e-9) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
            Glyph g;
            g.x = trm.e * dx + trm.f * dy;
            g.y = -trm.e * dy + trm.f * dx;
            g.x2 = end.e * dx + end.f * dy;
            g.dir = int(std::lround(std::atan2(dy, dx) * 8 / 3.14159265358979));      // in 22.5° steps
            g.size = std::hypot(trm.c, trm.d);
            if (g.size <= 0) g.size = std::abs(gs.fs);
            g.text = f.text(code);
            ++f.used;
            if (g.text.empty()) ++f.unmapped;
            out.push_back(std::move(g));
        }
    };

    Lexer lx(data);
    std::vector<Obj> ops;
    while (!lx.atEnd()) {
        Obj o = lx.next();
        if (o.t != Obj::Op) {
            ops.push_back(std::move(o));
            if (ops.size() > 256) ops.erase(ops.begin(), ops.begin() + 128);
            continue;
        }
        const std::string &op = o.s;
        auto nums = [&](size_t count) {
            if (ops.size() < count) return false;
            for (size_t k = ops.size() - count; k < ops.size(); ++k) if (!ops[k].isNum()) return false;
            return true;
        };
        auto at = [&](size_t k, size_t count) { return ops[ops.size() - count + k].n; };
        if (op == "BT") { tm = tlm = M{}; }
        else if (op == "q") { stack.push_back(gs); if (stack.size() > 256) stack.erase(stack.begin()); }
        else if (op == "Q") { if (!stack.empty()) { gs = stack.back(); stack.pop_back(); } }
        else if (op == "cm" && nums(6)) gs.ctm = mul(M{at(0, 6), at(1, 6), at(2, 6), at(3, 6), at(4, 6), at(5, 6)}, gs.ctm);
        else if (op == "Tf" && ops.size() >= 2 && ops[ops.size() - 2].t == Obj::Name) {
            gs.fs = ops.back().n;
            const Obj *ref = fontDict.t == Obj::Dict ? fontDict.get(ops[ops.size() - 2].s.c_str()) : nullptr;
            gs.font = ref ? loadFont(*ref, "direct:" + ops[ops.size() - 2].s + ":" + std::to_string(reinterpret_cast<uintptr_t>(&fontDict))) : nullptr;
        }
        else if (op == "Tc" && nums(1)) gs.tc = at(0, 1);
        else if (op == "Tw" && nums(1)) gs.tw = at(0, 1);
        else if (op == "Tz" && nums(1)) gs.th = at(0, 1) / 100.0;
        else if (op == "TL" && nums(1)) gs.tl = at(0, 1);
        else if (op == "Ts" && nums(1)) gs.rise = at(0, 1);
        else if ((op == "Td" || op == "TD") && nums(2)) {
            if (op == "TD") gs.tl = -at(1, 2);
            tlm = mul(M{1, 0, 0, 1, at(0, 2), at(1, 2)}, tlm);
            tm = tlm;
        } else if (op == "Tm" && nums(6)) {
            tlm = tm = M{at(0, 6), at(1, 6), at(2, 6), at(3, 6), at(4, 6), at(5, 6)};
        } else if (op == "T*") {
            tlm = mul(M{1, 0, 0, 1, 0, -gs.tl}, tlm);
            tm = tlm;
        } else if (op == "Tj" && !ops.empty() && ops.back().t == Obj::Str) show(ops.back().s);
        else if (op == "'" && !ops.empty() && ops.back().t == Obj::Str) {
            tlm = mul(M{1, 0, 0, 1, 0, -gs.tl}, tlm);
            tm = tlm;
            show(ops.back().s);
        } else if (op == "\"" && ops.size() >= 3 && ops.back().t == Obj::Str) {
            gs.tw = ops[ops.size() - 3].n;
            gs.tc = ops[ops.size() - 2].n;
            tlm = mul(M{1, 0, 0, 1, 0, -gs.tl}, tlm);
            tm = tlm;
            show(ops.back().s);
        } else if (op == "TJ" && !ops.empty() && ops.back().t == Obj::Arr) {
            for (const Obj &el : ops.back().a) {
                if (el.t == Obj::Str) show(el.s);
                else if (el.isNum()) tm = mul(M{1, 0, 0, 1, -el.n / 1000.0 * gs.fs * gs.th, 0}, tm);
            }
        } else if (op == "Do" && !ops.empty() && ops.back().t == Obj::Name && depth < 8 && xobjDict.t == Obj::Dict) {
            const Obj *ref = xobjDict.get(ops.back().s.c_str());
            if (ref && ref->t == Obj::Ref) {
                Entry *e = entry(ref->num);
                const Obj *st = e ? e->obj.get("Subtype") : nullptr;
                if (e && e->stream && st && st->isName("Form")) {
                    const Obj &formRes = resolve(e->obj.get("Resources") ? *e->obj.get("Resources") : kNull);
                    const M fm = e->obj.get("Matrix") ? matrixOf(resolve(*e->obj.get("Matrix"))) : M{};
                    const int num = ref->num;
                    content(streamOf(num), formRes.t == Obj::Dict ? formRes : resources, mul(fm, gs.ctm), depth + 1, out);
                }
            }
        } else if (op == "BI") {
            // an inline picture: skip its settings, then its data up to "EI"
            while (!lx.atEnd()) {
                Obj k = lx.next();
                if (k.t == Obj::Op && k.s == "ID") break;
            }
            size_t q = lx.p + 1;
            for (;;) {
                const size_t e = data.find("EI", q);
                if (e == std::string::npos) { lx.p = data.size(); break; }
                if (e > 0 && isWs(uint8_t(data[e - 1])) && (e + 2 >= data.size() || isWs(uint8_t(data[e + 2])))) { lx.p = e + 2; break; }
                q = e + 2;
            }
        }
        ops.clear();
    }
}

void Reader::collectPages(const Obj &node, const Obj *res, int depth, std::set<const Obj *> &seen,
                          std::vector<std::pair<const Obj *, const Obj *>> &pages)
{
    if (depth > 32 || node.t != Obj::Dict || seen.count(&node)) return;
    seen.insert(&node);
    const Obj *r = node.get("Resources");
    if (r) {
        const Obj &rr = resolve(*r);
        if (rr.t == Obj::Dict) res = &rr;
    }
    const Obj &kids = resolve(node.get("Kids") ? *node.get("Kids") : kNull);
    const Obj *type = node.get("Type");
    if (kids.t == Obj::Arr && !(type && type->isName("Page"))) {
        for (const Obj &k : kids.a) collectPages(resolve(k), res, depth + 1, seen, pages);
        return;
    }
    pages.emplace_back(&node, res);
}

// Turns the glyphs of a page, in the order they were drawn, into lines of text.
std::vector<Line> toLines(const std::vector<Glyph> &glyphs)
{
    std::vector<Line> lines;
    Line cur;
    double curX2 = 0;
    int curDir = 0;
    bool open = false;
    auto flush = [&]() {
        if (!open) return;
        std::string &t = cur.text;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        size_t s = 0;
        while (s < t.size() && t[s] == ' ') ++s;
        t.erase(0, s);
        if (!t.empty()) lines.push_back(cur);
        cur = Line{};
        open = false;
    };
    for (const Glyph &g : glyphs) {
        if (g.text.empty()) { if (open && g.dir == curDir && std::abs(g.y - cur.y) < 0.5 * std::max(cur.size, g.size)) curX2 = std::max(curX2, g.x2); continue; }
        if (!open) {
            cur.text = g.text;
            cur.x = g.x;
            cur.y = g.y;
            cur.size = g.size;
            curX2 = g.x2;
            curDir = g.dir;
            open = true;
            continue;
        }
        const double sz = std::max({cur.size, g.size, 1.0});
        const bool sameLine = g.dir == curDir && std::abs(g.y - cur.y) < 0.5 * sz && g.x > curX2 - 1.2 * sz;
        if (!sameLine) {
            flush();
            cur.text = g.text;
            cur.x = g.x;
            cur.y = g.y;
            cur.size = g.size;
            curX2 = g.x2;
            curDir = g.dir;
            open = true;
            continue;
        }
        const double gap = g.x - curX2;
        if (gap > 0.16 * sz && !cur.text.empty() && cur.text.back() != ' ' && g.text[0] != ' ') cur.text.push_back(' ');
        cur.text += g.text;
        curX2 = std::max(curX2, g.x2);
        if (g.size > cur.size && g.text != " ") cur.size = std::max(cur.size, std::min(g.size, cur.size * 1.05));
    }
    flush();
    return lines;
}

Document Reader::run()
{
    if (src.compare(0, 5, "%PDF-") != 0 && src.find("%PDF-") > 1024) { doc.error = "That isn't a PDF."; return doc; }
    scan();
    // the trailer: where the document starts, and its encryption
    std::sort(trailers.begin(), trailers.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
    const Obj *root = nullptr, *encryptRef = nullptr, *ids = nullptr;
    for (auto it = trailers.rbegin(); it != trailers.rend(); ++it) {
        if (!root && it->second.get("Root")) root = it->second.get("Root");
        if (!encryptRef && it->second.get("Encrypt")) encryptRef = it->second.get("Encrypt");
        if (!ids && it->second.get("ID")) ids = it->second.get("ID");
    }
    if (encryptRef) {
        doc.encrypted = true;
        if (encryptRef->t == Obj::Ref) encryptNum = encryptRef->num;
        auto eit = encryptNum >= 0 ? objs.find(encryptNum) : objs.end();
        if (eit != objs.end()) eit->second.ready = true;      // the encryption settings themselves aren't encrypted
        const Obj &ed = encryptRef->t == Obj::Ref ? (eit != objs.end() ? eit->second.obj : kNull) : *encryptRef;
        const Obj &idArr = ids ? (ids->t == Obj::Ref ? resolve(*ids) : *ids) : kNull;
        const std::string id0 = idArr.t == Obj::Arr && !idArr.a.empty() ? idArr.a[0].s : std::string();
        if (ed.t != Obj::Dict || !setupEncryption(ed, id0)) {
            if (doc.error.empty()) doc.error = "This PDF's protection couldn't be read.";
            return doc;
        }
        enc = true;
    }
    expandObjStms();
    const Obj *catalog = nullptr;
    if (root) catalog = &resolve(*root);
    if (!catalog || catalog->t != Obj::Dict) {
        for (auto &kv : objs) {
            const Obj *t = kv.second.obj.get("Type");
            if (t && t->isName("Catalog")) { catalog = &entry(kv.first)->obj; break; }
        }
    }
    if (!catalog || catalog->t != Obj::Dict) { doc.error = "Couldn't find the pages in this PDF."; return doc; }
    std::vector<std::pair<const Obj *, const Obj *>> pages;
    std::set<const Obj *> seen;
    collectPages(resolve(catalog->get("Pages") ? *catalog->get("Pages") : kNull), nullptr, 0, seen, pages);
    if (pages.empty()) { doc.error = "Couldn't find the pages in this PDF."; return doc; }
    for (const auto &[page, res] : pages) {
        std::string data;
        const Obj *c = page->get("Contents");
        if (c) {
            if (c->t == Obj::Ref) {
                const Obj &target = resolve(*c);
                if (target.t == Obj::Arr) { for (const Obj &x : target.a) { data += streamOf(x); data += '\n'; } }
                else data = streamOf(*c);
            } else if (c->t == Obj::Arr) {
                for (const Obj &x : c->a) { data += streamOf(x); data += '\n'; }
            }
        }
        std::vector<Glyph> glyphs;
        content(data, res ? *res : kNull, M{}, 0, glyphs);
        Page pg;
        pg.lines = toLines(glyphs);
        doc.pages.push_back(std::move(pg));
        streams.clear();      // a page's drawing isn't needed again; fonts and their maps are kept
    }
    if (std::getenv("PDFTEXT_DEBUG"))
        for (const auto &kv : fonts) {
            const Font &f = *kv.second;
            if (!f.used) continue;
            std::fprintf(stderr, "font %s %s %s enc=%s tounicode=%zu spaces=%zu glyphs=%ld unmapped=%ld\n", kv.first.c_str(),
                         f.name.c_str(), f.subtype.c_str(), f.encoding.c_str(), f.uni.size(), f.spaces.size(), f.used, f.unmapped);
        }
    return doc;
}

std::string normalise(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (std::isdigit(c)) { if (o.empty() || o.back() != '#') o.push_back('#'); }
        else if (c == ' ') { if (!o.empty() && o.back() != ' ') o.push_back(' '); }
        else o.push_back(char(std::tolower(c)));
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

} // namespace

Document read(const std::string &pdf)
{
    Reader r(pdf);
    return r.run();
}

std::string text(const Document &doc, bool dropRunning)
{
    std::unordered_set<std::string> running;
    const size_t n = doc.pages.size();
    if (dropRunning && n >= 3) {
        std::unordered_map<std::string, size_t> pagesWith;
        for (const Page &p : doc.pages) {
            std::unordered_set<std::string> here;
            for (const Line &l : p.lines) if (l.text.size() <= 160) here.insert(normalise(l.text));
            for (const auto &h : here) ++pagesWith[h];
        }
        const size_t need = std::max<size_t>(3, size_t(std::ceil(0.3 * double(n))));
        for (const auto &kv : pagesWith) if (kv.second >= need) running.insert(kv.first);
        running.insert("#");      // page numbers on their own
    }
    std::string out;
    for (const Page &p : doc.pages) {
        for (const Line &l : p.lines) {
            if (!running.empty() && running.count(normalise(l.text))) continue;
            out += l.text;
            out += '\n';
        }
    }
    return out;
}

std::string headings(const Document &doc)
{
    std::vector<std::pair<double, size_t>> sizes;      // size, characters set in it
    for (const Page &p : doc.pages) for (const Line &l : p.lines) sizes.emplace_back(l.size, l.text.size());
    if (sizes.empty()) return {};
    std::sort(sizes.begin(), sizes.end());
    size_t total = 0;
    for (const auto &s : sizes) total += s.second;
    size_t acc = 0;
    double body = sizes[0].first;
    for (const auto &s : sizes) { acc += s.second; if (acc * 2 >= total) { body = s.first; break; } }
    std::string out;
    for (const Page &p : doc.pages)
        for (const Line &l : p.lines)
            if (l.size >= body * 1.25 && l.text.size() >= 3 && l.text.size() <= 120) out += l.text + "\n";
    return out;
}

} // namespace pdftext
