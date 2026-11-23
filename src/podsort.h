#pragma once
// Pods: groups documents into topics on the device, with no network and no model.
// Plain C++ (no Qt) so it can be tested on its own (tests/podsort_test.cpp).
//
// How it works: every document becomes a list of weighted words and two-word phrases (TF-IDF: a word counts for
// more when a document uses it a lot and few other documents do; words in the file name and headings count extra).
// Documents that share a lot of those words are similar. Similar documents are joined into groups, the most similar
// first, until the groups left are too different to join (average-linkage clustering). A group's name is the word
// or phrase most of its documents share and the rest of the pod doesn't.
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

// Refreshing a pod: which of the existing topics each new document fits best, or -1 for none.
// topics[i] lists the indexes (into docs) of the documents already in topic i; fresh lists the new documents.
std::vector<int> placeNew(const std::vector<Doc> &docs, const std::vector<std::vector<int>> &topics,
                          const std::vector<int> &fresh, int detail);

} // namespace podsort
