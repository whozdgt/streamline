#pragma once
// Pods: groups documents into topics on the device, with no network and no model.
// Plain C++ (no Qt) so it can be tested on its own (tests/podsort_test.cpp).
//
// How it works: every document becomes a list of weighted words and two-word phrases (TF-IDF: a word counts for
// more when a document uses it a lot and few other documents do; words in the file name and headings count extra).
// Documents that share a lot of those words are similar. Similar documents are joined into groups, the most similar
// first, until the groups left are too different to join (average-linkage clustering). A group's name is the word
// or phrase most of its documents share and the rest of the pod doesn't.
#include <functional>
#include <string>
#include <vector>

namespace podsort {

struct Doc {
    std::string title;       // the file name without its extension (or the document's own title)
    std::string headings;    // heading lines, one per line
    std::string body;        // all the text
    std::string folder;      // the subfolder it is in, relative to the pod's folder ("" at the top)
};

struct Topic {
    std::vector<int> docs;                 // indexes into the input
    std::string name;                      // "Berlin Conference"
    std::vector<std::string> keywords;     // a few more words that describe it
    bool other = false;                    // the documents that didn't fit with anything
};

// detail: 0 = fewer, broader topics, 1 = balanced, 2 = more, narrower topics
std::vector<Topic> sortDocs(const std::vector<Doc> &docs, int detail);

// Names groups found some other way (the on-device AI): each gets its key words as a name; groups of one go in Other.
std::vector<Topic> nameGroups(const std::vector<Doc> &docs, const std::vector<std::vector<int>> &groups);

// ---- Nested sorting: Subjects › Topics › Subtopics ----

struct Node {
    std::string name, summary, number;     // number: a spec point's, such as "3.2.1"
    std::vector<std::string> keywords;
    int parent = -1;                       // index into the same list; -1 at the top
    int level = 0;                         // 0 subject, 1 topic, 2 subtopic (spec: how deep the point is)
    std::vector<int> docs;                 // documents directly in it
    bool other = false;
};
// Splits some documents (indexes into the pod's documents) into groups; level 0 broad, 1 balanced, 2 specific.
using Grouper = std::function<std::vector<std::vector<int>>(const std::vector<int> &subset, int level)>;
// Builds the tree level by level, each level inside the one above. A level that doesn't split anything is skipped,
// so a folder of one subject comes out as that subject's topics. Names come from key words (the writer can rename).
std::vector<Node> nest(const std::vector<Doc> &docs, const Grouper &group);
Grouper wordGrouper(const std::vector<Doc> &docs);           // grouping by key words, as sortDocs
std::vector<int> subtree(const std::vector<Node> &nodes, int node);   // all documents in a node and the nodes inside it

// ---- Matching to a specification ----

struct SpecPoint {
    std::string number, title, body;       // "3.2.1", "Cell structure", what the spec says under it
    int parent = -1;
    int depth = 0;
};
// Reads a specification's text: numbered points ("3.2.1 Cell structure"), skipping its contents page. A text with
// no numbering is split at its headings and paragraphs instead.
std::vector<SpecPoint> parseSpec(const std::string &text);
// The text a point is matched on: the titles above it, its own title and what the spec says under it.
std::string pointText(const std::vector<SpecPoint> &points, int i);
// The points documents are matched to: the most detailed ones (those with nothing numbered under them).
std::vector<int> leafPoints(const std::vector<SpecPoint> &points);
// By key words: the point (index into `targets`) each document is closest to, or -1 when nothing is close.
std::vector<int> matchPoints(const std::vector<Doc> &docs, const std::vector<std::string> &targets);
// How much each document's words have in common with each target's (TF-IDF cosine), [document][target]
std::vector<std::vector<double>> matchScores(const std::vector<Doc> &docs, const std::vector<std::string> &targets);
// The tree for a spec: the points documents matched, inside the points above them; unmatched documents in Other.
std::vector<Node> specTree(const std::vector<SpecPoint> &points, const std::vector<int> &docPoint);

// Refreshing a pod: which of the existing topics each new document fits best, or -1 for none.
// topics[i] lists the indexes (into docs) of the documents already in topic i; fresh lists the new documents.
std::vector<int> placeNew(const std::vector<Doc> &docs, const std::vector<std::vector<int>> &topics,
                          const std::vector<int> &fresh, int detail);

} // namespace podsort
