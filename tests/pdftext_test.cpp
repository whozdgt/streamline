// Prints a PDF's text as Streamline reads it:  pdftext_test file.pdf [--all-lines | --headings | --pages | --shape <text> <k> [after] [before]]
#include "pdftext.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
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
    if (argc > 4 && std::strcmp(argv[2], "--shape") == 0) {
        // The layout around the k-th line containing argv[3], without the document's words: each line's page, position
        // and font size, its numbering ("B1.1a", "2.3", "(a)") and a few structural words kept, every other word an x
        // (X when it starts with a capital). Enough to see how a board lays its content out, and nothing to read.
        static const std::set<std::string> keep = {
            "topic", "topics", "title", "sub-topic", "subtopic", "reference", "learning", "outcomes", "outcome", "to", "include",
            "maths", "mathematical", "working", "scientifically", "summary", "underlying", "knowledge", "understanding",
            "common", "misconceptions", "tiering", "content", "contents", "section", "paper", "papers", "unit", "component",
            "module", "key", "part", "option", "options", "practical", "activity", "group", "skills", "specification",
            "continued", "overview", "what", "students", "need", "learn", "guidance", "notes", "and", "of", "the", "in",
            "core", "extended", "supplement", "candidates", "should", "be", "able", "level", "as", "a", "theme", "area",
            "study", "statistics", "mechanics", "pure", "mathematics", "overarching", "themes", "assessment", "objectives"};
        const int k = std::atoi(argv[4]), n = argc > 5 ? std::atoi(argv[5]) : 30, before = argc > 6 ? std::atoi(argv[6]) : 3;
        int seen = 0, printed = -1;
        std::vector<std::pair<int, const pdftext::Line *>> flat;
        for (size_t p = 0; p < doc.pages.size(); ++p)
            for (const auto &l : doc.pages[p].lines) flat.push_back({int(p + 1), &l});
        for (size_t i = 0; i < flat.size() && printed < 0; ++i)
            if (flat[i].second->text.find(argv[3]) != std::string::npos && ++seen == k) printed = int(i);
        if (printed < 0) { std::printf("  (no line %d containing \"%s\")\n", k, argv[3]); return 0; }
        for (size_t i = size_t(std::max(0, printed - before)); i < flat.size() && i <= size_t(printed + n); ++i) {
            std::string out, word;
            auto flush = [&]() {
                if (word.empty()) return;
                std::string low;
                for (char c : word) low.push_back(char(std::tolower(uint8_t(c))));
                std::string bare = low;
                while (!bare.empty() && std::ispunct(uint8_t(bare.back()))) bare.pop_back();
                while (!bare.empty() && std::ispunct(uint8_t(bare.front()))) bare.erase(bare.begin());
                const bool digits = std::any_of(word.begin(), word.end(), [](char c) { return std::isdigit(uint8_t(c)); });
                const bool letterCode = word.size() <= 4 && word.front() == '(' && word.back() == ')';
                const bool punct = std::none_of(word.begin(), word.end(), [](char c) { return std::isalnum(uint8_t(c)) || (uint8_t(c) & 0x80); });
                if (digits || letterCode || punct || keep.count(bare)) out += word;
                else out += std::isupper(uint8_t(word[0])) ? "X" : "x";
                if (!word.empty() && (word.back() == ':' || word.back() == '.') && !digits && !keep.count(bare)) out += word.back();
                out += ' ';
                word.clear();
            };
            for (char c : flat[i].second->text) { if (c == ' ') flush(); else word.push_back(c); }
            flush();
            std::printf("  %s p%-3d x%-4d s%-4.1f %s\n", int(i) == printed ? ">" : " ", flat[i].first, int(flat[i].second->x),
                        flat[i].second->size, out.substr(0, 110).c_str());
        }
        return 0;
    }
    const bool heads = argc > 2 && std::strcmp(argv[2], "--headings") == 0;
    std::fputs((heads ? pdftext::headings(doc) : pdftext::text(doc, !all)).c_str(), stdout);
    std::fprintf(stderr, "%zu pages%s\n", doc.pages.size(), doc.encrypted ? ", encrypted" : "");
    return 0;
}
