#pragma once
// Pods' on-device AI, on llama.cpp. Plain C++ (no Qt), so it can be tested on its own (tests/podai_test.cpp).
//
//   Meaning model (BGE small): reads a document, in pieces if it is long, and turns it into a list of numbers that
//   sit close to another document's when the two are about the same thing, even if they share no words.
//   Writer model (Qwen2.5 Instruct): reads the start of each document in a topic and writes the topic's name and
//   a one-line summary.
//
// Everything runs on the device's processor. Nothing is sent anywhere.
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "podsort.h"

struct llama_model;
struct llama_context;

namespace podai {

// Called as work goes on; return false to stop early.
using Progress = std::function<bool(int done, int total)>;

class Meaning {
public:
    Meaning();
    ~Meaning();
    bool load(const std::string &path, std::string &error);
    // A document's meaning: its name, headings and text, read in up to `pieces` pieces spread through it.
    std::vector<float> read(const std::string &title, const std::string &headings, const std::string &body, int pieces = 4);
    // The same pieces, each on its own (a long document's parts can be about different points of a spec)
    std::vector<std::vector<float>> readPieces(const std::string &title, const std::string &headings, const std::string &body, int pieces = 4);
    std::vector<float> embed(const std::string &text);
private:
    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    int m_ctxTokens = 512;
    int m_dim = 0;
};

class Writer {
public:
    Writer();
    ~Writer();
    bool load(const std::string &path, std::string &error);
    std::string chat(const std::string &system, const std::string &user, int maxTokens = 80);
    int countTokens(const std::string &text) const;
private:
    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
};

struct DocText {
    std::string title, headings, body;
};

// Groups documents by meaning. detail: 0 broad, 1 balanced, 2 specific.
// Each group lists indexes into vecs; documents that belong with nothing come back as groups of one.
std::vector<std::vector<int>> group(const std::vector<std::vector<float>> &vecs, int detail);
// The same, for some of the documents (one subject's, say), with the cut-off still set by the whole pod.
std::vector<std::vector<int>> groupWithin(const std::vector<std::vector<float>> &vecs, const std::vector<int> &subset, int detail);

// ---- Matching to a specification ----
// A spec read by the meaning model: for every point, the titles above it and its own; for every point documents are
// matched to (a leaf), the whole point and its text in pieces. Kept in a file, so a pod's next sort needn't read it.
struct SpecIndex {
    std::vector<std::vector<float>> title;                     // per point
    std::vector<std::vector<float>> whole;                     // per point (leaves only; others empty)
    std::vector<std::vector<std::vector<float>>> pieces;       // per point (leaves only)
    bool save(const std::string &path) const;
    bool load(const std::string &path, size_t points);
};
SpecIndex readSpec(Meaning &m, const std::vector<podsort::SpecPoint> &points, const Progress &progress = {});
// Where each document goes: the best point (a leaf), the next best, and how far the best stands out from the rest
// of the spec. A document that stands out for nothing is in no point (-1): a shopping list, another subject's notes.
struct Placement {
    int point = -1, second = -1;
    double standout = 0;
    bool sure = false;          // well clear of the rest, and of the next best
};
// docPieces: each document read in pieces (Meaning::readPieces); words: key-word scores [document][leaf]
// (podsort::matchScores over the leaves' pointText), or empty.
std::vector<Placement> matchSpec(const std::vector<std::vector<std::vector<float>>> &docPieces,
                                 const std::vector<podsort::SpecPoint> &points, const SpecIndex &spec,
                                 const std::vector<std::vector<double>> &words);

// Matching to a specification: the spec point each document is closest to in meaning, or -1 when none is close.
std::vector<int> match(const std::vector<std::vector<float>> &docs, const std::vector<std::vector<float>> &points);

// Refreshing a pod: the topic each new document fits best by meaning, or -1 for none (as podsort::placeNew).
std::vector<int> place(const std::vector<std::vector<float>> &vecs, const std::vector<std::vector<int>> &topics,
                       const std::vector<int> &fresh, int detail);

// The writer names one group from its documents. `others` are the names already given, so it picks a different one.
// level: 0 a subject ("History"), 1 a topic ("Cold War"), 2 a part of the topic `parent` ("Cuban Missile Crisis").
struct Named { std::string name, summary; };
Named name(Writer &w, const std::vector<DocText> &docs, const std::vector<std::string> &others, int level = 1,
           const std::string &parent = std::string());

void quiet();      // silence llama.cpp's own log lines

} // namespace podai
