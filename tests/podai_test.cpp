// Pods' on-device AI on the test documents. Needs the models (the ai-models release):
//   podai_test <meaning.gguf> [writer.gguf] [spec.txt]
#include "podai.h"
#include "corpus.h"
#include <functional>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: podai_test meaning.gguf [writer.gguf]\n"); return 2; }
    podai::quiet();
    const auto docs = corpus();
    auto t0 = std::chrono::steady_clock::now();
    auto secs = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
    podai::Meaning m;
    std::string err;
    if (!m.load(argv[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::vector<std::vector<float>> vecs;
    for (const auto &d : docs) vecs.push_back(m.read(d.title, d.headings, d.body));
    std::printf("read %zu documents in %.1fs (dim %zu)\n", docs.size(), secs(), vecs[0].size());
    std::unique_ptr<podai::Writer> w;
    if (argc > 2) {
        w = std::make_unique<podai::Writer>();
        if (!w->load(argv[2], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    }
    int sixes = 0;
    for (int detail = 0; detail < 3; ++detail) {
        const auto groups = podai::group(vecs, detail);
        std::printf("=== detail %d: %zu groups\n", detail, groups.size());
        std::vector<std::string> names;
        int big = 0;
        for (const auto &g : groups) {
            std::string label = g.size() > 1 ? "group" : "alone";
            if (g.size() > 1 && w && detail == 1) {
                std::vector<podai::DocText> dt;
                for (int i : g) dt.push_back({docs[size_t(i)].title, docs[size_t(i)].headings, docs[size_t(i)].body});
                const auto t1 = secs();
                const auto n = podai::name(*w, dt, names);
                names.push_back(n.name);
                label = n.name + "  — " + n.summary + "  (" + std::to_string(secs() - t1).substr(0, 4) + "s)";
            }
            if (g.size() > 1) ++big;
            std::printf("[%s]\n", label.c_str());
            for (int i : g) std::printf("    %s\n", docs[size_t(i)].title.c_str());
        }
        if (detail == 1 && big == 6) sixes = 1;
    }
    // nested: Subjects › Topics › Subtopics, by meaning, named by the writer (parents first)
    auto nodes = podsort::nest(docs, [&](const std::vector<int> &subset, int level) { return podai::groupWithin(vecs, subset, level); });
    if (w) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].other) continue;
            std::vector<podai::DocText> dt;
            for (int d : podsort::subtree(nodes, int(i))) dt.push_back({docs[size_t(d)].title, docs[size_t(d)].headings, docs[size_t(d)].body});
            std::vector<std::string> sibs;
            for (size_t j = 0; j < i; ++j) if (nodes[j].parent == nodes[i].parent) sibs.push_back(nodes[j].name);
            const auto n = podai::name(*w, dt, sibs, nodes[i].level, nodes[i].parent >= 0 ? nodes[size_t(nodes[i].parent)].name : "");
            if (!n.name.empty()) nodes[i].name = n.name;
        }
    }
    std::printf("=== nested by meaning (%.1fs)\n", secs());
    std::function<void(int, int)> print = [&](int parent, int depth) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].parent != parent) continue;
            std::printf("%*s[%s%s%s]\n", depth * 4, "", nodes[i].number.c_str(), nodes[i].number.empty() ? "" : " ", nodes[i].name.c_str());
            for (int d : nodes[i].docs) std::printf("%*s- %s\n", depth * 4 + 4, "", docs[size_t(d)].title.c_str());
            print(int(i), depth + 1);
        }
    };
    print(-1, 0);
    // spec matching by meaning
    if (argc > 3) {
        std::ifstream f(argv[3]);
        std::stringstream ss;
        ss << f.rdbuf();
        const auto pts = podsort::parseSpec(ss.str());
        const auto leaves = podsort::leafPoints(pts);
        std::vector<std::vector<float>> pv;
        for (int l : leaves) pv.push_back(m.embed(podsort::pointText(pts, l)));
        const auto mt = podai::match(vecs, pv);
        std::vector<int> docPoint;
        for (int x : mt) docPoint.push_back(x < 0 ? -1 : leaves[size_t(x)]);
        nodes = podsort::specTree(pts, docPoint);
        std::printf("=== spec by meaning (%.1fs)\n", secs());
        print(-1, 0);
    }
    std::printf("%s (%.1fs)\n", sixes ? "OK" : "CHECK: balanced didn't find the six subjects", secs());
    return 0;
}
