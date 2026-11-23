#include "podsort.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace podsort {
namespace {

// Words that say nothing about a topic: English glue words, and words every school or work document uses.
const std::unordered_set<std::string> &stopwords()
{
    static const std::unordered_set<std::string> s = {
        "the", "and", "for", "are", "but", "not", "you", "all", "any", "can", "had", "her", "was", "one", "our", "out",
        "has", "him", "his", "how", "its", "may", "new", "now", "old", "see", "two", "way", "who", "did", "get", "got",
        "let", "put", "say", "she", "too", "use", "used", "using", "with", "this", "that", "from", "they", "have",
        "were", "been", "their", "there", "these", "those", "them", "then", "than", "what", "when", "where", "which",
        "while", "will", "would", "could", "should", "shall", "about", "above", "after", "again", "against", "also",
        "because", "before", "being", "below", "between", "both", "during", "each", "few", "further", "here", "into",
        "just", "more", "most", "much", "must", "only", "other", "over", "own", "same", "some", "such", "very",
        "under", "until", "upon", "your", "yours", "ours", "theirs", "itself", "himself", "herself", "themselves",
        "does", "doing", "done", "make", "made", "many", "like", "well", "even", "still", "though", "although",
        "however", "therefore", "thus", "hence", "whether", "within", "without", "through", "throughout", "across",
        "along", "among", "around", "towards", "toward", "onto", "per", "via", "etc", "yes", "yet", "every", "another",
        "first", "second", "third", "last", "next", "one", "three", "four", "five", "six", "seven", "eight", "nine",
        "ten", "page", "pages", "copy", "final", "draft", "version", "untitled", "document", "doc", "docx", "file",
        "notes", "note", "work", "homework", "question", "questions", "answer", "answers", "example", "examples",
        "name", "date", "task", "tasks", "part", "parts", "section", "sections", "chapter", "lesson", "class",
        "figure", "table", "write", "writing", "written", "explain", "describe", "discuss", "outline", "assess",
        "evaluate", "analyse", "analyze", "compare", "including", "include", "includes", "known", "called", "become",
        "became", "took", "take", "taken", "came", "come", "went", "going", "gave", "give", "given", "said", "says",
        "think", "thought", "felt", "feel", "want", "wanted", "need", "needed", "also", "able", "often", "always",
        "never", "sometimes", "usually", "important", "main", "key", "great", "large", "small", "high", "low", "long",
        "short", "good", "better", "best", "bad", "worse", "worst", "really", "quite", "rather", "around", "lot",
        "lots", "thing", "things", "something", "anything", "nothing", "everything", "someone", "people", "person",
        "time", "times", "year", "years", "day", "days", "way", "ways", "point", "points", "case", "cases", "fact",
        "number", "end", "start", "began", "begin", "led", "lead", "result", "results", "resulted", "due", "show",
        "shows", "shown", "help", "helped", "find", "found", "look", "looked", "seen", "seem", "seemed", "however",
        "why", "whom", "whose", "ever", "less", "least", "else", "per", "via", "within", "instead", "therefore",
        "furthermore", "moreover", "overall", "conclusion", "introduction", "summary", "paragraph", "essay", "words",
        "word", "mark", "marks", "marker", "agree", "disagree", "extent", "far", "certain", "different", "general",
        "similar", "various", "specific", "particular", "possible", "likely", "clear", "clearly", "simply", "already",
        "despite", "since", "whilst", "unless", "either", "neither", "nor", "own", "upon", "off", "per", "out"};
    return s;
}

bool isWordByte(unsigned char c) { return std::isalnum(c) || c >= 0x80 || c == '\''; }

struct Token { std::string key, shown; };

// A light stem, so "conferences" and "conference" count as one word. Only plurals: anything cleverer makes
// mistakes on names, and names matter here.
std::string stem(const std::string &w)
{
    const size_t n = w.size();
    if (n > 4 && w.compare(n - 3, 3, "ies") == 0) return w.substr(0, n - 3) + "y";
    if (n > 4 && w[n - 1] == 's' && w[n - 2] != 's' && w[n - 2] != 'u' && w[n - 2] != 'i') return w.substr(0, n - 1);
    return w;
}

// Words in reading order; a stopword or a punctuation mark leaves an empty key so phrases don't run across it.
std::vector<Token> tokens(const std::string &text)
{
    std::vector<Token> out;
    std::string cur;
    auto flush = [&](bool gap) {
        if (!cur.empty()) {
            while (!cur.empty() && cur.back() == '\'') cur.pop_back();
            while (!cur.empty() && cur.front() == '\'') cur.erase(cur.begin());
            if (cur.size() > 2 && cur.compare(cur.size() - 2, 2, "'s") == 0) cur.resize(cur.size() - 2);
            std::string low;
            bool digits = true;
            for (unsigned char c : cur) {
                low.push_back(char(c < 0x80 ? std::tolower(c) : c));
                if (!std::isdigit(c)) digits = false;
            }
            if (low.size() >= 3 && !digits && !stopwords().count(low)) out.push_back({stem(low), cur});
            else out.push_back({std::string(), std::string()});
            cur.clear();
        }
        if (gap) out.push_back({std::string(), std::string()});
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = text[i];
        if (isWordByte(c)) {
            cur.push_back(char(c));
        } else if (c == '-' && !cur.empty() && i + 1 < text.size() && std::isalpha((unsigned char)text[i + 1])) {
            cur.push_back('-');      // "non-metal" stays one word
        } else {
            flush(c != ' ');
        }
    }
    flush(false);
    return out;
}

// File names: "berlin_conf-notes2", "ColdWarEssay" → spaced words
std::string splitName(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = s[i];
        if (c == '_' || c == '-' || c == '.' || c == '+') { out.push_back(' '); continue; }
        if (i > 0 && std::isupper(c) && std::islower((unsigned char)s[i - 1])) out.push_back(' ');
        if (i > 0 && std::isdigit(c) != std::isdigit((unsigned char)s[i - 1]) && c < 0x80 && (unsigned char)s[i - 1] < 0x80) out.push_back(' ');
        out.push_back(char(c));
    }
    return out;
}

using Vec = std::vector<std::pair<int, double>>;        // term id → weight, sorted by id

struct Model {
    std::vector<std::unordered_map<int, double>> tf;     // per document: term → weighted count
    std::vector<std::string> terms;                      // id → key ("berlin conference")
    std::vector<std::map<std::string, int>> shownForms;  // id → how it was written, and how often
    std::vector<int> df;
    std::vector<Vec> vec;                                // per document: unit-length TF-IDF
    std::vector<double> idf;
};

Model build(const std::vector<Doc> &docs)
{
    Model m;
    std::unordered_map<std::string, int> ids;
    auto idOf = [&](const std::string &k) {
        auto it = ids.find(k);
        if (it != ids.end()) return it->second;
        const int id = int(m.terms.size());
        ids.emplace(k, id);
        m.terms.push_back(k);
        m.shownForms.emplace_back();
        m.df.push_back(0);
        return id;
    };
    m.tf.resize(docs.size());
    for (size_t d = 0; d < docs.size(); ++d) {
        auto add = [&](const std::string &text, double w) {
            const auto t = tokens(text);
            for (size_t i = 0; i < t.size(); ++i) {
                if (t[i].key.empty()) continue;
                const int id = idOf(t[i].key);
                m.tf[d][id] += w;
                ++m.shownForms[id][t[i].shown];
                if (i + 1 < t.size() && !t[i + 1].key.empty()) {      // a two-word phrase
                    const int pid = idOf(t[i].key + " " + t[i + 1].key);
                    m.tf[d][pid] += w;
                    ++m.shownForms[pid][t[i].shown + " " + t[i + 1].shown];
                }
            }
        };
        add(splitName(docs[d].title), 3.0);
        add(docs[d].headings, 2.0);
        add(docs[d].body, 1.0);
        add(splitName(docs[d].folder), 1.5);
        for (const auto &kv : m.tf[d]) ++m.df[kv.first];
    }
    const double n = double(docs.size());
    m.idf.resize(m.terms.size());
    for (size_t t = 0; t < m.terms.size(); ++t) m.idf[t] = std::log((n + 1.0) / (m.df[t] + 0.5));
    m.vec.resize(docs.size());
    for (size_t d = 0; d < docs.size(); ++d) {
        Vec v;
        double norm = 0;
        for (const auto &kv : m.tf[d]) {
            if (m.df[kv.first] < 2) continue;              // a word only one document has can't link it to another
            if (n >= 6 && m.df[kv.first] > 0.7 * n) continue;   // nor can a word nearly every document has
            const double w = (1.0 + std::log(kv.second)) * m.idf[kv.first];
            v.push_back({kv.first, w});
            norm += w * w;
        }
        norm = std::sqrt(norm);
        if (norm > 0) for (auto &p : v) p.second /= norm;
        std::sort(v.begin(), v.end());
        m.vec[d] = std::move(v);
    }
    return m;
}

double dot(const Vec &a, const Vec &b)
{
    double s = 0;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].first < b[j].first) ++i;
        else if (a[i].first > b[j].first) ++j;
        else { s += a[i].second * b[j].second; ++i; ++j; }
    }
    return s;
}

double threshold(int detail) { return detail <= 0 ? 0.025 : detail == 1 ? 0.06 : 0.2; }

std::string titleCase(const std::string &s)
{
    // Keep a word as written when it has capitals of its own (EPQ, McCarthy); otherwise capitalise its first letter.
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        std::string w = s.substr(i, j - i);
        bool caps = false;
        for (unsigned char c : w) if (std::isupper(c)) caps = true;
        if (!caps && !w.empty() && (unsigned char)w[0] < 0x80) w[0] = char(std::toupper((unsigned char)w[0]));
        if (!out.empty()) out.push_back(' ');
        out += w;
        i = j + 1;
    }
    return out;
}

std::string shown(const Model &m, int id)
{
    // the most common way it was written; ties go to the one with capitals
    std::string best;
    int bestN = -1;
    for (const auto &kv : m.shownForms[id]) {
        bool caps = false;
        for (unsigned char c : kv.first) if (std::isupper(c)) caps = true;
        const int n = kv.second * 2 + (caps ? 1 : 0);
        if (n > bestN) { bestN = n; best = kv.first; }
    }
    return titleCase(best);
}

// Names a group: the words most of its documents share and few documents outside it do.
void name(const Model &m, Topic &t, size_t total)
{
    std::unordered_map<int, int> inCount;
    std::unordered_map<int, double> inWeight;
    for (int d : t.docs)
        for (const auto &kv : m.tf[size_t(d)]) { ++inCount[kv.first]; inWeight[kv.first] += kv.second; }
    const double size = double(t.docs.size()), outside = double(total - t.docs.size());
    std::vector<std::pair<double, int>> scored;
    for (const auto &kv : inCount) {
        const int id = kv.first;
        const double cover = kv.second / size;
        if (t.docs.size() > 1 && kv.second < 2) continue;
        const double out = outside > 0 ? (m.df[size_t(id)] - kv.second) / outside : 0.0;
        const bool phrase = m.terms[size_t(id)].find(' ') != std::string::npos;
        const double score = cover * cover * std::max(0.05, cover - out) * std::log(1.0 + inWeight[id] / size)
                             * (phrase ? 1.35 : 1.0);
        scored.push_back({score, id});
    }
    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<std::string> picked;
    std::vector<double> pickedCover;
    std::set<std::string> used;           // word keys already shown, so "Cold War" isn't followed by "War"
    // Skip what adds nothing: a word already shown, or a phrase starting with one ("Congo", then "Congo Free").
    // "Lady Macbeth" after "Macbeth" stays: it adds a word at the front.
    auto covered = [&](const std::string &key) {
        const size_t sp = key.find(' ');
        if (used.count(key.substr(0, sp))) return true;
        if (sp == std::string::npos) return false;
        return used.count(key.substr(sp + 1)) > 0 && used.count(key.substr(0, sp)) > 0;
    };
    for (const auto &s : scored) {
        const std::string &key = m.terms[size_t(s.second)];
        if (covered(key)) continue;
        picked.push_back(shown(m, s.second));
        pickedCover.push_back(inCount[s.second] / size);
        size_t i = 0;
        while (i < key.size()) {
            size_t j = key.find(' ', i);
            if (j == std::string::npos) j = key.size();
            used.insert(key.substr(i, j - i));
            i = j + 1;
        }
        if (picked.size() >= 6) break;
    }
    if (picked.empty()) { t.name = "Untitled topic"; return; }
    // A broad group joins topics that share no single word ("Cells" and "Bonding"): name it after the two biggest.
    if (picked.size() > 2 && pickedCover[0] < 0.75) {
        t.name = picked[0] + " & " + picked[1];
        t.keywords.assign(picked.begin() + 2, picked.end());
    } else {
        t.name = picked[0];
        t.keywords.assign(picked.begin() + 1, picked.end());
    }
}

} // namespace

std::vector<Topic> sortDocs(const std::vector<Doc> &docs, int detail)
{
    std::vector<Topic> out;
    if (docs.empty()) return out;
    const Model m = build(docs);
    const size_t n = docs.size();

    // average-linkage clustering on cosine similarity
    std::vector<std::vector<double>> sim(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) sim[i][j] = sim[j][i] = dot(m.vec[i], m.vec[j]);
    std::vector<std::vector<int>> groups(n);
    std::vector<bool> alive(n, true);
    for (size_t i = 0; i < n; ++i) groups[i] = {int(i)};
    const double t = threshold(detail);
    for (;;) {
        double best = t;
        int a = -1, b = -1;
        for (size_t i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            for (size_t j = i + 1; j < n; ++j)
                if (alive[j] && sim[i][j] > best) { best = sim[i][j]; a = int(i); b = int(j); }
        }
        if (a < 0) break;
        if (std::getenv("PODSORT_TRACE")) std::fprintf(stderr, "merge %.3f  (%zu + %zu)\n", best, groups[size_t(a)].size(), groups[size_t(b)].size());
        const double na = double(groups[size_t(a)].size()), nb = double(groups[size_t(b)].size());
        for (size_t k = 0; k < n; ++k) {
            if (!alive[k] || int(k) == a || int(k) == b) continue;
            const double s = (na * sim[size_t(a)][k] + nb * sim[size_t(b)][k]) / (na + nb);
            sim[size_t(a)][k] = sim[k][size_t(a)] = s;
        }
        groups[size_t(a)].insert(groups[size_t(a)].end(), groups[size_t(b)].begin(), groups[size_t(b)].end());
        alive[size_t(b)] = false;
    }

    Topic other;
    other.other = true;
    for (size_t i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        if (groups[i].size() == 1 && n > 2) { other.docs.push_back(groups[i][0]); continue; }
        Topic tp;
        tp.docs = groups[i];
        std::sort(tp.docs.begin(), tp.docs.end());
        name(m, tp, n);
        out.push_back(tp);
    }
    std::sort(out.begin(), out.end(), [](const Topic &x, const Topic &y) { return x.docs.size() > y.docs.size(); });
    // two groups with the same name: add the next word to tell them apart
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j)
            if (out[i].name == out[j].name) {
                for (Topic *p : {&out[i], &out[j]})
                    if (!p->keywords.empty()) { p->name += " · " + p->keywords.front(); p->keywords.erase(p->keywords.begin()); }
            }
    if (!other.docs.empty()) {
        std::sort(other.docs.begin(), other.docs.end());
        other.name = "Other";
        out.push_back(other);
    }
    return out;
}

std::vector<int> placeNew(const std::vector<Doc> &docs, const std::vector<std::vector<int>> &topics,
                          const std::vector<int> &fresh, int detail)
{
    std::vector<int> out(fresh.size(), -1);
    if (docs.empty()) return out;
    const Model m = build(docs);
    const double t = threshold(detail);
    for (size_t f = 0; f < fresh.size(); ++f) {
        double best = t;
        for (size_t k = 0; k < topics.size(); ++k) {
            if (topics[k].empty()) continue;
            double s = 0;
            for (int d : topics[k]) s += dot(m.vec[size_t(fresh[f])], m.vec[size_t(d)]);
            s /= double(topics[k].size());
            if (s > best) { best = s; out[f] = int(k); }
        }
    }
    return out;
}

} // namespace podsort
