// Nested sorting and spec matching by key words, on the made-up documents and spec.
//   nest_test <spec_sample.txt>
#include "corpus.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace podsort;
void print(const std::vector<Node> &nodes, const std::vector<Doc> &docs, int parent, int depth)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent != parent) continue;
        std::printf("%*s[%s%s%s]\n", depth * 4, "", nodes[i].number.c_str(), nodes[i].number.empty() ? "" : " ", nodes[i].name.c_str());
        for (int d : nodes[i].docs) std::printf("%*s- %s\n", depth * 4 + 4, "", docs[size_t(d)].title.c_str());
        print(nodes, docs, int(i), depth + 1);
    }
}
int main(int argc, char **argv)
{
    const auto docs = corpus();
    std::printf("=== nested, key words\n");
    print(nest(docs, wordGrouper(docs)), docs, -1, 0);
    if (argc < 2) return 0;
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto pts = parseSpec(ss.str());
    const auto leaves = leafPoints(pts);
    std::printf("=== spec: %zu points, %zu to match\n", pts.size(), leaves.size());
    std::vector<std::string> targets;
    for (int l : leaves) targets.push_back(pointText(pts, l));
    const auto m = matchPoints(docs, targets);
    std::vector<int> docPoint;
    for (int x : m) docPoint.push_back(x < 0 ? -1 : leaves[size_t(x)]);
    print(specTree(pts, docPoint), docs, -1, 0);
    return 0;
}
