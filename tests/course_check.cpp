// Checks how Streamline reads each course's specification, and whether its points can be told apart.
//   course_check [--model meaning.gguf] [--leaves <id>,<id>…] <id>=<spec.pdf|spec.txt> …
// For each specification: the course it says it is, how many points it found, and anything that looks misread:
//   no content   the reader didn't find where the subject content starts, so it read the whole document
//   numbering    a numbered point inside a heading of another number ("1.1" inside "9 …"): two parts run together
//   repeated     two points with the same number: a document's numbering read twice
//   full         points whose text reached the reader's limit: often two parts of the document run together
//   odd titles   titles that aren't headings: sentence fragments, table headings, page numbers, change notes
//   thin         points with almost nothing to match on (a short title and no text)
//   few / many   fewer than 8 points to match, or more than 400
// With the meaning model:
//   alike        pairs of points whose text means nearly the same: the model can't tell them apart
//   self         half of each point's text (every other line) matched against the spec read from the other half:
//                how often it finds its own point. Low means the points are hard to tell apart, or their text is not
//                theirs (a misread document).
// Prints only structure (numbers, headings, counts), never a document's text.
#include "pdftext.h"
#include "podai.h"
#include "podsort.h"
#include "specread.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string cut(const std::string &s, size_t n)
{
    if (s.size() <= n) return s;
    size_t c = n;
    while (c > 0 && (uint8_t(s[c]) & 0xC0) == 0x80) --c;
    return s.substr(0, c) + "…";
}
std::string lower(std::string s)
{
    for (char &c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}
// "P4 1.1" → "1.1"; "1.1" → "1.1"; "Topic 2" → ""
std::string digits(const std::string &num)
{
    size_t i = 0;
    if (num.size() > 3 && std::isalpha(uint8_t(num[0])) && num.find(' ') != std::string::npos) i = num.find(' ') + 1;
    if (i >= num.size() || !std::isdigit(uint8_t(num[i]))) return {};
    std::string d = num.substr(i);
    for (char c : d) if (!std::isdigit(uint8_t(c)) && c != '.') return {};
    return d;
}

bool oddTitle(const std::string &t, std::string &why)
{
    const std::string l = lower(t);
    if (t.size() < 3) { why = "too short"; return true; }
    if (t.size() > 160) { why = "very long"; return true; }
    const unsigned char c0 = uint8_t(t[0]);
    if (c0 < 0x80 && std::islower(c0)) { why = "starts in lower case"; return true; }
    int letters = 0;
    for (unsigned char c : t) if (std::isalpha(c)) ++letters;
    if (letters * 2 < int(t.size())) { why = "mostly not words"; return true; }
    static const char *words[] = {"amended", "rewardable", "candidates must", "candidates should", "students must", "marks ",
                                  "mark scheme", "copyright", "©", "www.", "http", "see page", "continued", "page ",
                                  "assessment objective", "weighting", "guided learning hours"};
    for (const char *w : words) if (l.find(w) != std::string::npos) { why = std::string("says \"") + w + "\""; return true; }
    // ends with a page number: "Forging a new society 88"
    size_t sp = t.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < t.size()) {
        const std::string last = t.substr(sp + 1);
        if (last.size() <= 3 && std::all_of(last.begin(), last.end(), [](char c) { return std::isdigit(uint8_t(c)); }) && t.size() > 12)
            if (l.find("paper") == std::string::npos && l.find("unit") == std::string::npos && l.find("topic") == std::string::npos &&
                l.find("component") == std::string::npos && l.find("part") == std::string::npos && l.find("section") == std::string::npos &&
                l.find("level") == std::string::npos && l.find("year") == std::string::npos) { why = "ends in a page number"; return true; }
    }
    // a table's headings: "Paper 1 Paper 2"
    if (l.rfind("paper ", 0) == 0 && l.find(" paper ") != std::string::npos && t.size() < 30) { why = "table headings"; return true; }
    return false;
}

struct Report {
    std::string id, name;
    size_t points = 0, leaves = 0;
    std::vector<std::string> flags, details;
};

} // namespace

int main(int argc, char **argv)
{
    std::string modelPath;
    std::set<std::string> showLeaves;
    std::vector<std::pair<std::string, std::string>> specs;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--model") && i + 1 < argc) { modelPath = argv[++i]; continue; }
        if (!std::strcmp(argv[i], "--leaves") && i + 1 < argc) {
            std::stringstream ls(argv[++i]);
            for (std::string one; std::getline(ls, one, ',');) showLeaves.insert(one);
            continue;
        }
        const std::string a = argv[i];
        const size_t eq = a.find('=');
        if (eq == std::string::npos) specs.push_back({a, a});
        else specs.push_back({a.substr(0, eq), a.substr(eq + 1)});
    }
    if (specs.empty()) { std::fprintf(stderr, "usage: course_check [--model meaning.gguf] [--leaves id,…] id=spec.pdf …\n"); return 2; }
    podai::quiet();
    podai::Meaning m;
    bool haveModel = false;
    if (!modelPath.empty()) {
        std::string err;
        haveModel = m.load(modelPath, err);
        if (!haveModel) std::fprintf(stderr, "meaning model: %s\n", err.c_str());
    }

    int clean = 0;
    for (const auto &[id, path] : specs) {
        const auto t0 = std::chrono::steady_clock::now();
        Report r;
        r.id = id;
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        if (text.compare(0, 5, "%PDF-") == 0) {
            const auto doc = pdftext::read(text);
            if (!doc.error.empty()) { std::printf("\n== %s: PDF error: %s\n", id.c_str(), doc.error.c_str()); continue; }
            text = pdftext::text(doc);
        }
        if (text.empty()) { std::printf("\n== %s: empty\n", id.c_str()); continue; }
        r.name = specread::identify(text).name;
        const auto pts = podsort::parseSpec(text);
        const auto diag = specread::lastDiagnostics();
        const auto leaves = podsort::leafPoints(pts);
        r.points = pts.size();
        r.leaves = leaves.size();

        // ---- how it was read ----
        if (!diag.contentFound) r.flags.push_back("no content");
        if (leaves.size() < 8) r.flags.push_back("few (" + std::to_string(leaves.size()) + ")");
        if (leaves.size() > 400) r.flags.push_back("many (" + std::to_string(leaves.size()) + ")");
        {
            int bad = 0;
            for (size_t i = 0; i < pts.size(); ++i) {
                const std::string d = digits(pts[i].number);
                if (d.empty()) continue;
                for (int q = pts[i].parent; q >= 0; q = pts[size_t(q)].parent) {
                    const std::string pd = digits(pts[size_t(q)].number);
                    if (pd.empty()) continue;
                    if (d.rfind(pd + ".", 0) != 0 && d != pd) {
                        if (++bad <= 4) r.details.push_back("  numbering: " + pts[i].number + " is inside " + pts[size_t(q)].number + " " + cut(pts[size_t(q)].title, 40));
                    }
                    break;
                }
            }
            if (bad) r.flags.push_back("numbering (" + std::to_string(bad) + ")");
        }
        {
            std::map<std::string, int> seen;
            int rep = 0;
            for (const auto &p : pts)
                if (!p.number.empty() && ++seen[p.number] == 2 && ++rep <= 4) r.details.push_back("  repeated: " + p.number + " " + cut(p.title, 50));
            if (rep) r.flags.push_back("repeated (" + std::to_string(rep) + ")");
        }
        {
            int full = 0;
            std::vector<size_t> sizes;
            for (int l : leaves) sizes.push_back(pts[size_t(l)].body.size());
            std::sort(sizes.begin(), sizes.end());
            const size_t median = sizes.empty() ? 0 : sizes[sizes.size() / 2];
            for (int l : leaves) if (pts[size_t(l)].body.size() >= 3900) ++full;
            // a few long points are normal; many, when most are short, are parts run together
            if (full > 0 && (full * 10 > int(leaves.size()) || median < 900)) {
                r.flags.push_back("full (" + std::to_string(full) + ", median " + std::to_string(median) + ")");
                int shown = 0;
                for (int l : leaves) if (pts[size_t(l)].body.size() >= 3900 && ++shown <= 3) r.details.push_back("  full: " + pts[size_t(l)].number + " " + cut(pts[size_t(l)].title, 50));
            }
        }
        {
            int odd = 0;
            for (const auto &p : pts) {
                std::string why;
                if (oddTitle(p.title, why) && ++odd <= 5) r.details.push_back("  odd title (" + why + "): " + p.number + (p.number.empty() ? "" : " ") + cut(p.title, 60));
            }
            if (odd) r.flags.push_back("odd titles (" + std::to_string(odd) + ")");
        }
        {
            int thin = 0;
            for (int l : leaves) if (pts[size_t(l)].body.size() < 40 && pts[size_t(l)].title.size() < 25) ++thin;
            if (thin * 4 > int(leaves.size()) && thin >= 3) r.flags.push_back("thin (" + std::to_string(thin) + " of " + std::to_string(leaves.size()) + ")");
        }

        // ---- can the points be told apart ----
        std::string meaning;
        if (haveModel && leaves.size() >= 2 && leaves.size() <= 600) {
            const auto index = podai::readSpec(m, pts);
            // alike: the whole points, pair by pair
            struct Pair { double c; int a, b; };
            std::vector<Pair> alike;
            for (size_t x = 0; x < leaves.size(); ++x)
                for (size_t y = x + 1; y < leaves.size(); ++y) {
                    const auto &u = index.whole[size_t(leaves[x])], &v = index.whole[size_t(leaves[y])];
                    double c = 0;
                    for (size_t k = 0; k < u.size() && k < v.size(); ++k) c += double(u[k]) * v[k];
                    if (c >= 0.95) alike.push_back({c, leaves[x], leaves[y]});
                }
            std::sort(alike.begin(), alike.end(), [](const Pair &a, const Pair &b) { return a.c > b.c; });
            for (size_t k = 0; k < alike.size() && k < 4; ++k) {
                char b[64];
                std::snprintf(b, sizeof b, "%.3f", alike[k].c);
                r.details.push_back("  alike " + std::string(b) + ": " + pts[size_t(alike[k].a)].number + " " + cut(pts[size_t(alike[k].a)].title, 32) +
                                    "  ~  " + pts[size_t(alike[k].b)].number + " " + cut(pts[size_t(alike[k].b)].title, 32));
            }
            if (alike.size() * 20 > leaves.size() && alike.size() >= 3) r.flags.push_back("alike (" + std::to_string(alike.size()) + " pairs)");

            // self: every other line of each point's text as a note; the spec read from the lines left
            auto split = [](const std::string &body, std::string &a, std::string &b) {
                std::istringstream in(body);
                int n = 0;
                for (std::string line; std::getline(in, line);) {
                    if (line.size() < 3) continue;
                    ((n++ % 2) ? a : b) += line + "\n";
                }
            };
            std::vector<podsort::SpecPoint> half = pts;
            std::vector<std::string> notes(pts.size());
            std::vector<int> tested;
            for (int l : leaves) {
                std::string note, keep;
                split(pts[size_t(l)].body, note, keep);
                if (note.size() < 120 || keep.size() < 60) continue;      // too little text to split
                notes[size_t(l)] = note;
                half[size_t(l)].body = keep;
                tested.push_back(l);
            }
            if (tested.size() >= 5) {
                const auto hidx = podai::readSpec(m, half);
                std::vector<std::vector<std::vector<float>>> pieces;
                std::vector<podsort::Doc> wd;
                for (int l : tested) {
                    pieces.push_back(m.readPieces("", "", notes[size_t(l)]));
                    wd.push_back({"", std::string(), notes[size_t(l)], std::string()});
                }
                std::vector<std::string> targets;
                for (int l : leaves) targets.push_back(podsort::pointText(half, l));
                const auto placed = podai::matchSpec(pieces, half, hidx, podsort::matchScores(wd, targets));
                int right = 0, topic = 0, wrong = 0, out = 0;
                std::vector<std::string> misses;
                for (size_t k = 0; k < tested.size(); ++k) {
                    const int want = tested[k];
                    const auto &pl = placed[k];
                    if (pl.point < 0) { ++out; continue; }
                    if (pl.point == want) { ++right; continue; }
                    if (pts[size_t(pl.point)].parent == pts[size_t(want)].parent) ++topic;
                    else ++wrong;
                    if (misses.size() < 3)
                        misses.push_back("  self miss: " + pts[size_t(want)].number + " " + cut(pts[size_t(want)].title, 30) + "  ->  " +
                                         pts[size_t(pl.point)].number + " " + cut(pts[size_t(pl.point)].title, 30));
                }
                char b[160];
                std::snprintf(b, sizeof b, "self %d/%zu (%d%%), %d same topic, %d elsewhere, %d left out", right, tested.size(),
                              int(std::lround(100.0 * right / double(tested.size()))), topic, wrong, out);
                meaning = b;
                for (const auto &x : misses) r.details.push_back(x);
                if (right * 10 < int(tested.size()) * 8) r.flags.push_back("self " + std::to_string(int(std::lround(100.0 * right / double(tested.size())))) + "%");
                if (wrong * 10 > int(tested.size())) r.flags.push_back("confused (" + std::to_string(wrong) + " elsewhere)");
            } else meaning = "self: too little text to test";
        }

        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\n== %s | %s | %zu points, %zu to match | %s | %.0fs\n", r.id.c_str(), r.name.empty() ? "(no name)" : r.name.c_str(),
                    r.points, r.leaves, r.flags.empty() ? "OK" : ("CHECK: " + [&] { std::string j; for (const auto &x : r.flags) j += (j.empty() ? "" : ", ") + x; return j; }()).c_str(), secs);
        if (!meaning.empty()) std::printf("  %s\n", meaning.c_str());
        for (const auto &d : r.details) std::printf("%s\n", d.c_str());
        if (showLeaves.count(id) || showLeaves.count("all"))
            for (int l : leaves) std::printf("  leaf %s %s\n", pts[size_t(l)].number.c_str(), cut(pts[size_t(l)].title, 70).c_str());
        if (r.flags.empty()) ++clean;
        std::fflush(stdout);
    }
    std::printf("\n%d of %zu read with nothing to check\n", clean, specs.size());
    return 0;
}
