#pragma once
// Reading a course specification: which exam board and course it is, and its points in order, nested.
//
// Taught on the boards' own public specifications (see .github/specs/): Cambridge (IGCSE, O Level, AS & A Level),
// AQA (GCSE, AS and A-level), Pearson Edexcel (GCSE, International GCSE, AS and A level), with a general reading for
// anything else. In short:
//  - Only the subject content counts. Introductions, assessment details, administration and appendices are left out,
//    and so is the contents page, and any line repeated on page after page (headers, footers, column headings).
//  - Cambridge: topics "1 Cell structure", subtopics "1.1 …" (maths: "C1.1"/"E1.1", Core and Extended, are one);
//    the numbered learning statements under a subtopic belong to it. "continued" pages join up.
//  - AQA: numbered to four levels ("4.1.1.1 Eukaryotes and prokaryotes"); History's options ("AA America, 1840–1895")
//    and their parts. AS and A-level content repeated in one document is kept once (the A-level).
//  - Edexcel: "Topic 1 – …" with numbered statements; International GCSE sections, lettered subsections "(a) …" and
//    statements; History's papers, options, key topics and their numbered content.
#include "podsort.h"

#include <string>
#include <vector>

namespace specread {

struct Info {
    std::string board;     // "Cambridge", "AQA", "Edexcel", "OCR", "WJEC" or ""
    std::string name;      // "Cambridge IGCSE Biology (0610)", or "" when the text doesn't say
};
Info identify(const std::string &text);

std::vector<podsort::SpecPoint> parse(const std::string &text);

// How the last parse() on this thread went, for checking the reader on many specifications (tests/course_check.cpp)
struct Diagnostics {
    bool contentFound = false;      // it found where the subject content starts (otherwise it read the whole text)
    size_t from = 0, to = 0, lines = 0;
    std::string first, end;         // the content's first line, and the line it ends at (cut short)
};
Diagnostics lastDiagnostics();

} // namespace specread
