#include "specread.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>

namespace specread {
namespace {

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(uint8_t(s[a]))) ++a;
    while (b > a && std::isspace(uint8_t(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s)
{
    for (char &c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}
std::string firstWord(const std::string &s)
{
    size_t i = 0;
    while (i < s.size() && !std::isalpha(uint8_t(s[i]))) ++i;
    size_t j = i;
    while (j < s.size() && (std::isalpha(uint8_t(s[j])) || s[j] == '-')) ++j;
    return lower(s.substr(i, j - i));
}
// A line that is a learning statement ("Describe…", "understand that…") rather than a heading.
bool statementLike(const std::string &title)
{
    if (title.empty()) return true;
    const unsigned char c = uint8_t(title[0]);
    if (c < 0x80 && std::islower(c)) return true;
    static const std::set<std::string> verbs = {
        "describe", "state", "explain", "calculate", "convert", "identify", "investigate", "define", "outline", "use",
        "interpret", "compare", "relate", "recall", "know", "understand", "recognise", "recognize", "draw", "construct",
        "list", "name", "sketch", "suggest", "discuss", "evaluate", "analyse", "analyze", "apply", "plot", "measure",
        "determine", "distinguish", "show", "predict", "deduce", "classify", "demonstrate", "carry", "make", "select",
        "find", "solve", "simplify", "expand", "factorise", "factorize", "estimate", "round", "write", "read", "work",
        "manipulate", "differentiate", "integrate", "prove", "represent", "translate", "add", "multiply", "divide",
        "subtract", "complete", "label", "appreciate", "consider", "assess", "argue", "justify", "summarise",
        "summarize", "review", "examine", "explore", "recognize", "quote", "rearrange", "obtain", "verify", "perform",
        "record", "present", "process", "test", "observe", "plan", "evaluate", "comment", "give", "provide"};
    return verbs.count(firstWord(title)) > 0;
}

// "3.2.1 Cell structure", "C1.1 Types of number", "OT1.1 Construct…", "4 Subject content"
struct Num {
    std::string prefix;
    std::vector<int> parts;
    std::string title;
};
bool parseNum(const std::string &line, Num &n)
{
    size_t i = 0;
    while (i < line.size() && i < 2 && std::isupper(uint8_t(line[i]))) ++i;
    if (i < line.size() && !std::isdigit(uint8_t(line[i]))) return false;
    n.prefix = line.substr(0, i);
    n.parts.clear();
    for (;;) {
        const size_t d = i;
        while (i < line.size() && std::isdigit(uint8_t(line[i]))) ++i;
        if (i == d || i - d > 2) return false;
        n.parts.push_back(std::atoi(line.substr(d, i - d).c_str()));
        if (i + 1 < line.size() && line[i] == '.' && std::isdigit(uint8_t(line[i + 1])) && n.parts.size() < 5) { ++i; continue; }
        break;
    }
    if (i < line.size() && (line[i] == '.' || line[i] == ':' || line[i] == ')')) ++i;
    if (i >= line.size() || !std::isspace(uint8_t(line[i]))) return false;
    n.title = trim(line.substr(i));
    if (n.title.empty()) return false;
    if (n.parts.size() == 1 && n.prefix.empty() && n.parts[0] > 60) return false;
    return true;
}

// "Topic 1 – Key concepts in biology", "Key topic 2: William I in power", "Paper 1: …", "Option B1: …", "Part one: …"
struct Key {
    std::string keyword, id, title;
    int rank = 0;
};
bool parseKey(const std::string &line, Key &k)
{
    static const std::vector<std::pair<std::string, int>> words = {
        {"key topic", 20}, {"topic area", 19}, {"area of study", 20}, {"content area", 20}, {"paper", 10},
        {"component", 12}, {"unit", 12}, {"section", 14}, {"option", 16}, {"theme", 18}, {"module", 18},
        {"chapter", 18}, {"topic", 20}, {"part", 24}};
    if (line.size() > 140 || line.empty() || !std::isupper(uint8_t(line[0]))) return false;
    const std::string low = lower(line);
    for (const auto &[w, rank] : words) {
        if (low.compare(0, w.size(), w) != 0) continue;
        size_t i = w.size();
        if (i >= line.size() || line[i] != ' ') return false;
        ++i;
        const size_t s = i;
        while (i < line.size() && (std::isalnum(uint8_t(line[i])))) ++i;
        std::string id = line.substr(s, i - s);
        static const std::set<std::string> spelled = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
        const bool ok = !id.empty() && (id.size() <= 3 || spelled.count(lower(id)));
        if (!ok) return false;
        if (id.size() > 1 && std::all_of(id.begin(), id.end(), [](char c) { return std::isalpha(uint8_t(c)); }) && !spelled.count(lower(id)))
            return false;      // "Section Assessment…" is a sentence, not a heading
        // separator, then the title
        while (i < line.size() && (line[i] == ' ' || line[i] == ':' || line[i] == '.' || line[i] == '-')) ++i;
        if (line.compare(i, 3, "\xE2\x80\x93") == 0 || line.compare(i, 3, "\xE2\x80\x94") == 0) i += 3;      // – —
        while (i < line.size() && line[i] == ' ') ++i;
        k.keyword = w;
        k.keyword[0] = char(std::toupper(uint8_t(k.keyword[0])));
        k.id = id;
        k.title = trim(line.substr(i));
        k.rank = rank;
        return true;
    }
    return false;
}

// "(a) Characteristics of living organisms", "a) …", "a. …"
bool parseLetter(const std::string &line, char &letter, std::string &title)
{
    size_t i = 0;
    if (line.size() < 4) return false;
    if (line[0] == '(' && std::islower(uint8_t(line[1])) && line[2] == ')') i = 3;
    else if (std::islower(uint8_t(line[0])) && (line[1] == ')' || line[1] == '.') && line[2] == ' ') i = 2;
    else return false;
    letter = line[i == 3 ? 1 : 0];
    title = trim(line.substr(i));
    return !title.empty();
}

// AQA History options: "AA America, 1840–1895: Expansion and consolidation"
bool parseCode2(const std::string &line, std::string &code, std::string &title)
{
    if (line.size() < 8 || line[0] < 'A' || line[0] > 'E' || line[1] < 'A' || line[1] > 'J' || line[2] != ' ') return false;
    if (!std::isupper(uint8_t(line[3]))) return false;
    code = line.substr(0, 2);
    title = trim(line.substr(3));
    return title.size() > 6;
}

// A contents-page line: leader dots, or a page number after the title ("4.1 Cell biology 16")
bool tocLine(const std::string &raw)
{
    if (raw.find("....") != std::string::npos || raw.find("\xE2\x80\xA6\xE2\x80\xA6") != std::string::npos) return true;
    const size_t sp = raw.find_last_of(' ');
    if (sp == std::string::npos || sp + 1 >= raw.size()) return false;
    const std::string last = raw.substr(sp + 1);
    if (last.size() > 3 || !std::all_of(last.begin(), last.end(), [](char c) { return std::isdigit(uint8_t(c)); })) return false;
    // the word before the number: a page number follows a word, not a comma or a dash
    const std::string before = raw.substr(0, sp);
    if (before.empty()) return false;
    const char c = before.back();
    return std::isalpha(uint8_t(c)) || c == ')' || c == '?';
}

std::string cleanTitle(std::string t)
{
    static const char *tails[] = {"Notes and examples", "Learning outcomes", "(continued)", "continued", "Continued",
                                  "\xE2\x84\xA2", "\xC2\xAE"};
    for (bool again = true; again;) {
        again = false;
        t = trim(t);
        // contents-page leaders and page numbers
        while (!t.empty() && (std::isdigit(uint8_t(t.back())) || t.back() == '.') && t.size() > 3) {
            const size_t sp = t.find_last_of(' ');
            const std::string last = sp == std::string::npos ? t : t.substr(sp + 1);
            if (std::all_of(last.begin(), last.end(), [](char c) { return std::isdigit(uint8_t(c)) || c == '.'; })) {
                t = trim(t.substr(0, sp == std::string::npos ? 0 : sp));
                again = true;
            } else break;
        }
        for (const char *tail : tails) {
            const size_t n = std::strlen(tail);
            if (t.size() > n && t.compare(t.size() - n, n, tail) == 0) { t = trim(t.substr(0, t.size() - n)); again = true; }
        }
    }
    while (!t.empty() && (t.back() == ':' || t.back() == ',' || t.back() == ';')) t.pop_back();
    return trim(t);
}

std::string shortTitle(const std::string &t)
{
    if (t.size() <= 90) return t;
    size_t cut = t.rfind(' ', 86);
    if (cut == std::string::npos || cut < 40) cut = 86;
    while (cut > 0 && (uint8_t(t[cut]) & 0xC0) == 0x80) --cut;
    return t.substr(0, cut) + "\xE2\x80\xA6";
}

std::string norm(const std::string &s)
{
    std::string o;
    for (unsigned char c : s) {
        if (std::isdigit(c)) { if (o.empty() || o.back() != '#') o.push_back('#'); }
        else if (std::isspace(c)) { if (!o.empty() && o.back() != ' ') o.push_back(' '); }
        else o.push_back(char(std::tolower(c)));
    }
    return trim(o);
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::string t;
        bool sp = false;
        for (char c : cur) {
            if (c == '\t' || c == ' ' || c == '\f' || c == '\v') { sp = true; continue; }
            if (sp && !t.empty()) t.push_back(' ');
            sp = false;
            t.push_back(c);
        }
        if (!t.empty()) out.push_back(t);
        cur.clear();
    };
    for (char c : text) {
        if (c == '\n' || c == '\r') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

bool adminTitle(const std::string &title)
{
    static const char *words[] = {"assessment", "administration", "what else", "scheme of", "appendix", "appendices",
                                  "general information", "additional information", "further information", "support",
                                  "glossary", "command words", "mathematical requirements", "grade descriptions",
                                  "entries", "resits", "re-sits", "overlaps", "malpractice", "codes", "key features",
                                  "qualification at a glance", "specification at a glance", "syllabus overview",
                                  "introduction", "why choose", "about this", "other information"};
    const std::string t = lower(title);
    for (const char *w : words) if (t.find(w) != std::string::npos) return true;
    return false;
}

bool contentTitle(const std::string &title)
{
    const std::string t = lower(title);
    if (t.find("content") == std::string::npos) return false;
    return t.size() < 70 && t.find("contents") == std::string::npos;
}

} // namespace

Info identify(const std::string &text)
{
    Info info;
    const std::string head = text.substr(0, 6000);
    const std::string low = lower(head);
    if (low.find("cambridge") != std::string::npos && (low.find("igcse") != std::string::npos || low.find("a level") != std::string::npos ||
                                                       low.find("o level") != std::string::npos || low.find("cambridgeinternational") != std::string::npos))
        info.board = "Cambridge";
    else if (low.find("pearson edexcel") != std::string::npos || low.find("edexcel") != std::string::npos) info.board = "Edexcel";
    else if (low.find("aqa") != std::string::npos) info.board = "AQA";
    else if (low.find("ocr.org.uk") != std::string::npos || low.find("oxford cambridge and rsa") != std::string::npos) info.board = "OCR";
    else if (low.find("wjec") != std::string::npos || low.find("eduqas") != std::string::npos) info.board = "WJEC";

    std::smatch m;
    auto clean = [](std::string s) {
        static const std::regex tm("\xE2\x84\xA2|\xC2\xAE");
        s = std::regex_replace(s, tm, "");
        return trim(s);
    };
    if (info.board == "Cambridge") {
        static const std::regex re("Cambridge (International AS (?:&|and) A Level|IGCSE|O Level|International A Level|International AS Level)\\S* ([A-Z][A-Za-z ,&()-]{2,40}?) ([0-9]{4})");
        if (std::regex_search(head, m, re)) info.name = "Cambridge " + clean(m[1].str()) + " " + clean(m[2].str()) + " (" + m[3].str() + ")";
    } else if (info.board == "Edexcel") {
        static const std::regex re("(GCSE \\(9\\S1\\)|International GCSE|Advanced Subsidiary GCE|Advanced GCE|GCE A level|International Advanced Level) in ([A-Z][A-Za-z ,&-]{2,40}?) \\(([0-9A-Z]{4})\\)");
        if (std::regex_search(head, m, re)) {
            std::string level = m[1].str();
            if (level.rfind("GCSE (9", 0) == 0) level = "GCSE";
            else if (level == "Advanced GCE" || level == "GCE A level") level = "A level";
            else if (level == "Advanced Subsidiary GCE") level = "AS level";
            info.name = "Edexcel " + level + " " + clean(m[2].str()) + " (" + m[3].str() + ")";
        }
    } else if (info.board == "AQA") {
        static const std::regex re("(GCSE|AS AND A-LEVEL|AS and A-level|A-LEVEL|A-level|AS|Level 3 Extended Project|EXTENDED PROJECT)\\s+([A-Z][A-Z ,&'-]{2,40}?)\\s*\\(([0-9]{4})(?:, ?[0-9]{4})?\\)");
        if (std::regex_search(head, m, re)) {
            std::string subj = lower(clean(m[2].str()));
            bool cap = true;
            for (char &c : subj) { if (cap && std::isalpha(uint8_t(c))) { c = char(std::toupper(uint8_t(c))); cap = false; } if (c == ' ') cap = true; }
            for (const char *w : {" And ", " Of ", " The "}) {
                std::string lw = w;
                for (char &c : lw) c = char(std::tolower(uint8_t(c)));
                size_t p;
                while ((p = subj.find(w)) != std::string::npos) subj.replace(p, std::strlen(w), lw);
            }
            std::string level = m[1].str();
            if (lower(level).find("a-level") != std::string::npos) level = "A-level";
            info.name = "AQA " + level + " " + subj + " (" + m[3].str() + ")";
        }
    }
    return info;
}

std::vector<podsort::SpecPoint> parse(const std::string &text)
{
    std::vector<std::string> all = splitLines(text);

    // 1. Lines repeated on page after page: running headers and footers, column headings
    {
        std::unordered_map<std::string, int> freq;
        for (const auto &l : all) ++freq[norm(l)];
        std::vector<std::string> kept;
        for (const auto &l : all) {
            const int f = freq[norm(l)];
            Num n;
            Key k;
            const bool heading = (parseNum(l, n) && !statementLike(n.title)) || parseKey(l, k);
            if (l.size() <= 160 && (f >= 8 || (f >= 4 && !heading))) continue;
            kept.push_back(l);
        }
        all.swap(kept);
    }

    // 2. The subject content: from "3 Subject content" (not its entry on the contents page) to the next part of the
    //    document that isn't content ("4 Details of the assessment"). Several content parts in a row count as one.
    size_t from = 0, to = all.size();
    {
        int section = -1;
        for (size_t i = 0; i < all.size(); ++i) {
            Num n;
            if (!parseNum(all[i], n) || n.parts.size() != 1 || !n.prefix.empty()) continue;
            if (tocLine(all[i])) continue;
            if (section < 0) {
                if (contentTitle(cleanTitle(n.title))) { section = n.parts[0]; from = i + 1; }
                continue;
            }
            if (n.parts[0] > section && n.parts[0] <= section + 2 && adminTitle(cleanTitle(n.title)) && !contentTitle(cleanTitle(n.title))) {
                to = i;
                break;
            }
            if (n.parts[0] == section + 1 && contentTitle(cleanTitle(n.title))) section = n.parts[0];      // AS, then A-level
        }
        if (section < 0) { from = 0; to = all.size(); }
    }
    const std::vector<std::string> lines(all.begin() + long(from), all.begin() + long(to));
    const std::string board = identify(text).board;

    // AQA History's option codes count only when there are several of them
    int code2s = 0;
    if (board == "AQA")
        for (const auto &l : lines) { std::string c, t; if (parseCode2(l, c, t)) ++code2s; }
    const bool useCode2 = code2s >= 3;

    std::vector<podsort::SpecPoint> pts;
    std::vector<int> rankOf;                     // per point
    std::vector<bool> dotted;                    // per point: a numbered point with dots ("1.1")
    std::vector<int> section;                    // per point: which top-level content section (AS / A-level)
    std::map<std::pair<int, std::string>, int> byKey;      // (parent, number) → point: pages that continue it
    std::set<std::string> numbersSeen;
    struct Open { int rank; int point; };
    std::vector<Open> stack;
    int lastTop = 0;
    int topSection = 0;

    auto open = [&](int rank, const std::string &number, const std::string &title, bool isDotted) -> int {
        while (!stack.empty() && stack.back().rank >= rank) stack.pop_back();
        const int parent = stack.empty() ? -1 : stack.back().point;
        const auto key = std::make_pair(parent, number.empty() ? lower(title) : number);
        auto it = byKey.find(key);
        int idx;
        if (it != byKey.end()) {
            idx = it->second;
            if (pts[size_t(idx)].title.empty()) pts[size_t(idx)].title = title;
        } else {
            podsort::SpecPoint p;
            p.number = number;
            p.title = title;
            p.parent = parent;
            pts.push_back(p);
            rankOf.push_back(rank);
            dotted.push_back(isDotted);
            section.push_back(topSection);
            idx = int(pts.size()) - 1;
            byKey[key] = idx;
        }
        stack.push_back({rank, idx});
        return idx;
    };
    auto body = [&](const std::string &l) {
        if (stack.empty()) return;
        std::string &b = pts[size_t(stack.back().point)].body;
        if (b.size() < 4000) b += l + "\n";
    };
    auto dottedOpen = [&]() {
        for (const auto &o : stack) if (dotted[size_t(o.point)]) return true;
        return false;
    };
    auto letterOpen = [&]() {
        for (const auto &o : stack) if (rankOf[size_t(o.point)] == 31) return true;
        return false;
    };
    // the first number with dots after line i, and its first part ("2.1" → 2)
    auto nextDottedMajor = [&](size_t i) {
        for (size_t j = i + 1; j < lines.size() && j < i + 400; ++j) {
            Num n;
            if (parseNum(lines[j], n) && n.parts.size() >= 2 && !statementLike(n.title)) return n.parts[0];
        }
        return -1;
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string &l = lines[i];
        Num n;
        Key k;
        char letter = 0;
        if (tocLine(l) && ((parseNum(l, n) && !statementLike(n.title)) || parseKey(l, k))) continue;      // the contents page
        std::string code, title;
        int made = -1;

        if (parseKey(l, k)) {
            int rank = k.rank;
            if (useCode2 && k.keyword == "Part") rank = 52;
            made = open(rank, k.keyword + " " + k.id, cleanTitle(k.title), false);
            if (rank < 30) lastTop = 0;
        } else if (useCode2 && parseCode2(l, code, title)) {
            made = open(50, code, cleanTitle(title), false);
        } else if (parseNum(l, n)) {
            const std::string t = cleanTitle(n.title);
            std::string number;
            const bool tier = board == "Cambridge" && (n.prefix == "C" || n.prefix == "E");      // Core and Extended: one point
            if (!tier) number = n.prefix;
            for (size_t p = 0; p < n.parts.size(); ++p) number += (p ? "." : "") + std::to_string(n.parts[p]);
            const bool statement = statementLike(n.title);
            if (lower(t).rfind("extended content only", 0) == 0 || lower(t).rfind("core content only", 0) == 0) {
                numbersSeen.insert(number);
                continue;
            }
            if (n.parts.size() == 1 && (n.prefix.empty() || tier)) {
                // a topic ("2 Organisation of the organism"), or a numbered learning statement under a subtopic
                const int v = n.parts[0];
                bool heading = !statement;
                if (heading && dottedOpen()) heading = v == lastTop || (v == lastTop + 1 && nextDottedMajor(i) == v);
                if (heading) {
                    made = open(30, number, t, false);
                    lastTop = v;
                } else body(l);
            } else if (n.parts.size() >= 2) {
                // a subtopic ("4.1.1 Cell structure"), or a statement ("2.4 know the similarities…")
                std::string parentNum = number.substr(0, number.rfind('.'));
                std::string prevNum = parentNum + "." + std::to_string(n.parts.back() - 1);
                const bool fits = n.parts.back() <= 1 || numbersSeen.count(prevNum) || numbersSeen.count(parentNum) ||
                                  !statementLike(n.title) || n.parts.back() == 2;
                numbersSeen.insert(number);
                if (statement && (letterOpen() || !fits)) body(l);
                else {
                    made = open(30 + 2 * int(n.parts.size() - 1), number, statement ? shortTitle(t) : t, true);
                    if (statement) body(l);
                }
            } else body(l);
            if (made >= 0) numbersSeen.insert(number);
        } else if (parseLetter(l, letter, title)) {
            if (!statementLike(title) && !stack.empty()) made = open(31, std::string("(") + letter + ")", cleanTitle(title), false);
            else body(l);
        } else {
            // a heading's title that wrapped onto this line ("1.1 The microscope in cell" + "studies")
            if (!stack.empty() && i > 0 && !pts[size_t(stack.back().point)].title.empty()) {
                std::string &bt = pts[size_t(stack.back().point)].title;
                const std::string &bb = pts[size_t(stack.back().point)].body;
                const unsigned char c0 = uint8_t(l[0]);
                if (bb.empty() && c0 < 0x80 && std::islower(c0) && l.size() < 45 && bt.size() < 70 &&
                    bt.back() != '.' && bt.back() != ':' && bt.find("\xE2\x80\xA6") == std::string::npos) {
                    bt += " " + cleanTitle(l);
                    continue;
                }
            }
            body(l);
        }
        (void)made;
    }

    // 3. AS and A-level content in one document: keep the later (A-level) copy of a point that appears in both
    {
        std::map<std::pair<std::string, int>, int> last;
        std::vector<bool> drop(pts.size(), false);
        auto depthOf = [&](int i) { int d = 0; for (int p = pts[size_t(i)].parent; p >= 0; p = pts[size_t(p)].parent) ++d; return d; };
        for (size_t i = 0; i < pts.size(); ++i) {
            const auto key = std::make_pair(lower(pts[i].title), depthOf(int(i)));
            auto it = last.find(key);
            if (it != last.end() && pts[size_t(it->second)].number != pts[i].number && !pts[i].number.empty() &&
                pts[size_t(it->second)].number.size() == pts[i].number.size() && pts[i].number[0] != pts[size_t(it->second)].number[0] &&
                std::isdigit(uint8_t(pts[i].number[0])))
                drop[size_t(it->second)] = true;
            last[key] = int(i);
        }
        // a dropped point takes the points inside it with it
        for (size_t i = 0; i < pts.size(); ++i)
            for (int p = pts[i].parent; p >= 0 && !drop[i]; p = pts[size_t(p)].parent) if (drop[size_t(p)]) drop[i] = true;
        std::vector<int> remap(pts.size(), -1);
        std::vector<podsort::SpecPoint> kept;
        for (size_t i = 0; i < pts.size(); ++i) {
            if (drop[i] || pts[i].title.empty()) continue;
            remap[i] = int(kept.size());
            kept.push_back(pts[i]);
        }
        for (auto &p : kept) {
            int q = p.parent;
            while (q >= 0 && remap[size_t(q)] < 0) q = pts[size_t(q)].parent;      // skip removed parents
            p.parent = q >= 0 ? remap[size_t(q)] : -1;
        }
        pts.swap(kept);
    }
    for (auto &p : pts) {
        p.depth = 0;
        for (int q = p.parent; q >= 0; q = pts[size_t(q)].parent) ++p.depth;
        p.title = shortTitle(p.title);
    }
    return pts;
}

} // namespace specread
