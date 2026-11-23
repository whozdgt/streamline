// How Streamline reads a specification:  spec_test <spec.pdf | spec.txt> [--text]
// Prints the board and course it recognised and the points it found, nested, with how much text each holds.
#include "pdftext.h"
#include "podsort.h"
#include "specread.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: spec_test <spec.pdf|spec.txt> [--text]\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.compare(0, 5, "%PDF-") == 0) {
        const auto doc = pdftext::read(text);
        if (!doc.error.empty()) { std::printf("PDF error: %s\n", doc.error.c_str()); return 1; }
        text = pdftext::text(doc);
    }
    if (argc > 2 && std::strcmp(argv[2], "--text") == 0) { std::fputs(text.c_str(), stdout); return 0; }
    const auto info = specread::identify(text);
    const auto pts = podsort::parseSpec(text);
    size_t leaves = podsort::leafPoints(pts).size();
    std::printf("board: %s\nname: %s\npoints: %zu (to match: %zu)\n", info.board.c_str(), info.name.c_str(), pts.size(), leaves);
    for (const auto &p : pts) {
        std::string t = p.title.size() > 70 ? p.title.substr(0, 70) + "..." : p.title;
        std::printf("%*s%s%s%s  [%zu]\n", p.depth * 2, "", p.number.c_str(), p.number.empty() ? "" : " ", t.c_str(), p.body.size());
    }
    return 0;
}
