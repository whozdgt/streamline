// Prints a PDF's text as Streamline reads it:  pdftext_test file.pdf [--all-lines | --headings]
#include "pdftext.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
int main(int argc, char **argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: pdftext_test file.pdf [--all-lines|--headings]\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const auto doc = pdftext::read(ss.str());
    if (!doc.error.empty()) { std::fprintf(stderr, "error: %s\n", doc.error.c_str()); return 1; }
    const bool all = argc > 2 && std::strcmp(argv[2], "--all-lines") == 0;
    if (argc > 2 && std::strcmp(argv[2], "--pages") == 0) {      // every line, a form feed between pages (as pdftotext)
        for (const auto &p : doc.pages) {
            for (const auto &l : p.lines) std::printf("%s\n", l.text.c_str());
            std::printf("\f");
        }
        return 0;
    }
    const bool heads = argc > 2 && std::strcmp(argv[2], "--headings") == 0;
    std::fputs((heads ? pdftext::headings(doc) : pdftext::text(doc, !all)).c_str(), stdout);
    std::fprintf(stderr, "%zu pages%s\n", doc.pages.size(), doc.encrypted ? ", encrypted" : "");
    return 0;
}
