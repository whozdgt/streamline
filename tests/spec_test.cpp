// How Streamline reads a specification:  spec_test <spec.pdf | spec.txt> [--text]
// Prints the board and course it recognised and the points it found, nested, with how much text each holds.
#include "pdftext.h"
#include "podsort.h"
#include "specread.h"
#include <cstdint>
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
    if (argc > 2 && std::strcmp(argv[2], "--outline") == 0) {     // the heading-like lines, numbered, cut short: the shape
        std::istringstream in(text);                                  // of a document, without its text
        int i = 0;
        for (std::string line; std::getline(in, line); ++i) {
            const std::string l = line.substr(0, 60);
            const bool head = (!l.empty() && std::isdigit(uint8_t(l[0]))) || l.rfind("Topic", 0) == 0 || l.rfind("Paper", 0) == 0 ||
                              l.rfind("Component", 0) == 0 || l.find("option") != std::string::npos || l.find("Option") != std::string::npos ||
                              l.find("AS Level") != std::string::npos || l.find("A Level") != std::string::npos ||
                              l.rfind("Key question", 0) == 0 || l.rfind("Content", 0) == 0;
            if (head) std::printf("%6d  %s\n", i, l.c_str());
        }
        return 0;
    }
    const auto info = specread::identify(text);
    const auto pts = podsort::parseSpec(text);
    size_t leaves = podsort::leafPoints(pts).size();
    std::printf("board: %s\nname: %s\npoints: %zu (to match: %zu)\n", info.board.c_str(), info.name.c_str(), pts.size(), leaves);
    for (const auto &p : pts) {
        size_t cut = 70;
        while (cut < p.title.size() && (uint8_t(p.title[cut]) & 0xC0) == 0x80) --cut;      // not inside a character
        std::string t = p.title.size() > 70 ? p.title.substr(0, cut) + "..." : p.title;
        std::printf("%*s%s%s%s  [%zu]\n", p.depth * 2, "", p.number.c_str(), p.number.empty() ? "" : " ", t.c_str(), p.body.size());
    }
    return 0;
}
