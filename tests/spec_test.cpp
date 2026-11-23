// How Streamline reads a specification:  spec_test <spec.pdf | spec.txt> [--text]
// Prints the board and course it recognised and the points it found, nested, with how much text each holds.
#include "pdftext.h"
#include "podsort.h"
#include "specread.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
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
    if (argc > 2 && std::strcmp(argv[2], "--head") == 0) {      // the cover page's first lines, to check the name is found
        std::istringstream in(text);
        std::string line;
        for (int i = 0; i < 12 && std::getline(in, line); ++i) std::printf("  | %s\n", line.substr(0, 80).c_str());
        return 0;
    }
    if (argc > 3 && std::strcmp(argv[2], "--find") == 0) {      // the first few lines containing argv[3], cut short
        std::istringstream in(text);
        int shown = 0;
        for (std::string line; std::getline(in, line) && shown < 4;)
            if (line.find(argv[3]) != std::string::npos) { std::printf("  find: %s\n", line.substr(0, 80).c_str()); ++shown; }
        return 0;
    }
    if (argc > 3 && std::strcmp(argv[2], "--around") == 0) {    // a few lines either side of the first line starting with argv[3]
        std::vector<std::string> ls;
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) ls.push_back(line);
        for (size_t i = 0; i < ls.size(); ++i)
            if (ls[i].rfind(argv[3], 0) == 0) {
                for (size_t j = i >= 4 ? i - 4 : 0; j < ls.size() && j <= i + 4; ++j) std::printf("  %s %s\n", j == i ? ">" : " ", ls[j].substr(0, 50).c_str());
                break;
            }
        return 0;
    }
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
