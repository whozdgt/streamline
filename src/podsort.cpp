#include "podsort.h"
#include "specread.h"

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
// scope: the documents to tell it apart from (its parent's, for a subtopic; the whole pod when null).
// banned: word keys not to use (the parent's name, so "Cold War" inside "Cold War" picks something else).
// keys (out): the word keys of the name it chose.
void name(const Model &m, Topic &t, size_t total, const std::vector<int> *scope = nullptr,
          const std::set<std::string> *banned = nullptr, std::set<std::string> *keys = nullptr)
{
    std::unordered_map<int, int> inCount;
    std::unordered_map<int, double> inWeight;
    for (int d : t.docs)
        for (const auto &kv : m.tf[size_t(d)]) { ++inCount[kv.first]; inWeight[kv.first] += kv.second; }
    std::unordered_map<int, int> scopeCount;      // in the scope, outside this group
    if (scope) {
        const std::set<int> mine(t.docs.begin(), t.docs.end());
        for (int d : *scope)
            if (!mine.count(d)) for (const auto &kv : m.tf[size_t(d)]) ++scopeCount[kv.first];
    }
    const double size = double(t.docs.size());
    const double outside = scope ? double(scope->size() - t.docs.size()) : double(total - t.docs.size());
    std::vector<std::pair<double, int>> scored;
    for (const auto &kv : inCount) {
        const int id = kv.first;
        const double cover = kv.second / size;
        if (t.docs.size() > 1 && kv.second < 2) continue;
        const double out = outside <= 0 ? 0.0 : scope ? scopeCount[id] / outside : (m.df[size_t(id)] - kv.second) / outside;
        const bool phrase = m.terms[size_t(id)].find(' ') != std::string::npos;
        const double score = cover * cover * std::max(0.05, cover - out) * std::log(1.0 + inWeight[id] / size)
                             * (phrase ? 1.35 : 1.0);
        scored.push_back({score, id});
    }
    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<std::string> picked;
    std::vector<double> pickedCover;
    std::vector<std::string> pickedKeys;
    std::set<std::string> used;           // word keys already shown, so "Cold War" isn't followed by "War"
    if (banned) used = *banned;
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
        pickedKeys.push_back(key);
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
    auto addKeys = [&](const std::string &k) {
        if (!keys) return;
        size_t i = 0;
        while (i < k.size()) {
            size_t j = k.find(' ', i);
            if (j == std::string::npos) j = k.size();
            keys->insert(k.substr(i, j - i));
            i = j + 1;
        }
    };
    // A broad group joins topics that share no single word ("Cells" and "Bonding"): name it after the two biggest.
    if (picked.size() > 2 && pickedCover[0] < 0.75) {
        t.name = picked[0] + " & " + picked[1];
        t.keywords.assign(picked.begin() + 2, picked.end());      // its words aren't kept from the topics inside: they are those topics' names
    } else {
        t.name = picked[0];
        t.keywords.assign(picked.begin() + 1, picked.end());
        addKeys(pickedKeys[0]);
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

std::vector<Topic> nameGroups(const std::vector<Doc> &docs, const std::vector<std::vector<int>> &groups)
{
    std::vector<Topic> out;
    if (docs.empty()) return out;
    const Model m = build(docs);
    Topic other;
    other.other = true;
    other.name = "Other";
    for (const auto &g : groups) {
        if (g.size() == 1 && docs.size() > 2) { other.docs.push_back(g[0]); continue; }
        Topic t;
        t.docs = g;
        name(m, t, docs.size());
        out.push_back(t);
    }
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j)
            if (out[i].name == out[j].name)
                for (Topic *p : {&out[i], &out[j]})
                    if (!p->keywords.empty()) { p->name += " · " + p->keywords.front(); p->keywords.erase(p->keywords.begin()); }
    if (!other.docs.empty()) { std::sort(other.docs.begin(), other.docs.end()); out.push_back(other); }
    return out;
}

// ---------------- nested ----------------

Grouper wordGrouper(const std::vector<Doc> &docs)
{
    return [&docs](const std::vector<int> &subset, int level) {
        std::vector<Doc> sub;
        for (int i : subset) sub.push_back(docs[size_t(i)]);
        std::vector<std::vector<int>> out;
        for (const Topic &t : sortDocs(sub, level)) {
            if (t.other) { for (int i : t.docs) out.push_back({subset[size_t(i)]}); continue; }
            std::vector<int> g;
            for (int i : t.docs) g.push_back(subset[size_t(i)]);
            out.push_back(g);
        }
        return out;
    };
}

std::vector<int> subtree(const std::vector<Node> &nodes, int node)
{
    std::vector<int> out = nodes[size_t(node)].docs;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].parent == node) {
            const auto more = subtree(nodes, int(i));
            out.insert(out.end(), more.begin(), more.end());
        }
    return out;
}

std::vector<Node> nest(const std::vector<Doc> &docs, const Grouper &group)
{
    std::vector<Node> nodes;
    std::vector<int> loose;              // documents that fit no subject: Other
    std::function<void(const std::vector<int> &, int, int)> add = [&](const std::vector<int> &subset, int level, int parent) {
        auto direct = [&](const std::vector<int> &d) {
            if (parent < 0) loose.insert(loose.end(), d.begin(), d.end());
            else nodes[size_t(parent)].docs.insert(nodes[size_t(parent)].docs.end(), d.begin(), d.end());
        };
        if (level > 2 || subset.size() < 3) { direct(subset); return; }
        std::vector<std::vector<int>> real;
        std::vector<int> alone;
        for (const auto &g : group(subset, level)) {
            if (g.size() >= 2) real.push_back(g);
            else alone.insert(alone.end(), g.begin(), g.end());
        }
        // a level that splits nothing is skipped (a topic, or a subject that is everything there is)
        const bool splits = real.size() >= 2 || (level == 0 && real.size() == 1 && !alone.empty());
        if (!splits) {
            if (real.size() == 1) { add(real[0], level + 1, parent); direct(alone); }
            else direct(subset);
            return;
        }
        for (const auto &g : real) {
            Node n;
            n.parent = parent;
            n.level = level;
            nodes.push_back(n);
            add(g, level + 1, int(nodes.size()) - 1);
        }
        direct(alone);
    };
    std::vector<int> all(docs.size());
    for (size_t i = 0; i < docs.size(); ++i) all[i] = int(i);
    add(all, 0, -1);

    // Names from key words: a subject against the whole pod; a topic against the rest of its subject, without the
    // subject's own words ("Crises" inside "Cold War", not "Cold War" again). Parents come before their children.
    const Model m = build(docs);
    std::vector<std::set<std::string>> keys(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        Topic t;
        t.docs = subtree(nodes, int(i));
        std::sort(t.docs.begin(), t.docs.end());
        const int p = nodes[i].parent;
        if (p < 0) {
            name(m, t, docs.size(), nullptr, nullptr, &keys[i]);
        } else {
            const std::vector<int> scope = subtree(nodes, p);
            std::set<std::string> banned;
            for (int a = p; a >= 0; a = nodes[size_t(a)].parent) banned.insert(keys[size_t(a)].begin(), keys[size_t(a)].end());
            name(m, t, docs.size(), &scope, &banned, &keys[i]);
        }
        nodes[i].name = t.name;
        nodes[i].keywords = t.keywords;
    }
    // A subject whose key words don't come to one name ("Cell & Bonding") is named after its two biggest topics.
    for (size_t i = nodes.size(); i-- > 0;) {
        if (nodes[i].name.find(" & ") == std::string::npos) continue;
        std::vector<std::pair<size_t, int>> kids;
        for (size_t j = 0; j < nodes.size(); ++j)
            if (nodes[j].parent == int(i)) kids.push_back({subtree(nodes, int(j)).size(), int(j)});
        if (kids.size() < 2) continue;
        std::sort(kids.begin(), kids.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        nodes[i].name = nodes[size_t(kids[0].second)].name + " & " + nodes[size_t(kids[1].second)].name + (kids.size() > 2 ? " …" : "");
    }
    if (!loose.empty()) {
        Node o;
        o.name = "Other";
        o.other = true;
        o.docs = loose;
        nodes.push_back(o);
    }
    return nodes;
}

// ---------------- specifications ----------------

namespace {
std::string trimSpace(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
// "3.2.1 Cell structure ........ 14" → number "3.2.1", title "Cell structure". False when the line isn't a point.
bool pointLine(const std::string &line, std::string &number, std::string &title)
{
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const size_t start = i;
    int parts = 0;
    for (;;) {
        const size_t d = i;
        while (i < line.size() && std::isdigit((unsigned char)line[i])) ++i;
        if (i == d || i - d > 2) return false;          // 1 or 2 digits a part: "2019" isn't a point
        ++parts;
        if (i + 1 < line.size() && line[i] == '.' && std::isdigit((unsigned char)line[i + 1])) { ++i; continue; }
        break;
    }
    number = line.substr(start, i - start);
    if (i < line.size() && line[i] == '.') ++i;
    if (i >= line.size() || (line[i] != ' ' && line[i] != '\t')) return false;
    std::string rest = trimSpace(line.substr(i));
    // drop a contents page's leader dots and page number
    while (!rest.empty() && std::isdigit((unsigned char)rest.back())) rest.pop_back();
    while (!rest.empty() && (rest.back() == '.' || rest.back() == ' ' || rest.back() == '\t')) rest.pop_back();
    if (rest.size() < 3 || rest.size() > 140) return false;
    if (!std::isalpha((unsigned char)rest[0]) && (unsigned char)rest[0] < 0x80) return false;
    if (parts == 1 && std::atoi(number.c_str()) > 40) return false;
    title = rest;
    return true;
}
} // namespace

// The first way of reading a spec (numbered points anywhere, or blocks of text): what specread falls back on.
std::vector<SpecPoint> parseSpecSimple(const std::string &text)
{
    std::vector<std::string> lines;
    {
        size_t i = 0;
        while (i <= text.size()) {
            size_t j = text.find('\n', i);
            if (j == std::string::npos) j = text.size();
            lines.push_back(text.substr(i, j - i));
            i = j + 1;
        }
    }
    std::vector<SpecPoint> pts;
    std::map<std::string, size_t> byNumber;
    int current = -1;
    for (const std::string &line : lines) {
        std::string number, title;
        if (pointLine(line, number, title)) {
            auto it = byNumber.find(number);
            if (it != byNumber.end()) {            // seen before: that was the contents page; the text under this one counts
                current = int(it->second);
                pts[it->second].title = title;
                pts[it->second].body.clear();
                continue;
            }
            SpecPoint p;
            p.number = number;
            p.title = title;
            p.depth = int(std::count(number.begin(), number.end(), '.'));
            byNumber[number] = pts.size();
            pts.push_back(p);
            current = int(pts.size()) - 1;
        } else if (current >= 0 && pts[size_t(current)].body.size() < 2500) {
            const std::string t = trimSpace(line);
            if (!t.empty()) pts[size_t(current)].body += t + "\n";
        }
    }
    if (pts.size() >= 3) {
        for (auto &p : pts) {          // parent: the longest number that starts this one ("3.2" for "3.2.1")
            std::string n = p.number;
            while (true) {
                const size_t dot = n.rfind('.');
                if (dot == std::string::npos) break;
                n.resize(dot);
                auto it = byNumber.find(n);
                if (it != byNumber.end()) { p.parent = int(it->second); break; }
            }
        }
        return pts;
    }
    // No numbering: a point for each block of text, titled by its first line
    pts.clear();
    std::string block;
    auto flush = [&]() {
        const std::string b = trimSpace(block);
        block.clear();
        if (b.size() < 20) return;
        SpecPoint p;
        const size_t nl = b.find('\n');
        p.title = trimSpace(b.substr(0, std::min<size_t>(nl == std::string::npos ? b.size() : nl, 80)));
        p.body = b;
        pts.push_back(p);
    };
    for (const std::string &line : lines) {
        if (trimSpace(line).empty()) flush();
        else block += line + "\n";
    }
    flush();
    return pts;
}

std::vector<SpecPoint> parseSpec(const std::string &text)
{
    auto pts = specread::parse(text);
    if (pts.size() >= 3) return pts;
    auto simple = parseSpecSimple(text);
    return simple.size() > pts.size() ? simple : pts;
}

std::string pointText(const std::vector<SpecPoint> &points, int i)
{
    std::string head;
    for (int p = points[size_t(i)].parent; p >= 0; p = points[size_t(p)].parent) head = points[size_t(p)].title + ". " + head;
    return head + points[size_t(i)].title + ".\n" + points[size_t(i)].body;
}

std::vector<int> leafPoints(const std::vector<SpecPoint> &points)
{
    std::vector<bool> hasChild(points.size(), false);
    for (const auto &p : points) if (p.parent >= 0) hasChild[size_t(p.parent)] = true;
    std::vector<int> out;
    for (size_t i = 0; i < points.size(); ++i) if (!hasChild[i]) out.push_back(int(i));
    return out;
}

std::vector<int> matchPoints(const std::vector<Doc> &docs, const std::vector<std::string> &targets)
{
    std::vector<int> out(docs.size(), -1);
    if (docs.empty() || targets.empty()) return out;
    // one model over documents and points together, so words are weighed the same in both
    std::vector<Doc> all = docs;
    for (const auto &t : targets) all.push_back({std::string(), std::string(), t, std::string()});
    const Model m = build(all);
    std::vector<double> bestScore(docs.size(), 0.0);
    for (size_t d = 0; d < docs.size(); ++d)
        for (size_t p = 0; p < targets.size(); ++p) {
            const double s = dot(m.vec[d], m.vec[docs.size() + p]);
            if (s > bestScore[d]) { bestScore[d] = s; out[d] = int(p); }
        }
    // How close counts as a match depends on how wordy the spec and the documents are, so it is set from this pod:
    // a document must score at least 40% of what a typical document's best match scores.
    std::vector<double> sorted = bestScore;
    std::sort(sorted.begin(), sorted.end());
    const double cutoff = std::max(0.06, 0.4 * sorted[sorted.size() / 2]);
    for (size_t d = 0; d < docs.size(); ++d) if (bestScore[d] < cutoff) out[d] = -1;
    return out;
}

std::vector<Node> specTree(const std::vector<SpecPoint> &points, const std::vector<int> &docPoint)
{
    std::vector<Node> nodes;
    std::map<int, int> nodeOf;          // spec point → node
    std::function<int(int)> nodeFor = [&](int p) -> int {
        auto it = nodeOf.find(p);
        if (it != nodeOf.end()) return it->second;
        const int parent = points[size_t(p)].parent >= 0 ? nodeFor(points[size_t(p)].parent) : -1;
        Node n;
        n.number = points[size_t(p)].number;
        n.name = points[size_t(p)].title;
        n.parent = parent;
        n.level = points[size_t(p)].depth;
        nodes.push_back(n);
        nodeOf[p] = int(nodes.size()) - 1;
        return int(nodes.size()) - 1;
    };
    // in the spec's own order
    std::vector<std::vector<int>> byPoint(points.size());
    std::vector<int> loose;
    for (size_t d = 0; d < docPoint.size(); ++d) {
        if (docPoint[d] >= 0) byPoint[size_t(docPoint[d])].push_back(int(d));
        else loose.push_back(int(d));
    }
    for (size_t p = 0; p < points.size(); ++p)
        if (!byPoint[p].empty()) nodes[size_t(nodeFor(int(p)))].docs = byPoint[p];
    if (!loose.empty()) {
        Node o;
        o.name = "Not in the spec";
        o.other = true;
        o.docs = loose;
        nodes.push_back(o);
    }
    return nodes;
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
