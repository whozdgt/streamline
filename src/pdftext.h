#pragma once
// Reads the text of a PDF: plain C++ with no libraries, so it works the same on every platform and in the tests.
//
// It understands what exam boards' specifications and most school and university documents use: compressed
// streams and object streams, files "encrypted" with an empty password (RC4, AES-128, AES-256), and fonts that map
// their characters to Unicode (ToUnicode maps, standard encodings and glyph names). Text is put back into lines in
// the order the PDF draws it, which for documents made in Word or InDesign is the reading order.
// Not handled: scanned pages (pictures of text) and PDFs that need a password to open.
#include <string>
#include <vector>

namespace pdftext {

struct Line {
    std::string text;           // UTF-8
    double x = 0, y = 0;        // where it starts on the page (points from the bottom left)
    double size = 0;            // font size in points
};
struct Page { std::vector<Line> lines; };
struct Document {
    std::vector<Page> pages;
    std::string error;          // why it couldn't be read; "" when it could
    bool encrypted = false;
};

Document read(const std::string &pdf);

// All the text, a line per line, without the lines repeated on most pages (running headers, footers and page numbers).
std::string text(const Document &doc, bool dropRunning = true);
// Lines set noticeably larger than the body text: the document's headings.
std::string headings(const Document &doc);

std::string inflate(const std::string &data, bool *complete = nullptr);   // zlib or raw deflate (for the tests)

} // namespace pdftext
