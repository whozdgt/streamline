// How often Pods put a document under the right point of a specification.
//   match_eval <meaning.gguf> <spec.pdf|spec.txt> <notes.txt> [--leaves] [--misses]
// notes.txt: students' notes written for the test, each one under a line "=== <point> | <file name>", <point> being
// the spec point it is about ("4.1.1.2"; a point with points inside it accepts any of them). Lines starting "#"
// are comments ("# spec: <url>" says where the spec comes from, for CI).
// Prints, for each way of matching, how many documents went to the right point, to the right topic (the point's
// parent) and to the right section (the top of the spec), so changes to the matching can be compared.
#include "pdftext.h"
#include "podai.h"
#include "podsort.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Note { std::string label, title, body; };

std::vector<Note> readNotes(const std::string &path)
{
    std::ifstream f(path);
    std::vector<Note> out;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#", 0) == 0 && (out.empty() || line.rfind("# ", 0) == 0)) continue;
        if (line.rfind("=== ", 0) == 0) {
            Note n;
            const std::string rest = line.substr(4);
            const size_t bar = rest.find('|');
            n.label = rest.substr(0, bar == std::string::npos ? rest.size() : bar);
            n.title = bar == std::string::npos ? std::string() : rest.substr(bar + 1);
            auto trim = [](std::string &s) {
                s.erase(0, s.find_first_not_of(" \t"));
                s.erase(s.find_last_not_of(" \t") + 1);
            };
            trim(n.label);
            trim(n.title);
            out.push_back(n);
            continue;
        }
        if (!out.empty()) out.back().body += line + "\n";
    }
    return out;
}

double cosv(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.empty() || b.empty()) return 0;
    double s = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) s += double(a[i]) * b[i];
    return s;
}

std::vector<float> meanOf(const std::vector<std::vector<float>> &vs)
{
    std::vector<float> m;
    for (const auto &v : vs) {
        if (m.empty()) m.assign(v.size(), 0.f);
        for (size_t i = 0; i < v.size(); ++i) m[i] += v[i];
    }
    double s = 0;
    for (float x : m) s += double(x) * x;
    s = std::sqrt(s);
    if (s > 0) for (float &x : m) x = float(x / s);
    return m;
}

// z-scores of one document's scores across the points
std::vector<double> zs(const std::vector<double> &v)
{
    double mean = 0, sd = 0;
    for (double x : v) mean += x;
    mean /= double(std::max<size_t>(1, v.size()));
    for (double x : v) sd += (x - mean) * (x - mean);
    sd = std::sqrt(sd / double(std::max<size_t>(1, v.size())));
    std::vector<double> out;
    for (double x : v) out.push_back(sd > 0 ? (x - mean) / sd : 0);
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: match_eval meaning.gguf spec.pdf notes.txt [--leaves] [--misses]\n"); return 2; }
    bool showLeaves = false, showMisses = false;
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--leaves")) showLeaves = true;
        if (!std::strcmp(argv[i], "--misses")) showMisses = true;
    }
    podai::quiet();

    // the spec
    std::ifstream sf(argv[2], std::ios::binary);
    std::stringstream ss;
    ss << sf.rdbuf();
    std::string text = ss.str();
    if (text.compare(0, 5, "%PDF-") == 0) {
        const auto doc = pdftext::read(text);
        if (!doc.error.empty()) { std::printf("PDF error: %s\n", doc.error.c_str()); return 1; }
        text = pdftext::text(doc);
    }
    const auto points = podsort::parseSpec(text);
    const auto leaves = podsort::leafPoints(points);
    std::map<std::string, int> byNumber;
    for (size_t i = 0; i < points.size(); ++i) if (!points[i].number.empty() && !byNumber.count(points[i].number)) byNumber[points[i].number] = int(i);
    std::printf("spec: %zu points, %zu to match\n", points.size(), leaves.size());
    if (showLeaves)
        for (int l : leaves) std::printf("  leaf %s %s\n", points[size_t(l)].number.c_str(), points[size_t(l)].title.substr(0, 60).c_str());

    // the notes, and the point each is about
    const auto notes = readNotes(argv[3]);
    std::vector<int> label;
    std::vector<bool> offTopic;      // "-": in no point of the spec (a shopping list, another subject's notes)
    for (const auto &n : notes) {
        offTopic.push_back(n.label == "-");
        if (n.label == "-") { label.push_back(-1); continue; }
        const auto it = byNumber.find(n.label);
        if (it == byNumber.end()) std::printf("  no point %s in the spec (note \"%s\")\n", n.label.c_str(), n.title.c_str());
        label.push_back(it == byNumber.end() ? -1 : it->second);
    }
    std::printf("notes: %zu\n", notes.size());

    podai::Meaning m;
    std::string err;
    if (!m.load(argv[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

    // documents: their pieces, and the whole
    std::vector<std::vector<std::vector<float>>> P;
    std::vector<std::vector<float>> M;
    for (const auto &n : notes) {
        P.push_back(m.readPieces(n.title, "", n.body));
        M.push_back(meanOf(P.back()));
    }
    std::printf("read the notes in %.1fs\n", secs());

    // points: the titles above and its own; the whole point as matched today; its text in pieces
    auto titlePath = [&](int i) {
        std::string t = points[size_t(i)].title;
        for (int p = points[size_t(i)].parent; p >= 0; p = points[size_t(p)].parent) t = points[size_t(p)].title + " › " + t;
        return t;
    };
    const std::string instruction = "Represent this sentence for searching relevant passages: ";
    std::vector<std::vector<float>> T(points.size()), Tq(points.size()), F(points.size());
    std::vector<std::vector<std::vector<float>>> B(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        T[i] = m.embed(titlePath(int(i)));
        Tq[i] = m.embed(instruction + titlePath(int(i)));
    }
    for (int l : leaves) {
        F[size_t(l)] = m.embed(podsort::pointText(points, l));
        const std::string &body = points[size_t(l)].body;
        const size_t chunk = 700;
        for (size_t at = 0; at < body.size() && B[size_t(l)].size() < 6; at += chunk) {
            size_t end = std::min(body.size(), at + chunk);
            while (end < body.size() && (uint8_t(body[end]) & 0xC0) == 0x80) ++end;
            B[size_t(l)].push_back(m.embed(titlePath(l) + ": " + body.substr(at, end - at)));
        }
    }
    std::printf("read the spec in %.1fs\n", secs());

    // key words
    std::vector<podsort::Doc> wd;
    for (const auto &n : notes) wd.push_back({n.title, std::string(), n.body, std::string()});
    std::vector<std::string> targets;
    for (int l : leaves) targets.push_back(podsort::pointText(points, l));
    const auto L = podsort::matchScores(wd, targets);

    const size_t nd = notes.size(), nl = leaves.size();
    // the parts every way of matching is built from, [document][leaf]
    using Grid = std::vector<std::vector<double>>;
    auto grid = [&](const std::function<double(size_t, int)> &f) {
        Grid g(nd, std::vector<double>(nl));
        for (size_t d = 0; d < nd; ++d) for (size_t k = 0; k < nl; ++k) g[d][k] = f(d, leaves[k]);
        return g;
    };
    auto bestPiece = [&](size_t d, const std::vector<float> &v) {
        double b = -1;
        for (const auto &p : P[d]) b = std::max(b, cosv(p, v));
        return b;
    };
    const Grid wholeFull = grid([&](size_t d, int l) { return cosv(M[d], F[size_t(l)]); });
    const Grid wholeTitle = grid([&](size_t d, int l) { return cosv(M[d], T[size_t(l)]); });
    const Grid pieceTitleQ = grid([&](size_t d, int l) { return bestPiece(d, Tq[size_t(l)]); });
    const Grid pieceFull = grid([&](size_t d, int l) { return bestPiece(d, F[size_t(l)]); });
    const Grid pieceChunks = grid([&](size_t d, int l) {
        double b = bestPiece(d, T[size_t(l)]);
        for (const auto &c : B[size_t(l)]) b = std::max(b, bestPiece(d, c));
        return b;
    });

    // the leaves under each point, for scores of whole topics and sections
    std::vector<std::vector<int>> under(points.size());      // leaf positions (k) under each point
    for (size_t k = 0; k < nl; ++k)
        for (int p = leaves[k]; p >= 0; p = points[size_t(p)].parent) under[size_t(p)].push_back(int(k));
    // a leaf's score plus how well the document fits the topics and the section above it (the best leaf there)
    auto withContext = [&](const Grid &g, double beta) {
        Grid out = g;
        for (size_t d = 0; d < nd; ++d)
            for (size_t k = 0; k < nl; ++k) {
                double ctx = 0;
                int n = 0;
                for (int p = points[size_t(leaves[k])].parent; p >= 0; p = points[size_t(p)].parent) {
                    double best = -1;
                    for (int j : under[size_t(p)]) best = std::max(best, g[d][size_t(j)]);
                    ctx += best;
                    ++n;
                }
                out[d][k] = g[d][k] + (n ? beta * ctx / n : 0);
            }
        return out;
    };
    auto mix = [&](const std::vector<std::pair<const Grid *, double>> &parts) {
        Grid out(nd, std::vector<double>(nl, 0.0));
        for (size_t d = 0; d < nd; ++d)
            for (const auto &[g, w] : parts) {
                const auto z = zs((*g)[d]);
                for (size_t k = 0; k < nl; ++k) out[d][k] += w * z[k];
            }
        return out;
    };

    struct Way { std::string name; Grid g; };
    std::vector<Way> ways;
    ways.push_back({"today: whole document vs whole point", wholeFull});
    ways.push_back({"whole document vs titles", wholeTitle});
    ways.push_back({"best piece vs titles (query)", pieceTitleQ});
    ways.push_back({"best piece vs whole point", pieceFull});
    ways.push_back({"best piece vs title or point's pieces", pieceChunks});
    ways.push_back({"key words", L});
    const Grid semantic = mix({{&pieceChunks, 1.0}, {&pieceFull, 0.5}, {&pieceTitleQ, 0.5}});
    ways.push_back({"meaning mix", semantic});
    ways.push_back({"meaning mix + topics", withContext(semantic, 0.5)});
    const Grid hybrid = mix({{&pieceChunks, 1.0}, {&pieceFull, 0.5}, {&pieceTitleQ, 0.5}, {&L, 0.6}});
    ways.push_back({"meaning mix + key words", hybrid});
    ways.push_back({"meaning mix + key words + topics", withContext(hybrid, 0.5)});
    ways.push_back({"meaning mix + key words + topics (1.0)", withContext(hybrid, 1.0)});

    auto under_ = [&](int p, int a) {
        for (int q = p; q >= 0; q = points[size_t(q)].parent) if (q == a) return true;
        return false;
    };
    auto top = [&](int p) { while (p >= 0 && points[size_t(p)].parent >= 0) p = points[size_t(p)].parent; return p; };
    // how much a document's best point stands out from the rest (in standard deviations): low for a document that
    // belongs to no point
    auto standout = [&](const std::vector<double> &v, size_t best) {
        double mean = 0, sd = 0;
        for (double x : v) mean += x;
        mean /= double(v.size());
        for (double x : v) sd += (x - mean) * (x - mean);
        sd = std::sqrt(sd / double(v.size()));
        return sd > 0 ? (v[best] - mean) / sd : 0.0;
    };
    std::printf("\n%-42s %8s %8s %8s   %s\n", "way of matching", "point", "topic", "section", "stand-out: right notes (10th pct) / others (max)");
    for (const auto &w : ways) {
        int exact = 0, topic = 0, section = 0, counted = 0;
        std::vector<std::string> misses;
        std::vector<double> rightOut, offOut;
        for (size_t d = 0; d < nd; ++d) {
            size_t bestK = 0;
            for (size_t k = 1; k < nl; ++k) if (w.g[d][k] > w.g[d][bestK]) bestK = k;
            if (offTopic[d]) { offOut.push_back(standout(w.g[d], bestK)); continue; }
            if (label[d] < 0) continue;
            ++counted;
            size_t best = 0;
            for (size_t k = 1; k < nl; ++k) if (w.g[d][k] > w.g[d][best]) best = k;
            const int pred = leaves[best];
            const int parent = points[size_t(label[d])].parent;
            const bool e = under_(pred, label[d]);
            if (e) rightOut.push_back(standout(w.g[d], best));
            exact += e;
            topic += e || (parent >= 0 && under_(pred, parent));
            section += top(pred) == top(label[d]);
            if (!e) misses.push_back("    " + notes[d].label + " " + notes[d].title + "  ->  " + points[size_t(pred)].number + " " +
                                     points[size_t(pred)].title.substr(0, 50));
        }
        auto pc = [&](int x) { return counted ? 100.0 * x / counted : 0.0; };
        std::sort(rightOut.begin(), rightOut.end());
        const double low = rightOut.empty() ? 0 : rightOut[rightOut.size() / 10];
        const double offMax = offOut.empty() ? 0 : *std::max_element(offOut.begin(), offOut.end());
        std::printf("%-42s %7.0f%% %7.0f%% %7.0f%%   %.2f / %.2f\n", w.name.c_str(), pc(exact), pc(topic), pc(section), low, offMax);
        if (showMisses) for (const auto &s : misses) std::printf("%s\n", s.c_str());
    }
    std::printf("\n(%.1fs)\n", secs());
    return 0;
}
