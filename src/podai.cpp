#include "podai.h"

#include "llama.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <numeric>
#include <thread>

namespace podai {
namespace {

void initOnce()
{
    static std::once_flag once;
    std::call_once(once, []() { llama_backend_init(); });
}

int threads()
{
    const unsigned n = std::thread::hardware_concurrency();
    return int(std::clamp(n == 0 ? 4u : n - (n > 4 ? 1u : 0u), 1u, 8u));   // leave one core for the app itself
}

std::vector<llama_token> tokenize(const llama_vocab *vocab, const std::string &text, bool special)
{
    std::vector<llama_token> t(text.size() + 16);
    int n = llama_tokenize(vocab, text.data(), int32_t(text.size()), t.data(), int32_t(t.size()), special, special);
    if (n < 0) {
        t.resize(size_t(-n));
        n = llama_tokenize(vocab, text.data(), int32_t(text.size()), t.data(), int32_t(t.size()), special, special);
    }
    t.resize(size_t(std::max(0, n)));
    return t;
}

void normalize(std::vector<float> &v)
{
    double s = 0;
    for (float x : v) s += double(x) * x;
    s = std::sqrt(s);
    if (s > 0) for (float &x : v) x = float(x / s);
}

// Cuts text at a UTF-8 character boundary near `n` bytes
std::string cut(const std::string &s, size_t from, size_t n)
{
    if (from >= s.size()) return {};
    while (from > 0 && from < s.size() && (static_cast<unsigned char>(s[from]) & 0xC0) == 0x80) ++from;
    size_t end = std::min(s.size(), from + n);
    while (end < s.size() && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) ++end;
    return s.substr(from, end - from);
}

std::string clean(std::string s)
{
    for (char &c : s) if (c == '\r' || c == '\t') c = ' ';
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\n') { if (!space && !out.empty()) out.push_back(c == '\n' ? '\n' : ' '); space = true; }
        else { out.push_back(c); space = false; }
    }
    return out;
}

} // namespace

void quiet()
{
    llama_log_set([](ggml_log_level level, const char *text, void *) {
        if (level == GGML_LOG_LEVEL_ERROR) fputs(text, stderr);
    }, nullptr);
}

// ---------------- Meaning ----------------

Meaning::Meaning() { initOnce(); }
Meaning::~Meaning()
{
    if (m_ctx) llama_free(m_ctx);
    if (m_model) llama_model_free(m_model);
}

bool Meaning::load(const std::string &path, std::string &error)
{
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    m_model = llama_model_load_from_file(path.c_str(), mp);
    if (!m_model) { error = "couldn't load the meaning model"; return false; }
    m_ctxTokens = std::clamp(llama_model_n_ctx_train(m_model), 128, 512);
    llama_context_params cp = llama_context_default_params();
    cp.embeddings = true;
    cp.n_ctx = uint32_t(m_ctxTokens);
    cp.n_batch = cp.n_ubatch = uint32_t(m_ctxTokens);    // a model that reads both ways needs the whole text in one go
    cp.n_seq_max = 1;
    cp.n_threads = cp.n_threads_batch = threads();
    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) { error = "couldn't start the meaning model"; return false; }
    m_dim = llama_model_n_embd_out(m_model);
    if (m_dim <= 0) m_dim = llama_model_n_embd(m_model);
    return true;
}

std::vector<float> Meaning::embed(const std::string &text)
{
    if (!m_ctx) return {};
    const llama_vocab *vocab = llama_model_get_vocab(m_model);
    std::vector<llama_token> t = tokenize(vocab, text, true);
    if (t.empty()) return {};
    if (int(t.size()) > m_ctxTokens) {      // keep the start, and the end-of-text marker the model expects
        const llama_token last = t.back();
        t.resize(size_t(m_ctxTokens));
        t.back() = last;
    }
    llama_memory_clear(llama_get_memory(m_ctx), true);
    llama_batch b = llama_batch_init(int32_t(t.size()), 0, 1);
    for (size_t i = 0; i < t.size(); ++i) {
        b.token[i] = t[i];
        b.pos[i] = llama_pos(i);
        b.n_seq_id[i] = 1;
        b.seq_id[i][0] = 0;
        b.logits[i] = true;
    }
    b.n_tokens = int32_t(t.size());
    const bool encoderOnly = llama_model_has_encoder(m_model) && !llama_model_has_decoder(m_model);
    const int rc = encoderOnly ? llama_encode(m_ctx, b) : llama_decode(m_ctx, b);
    std::vector<float> out;
    if (rc == 0) {
        const float *e = llama_get_embeddings_seq(m_ctx, 0);
        if (e) out.assign(e, e + m_dim);
    }
    llama_batch_free(b);
    normalize(out);
    return out;
}

std::vector<float> Meaning::read(const std::string &title, const std::string &headings, const std::string &body, int pieces)
{
    std::vector<float> sum;
    for (const auto &e : readPieces(title, headings, body, pieces)) {
        if (sum.empty()) sum.assign(e.size(), 0.f);
        for (size_t i = 0; i < e.size(); ++i) sum[i] += e[i];
    }
    normalize(sum);
    return sum;
}

std::vector<std::vector<float>> Meaning::readPieces(const std::string &title, const std::string &headings, const std::string &body, int pieces)
{
    // Every piece starts with the name and headings, so each one knows what document it is from.
    std::string head = clean(title);
    if (!headings.empty()) head += "\n" + cut(clean(headings), 0, 300);
    const std::string text = clean(body);
    const size_t piece = size_t(std::max(400, m_ctxTokens * 3));     // about a piece's worth of words, in bytes
    std::vector<std::string> parts;
    if (text.size() <= piece || pieces <= 1) {
        parts.push_back(head + "\n" + cut(text, 0, piece));
    } else {
        const int n = std::min(pieces, int((text.size() + piece - 1) / piece));
        const size_t step = (text.size() - piece) / size_t(std::max(1, n - 1));
        for (int i = 0; i < n; ++i) parts.push_back(head + "\n" + cut(text, size_t(i) * step, piece));
    }
    std::vector<std::vector<float>> out;
    for (const std::string &p : parts) {
        std::vector<float> e = embed(p);
        if (!e.empty()) out.push_back(std::move(e));
    }
    return out;
}

// ---------------- Writer ----------------

Writer::Writer() { initOnce(); }
Writer::~Writer()
{
    if (m_ctx) llama_free(m_ctx);
    if (m_model) llama_model_free(m_model);
}

bool Writer::load(const std::string &path, std::string &error)
{
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    m_model = llama_model_load_from_file(path.c_str(), mp);
    if (!m_model) { error = "couldn't load the writer model"; return false; }
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 4096;
    cp.n_batch = 512;
    cp.n_ubatch = 512;
    cp.n_seq_max = 1;
    cp.n_threads = cp.n_threads_batch = threads();
    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) { error = "couldn't start the writer model"; return false; }
    return true;
}

int Writer::countTokens(const std::string &text) const
{
    return m_model ? int(tokenize(llama_model_get_vocab(m_model), text, false).size()) : 0;
}

std::string Writer::chat(const std::string &system, const std::string &user, int maxTokens)
{
    if (!m_ctx) return {};
    const llama_vocab *vocab = llama_model_get_vocab(m_model);
    const char *tmpl = llama_model_chat_template(m_model, nullptr);
    const llama_chat_message msgs[] = {{"system", system.c_str()}, {"user", user.c_str()}};
    std::vector<char> buf(system.size() + user.size() + 1024);
    int n = llama_chat_apply_template(tmpl, msgs, 2, true, buf.data(), int32_t(buf.size()));
    if (n > int(buf.size())) {
        buf.resize(size_t(n) + 1);
        n = llama_chat_apply_template(tmpl, msgs, 2, true, buf.data(), int32_t(buf.size()));
    }
    if (n < 0) return {};
    std::vector<llama_token> prompt = tokenize(vocab, std::string(buf.data(), size_t(n)), true);
    const int room = int(llama_n_ctx(m_ctx)) - maxTokens - 8;
    if (int(prompt.size()) > room) prompt.erase(prompt.begin() + room / 4, prompt.begin() + room / 4 + (long(prompt.size()) - room));

    llama_memory_clear(llama_get_memory(m_ctx), true);
    for (size_t i = 0; i < prompt.size(); i += 512) {
        const int32_t len = int32_t(std::min<size_t>(512, prompt.size() - i));
        if (llama_decode(m_ctx, llama_batch_get_one(prompt.data() + i, len)) != 0) return {};
    }
    llama_sampler *smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(llama_vocab_n_tokens(vocab), 64, 1.15f, 0.0f, 0.0f));
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    std::string out;
    for (int i = 0; i < maxTokens; ++i) {
        llama_token tok = llama_sampler_sample(smpl, m_ctx, -1);
        if (llama_vocab_is_eog(vocab, tok)) break;
        char piece[256];
        const int len = llama_token_to_piece(vocab, tok, piece, sizeof piece, 0, false);
        if (len > 0) out.append(piece, size_t(len));
        if (llama_decode(m_ctx, llama_batch_get_one(&tok, 1)) != 0) break;
    }
    llama_sampler_free(smpl);
    return out;
}

// ---------------- Grouping ----------------

namespace {
double dotv(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.empty() || a.size() != b.size()) return 0.0;
    double s = 0;
    for (size_t k = 0; k < a.size(); ++k) s += double(a[k]) * b[k];
    return s;
}
// Meaning models never score two texts near zero: even unrelated ones come out fairly similar. So cut-offs are set
// from the pod's own scores: a typical pair (the median) and the closest pairs (the top 2%).
// Spec matching: how far (in standard deviations of the spec's points) a document's best point must stand out to be
// matched, and to be sure (and well clear of the next best). Set on tests/match (CI's "match" run).
// When a document goes under a point. Each meaning model spreads its numbers differently, so each has its own
// settings, set on the notes in tests/match (tests/match_eval.cpp prints where each note would go and why).
//   standout: how far the best point stands above the rest (z-score); a document is filed from `match`, and is
//     `sure` (no need to check it) from `sure` when it is also `margin` clear of the next best point
//   fit: how close (cosine) the document's best piece comes to the point's own text. Notes on other subjects can
//     still stand out a little for some point, but don't come this close; from `fitClose`, standing out from
//     `standoutClose` is enough.
struct Tuning {
    double match, sure, margin, fitFloor, fitClose, standoutClose;
};
static Tuning tuningFor(size_t dims)
{
    if (dims >= 768) return {3.2, 4.6, 0.8, 0.57, 0.57, 3.2};   // BGE base (tried, not used: no better than small)
    return {3.6, 4.6, 0.8, 0.63, 0.70, 3.3};                     // BGE small
}

// Points that are not content of their own, such as AQA's "Key ideas" (ideas that run through the whole subject):
// anything at all can look a little like them, so nothing is filed there.
static bool fileable(const podsort::SpecPoint &p)
{
    std::string t;
    for (char c : p.title) t += char(std::tolower(static_cast<unsigned char>(c)));
    while (!t.empty() && (t.back() == ' ' || t.back() == '.')) t.pop_back();
    return t != "key ideas" && t != "key concepts";
}
struct Spread { double median = 0, high = 1; double at(double reach) const { return median + (high - median) * reach; } };
Spread spread(std::vector<double> all)
{
    Spread s;
    if (all.empty()) return s;
    std::sort(all.begin(), all.end());
    s.median = all[all.size() / 2];
    s.high = all[std::min(all.size() - 1, size_t(double(all.size()) * 0.98))];
    return s;
}
Spread docSpread(const std::vector<std::vector<float>> &vecs)
{
    std::vector<double> all;
    for (size_t i = 0; i < vecs.size(); ++i)
        for (size_t j = i + 1; j < vecs.size(); ++j) all.push_back(dotv(vecs[i], vecs[j]));
    return spread(all);
}
double reachFor(int detail) { return detail <= 0 ? 0.30 : detail == 1 ? 0.45 : 0.60; }   // fraction of the way from typical to closest
} // namespace

std::vector<std::vector<int>> group(const std::vector<std::vector<float>> &vecs, int detail)
{
    std::vector<int> all(vecs.size());
    std::iota(all.begin(), all.end(), 0);
    return groupWithin(vecs, all, detail);
}

std::vector<std::vector<int>> groupWithin(const std::vector<std::vector<float>> &vecs, const std::vector<int> &subset, int detail)
{
    const size_t n = subset.size();
    std::vector<std::vector<int>> groups(n);
    for (size_t i = 0; i < n; ++i) groups[i] = {subset[i]};
    if (n < 2) return groups;
    // the cut-off comes from the whole pod, so "a topic" means the same thing inside every subject
    const double cutoff = docSpread(vecs).at(reachFor(detail));
    std::vector<std::vector<double>> sim(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) sim[i][j] = sim[j][i] = dotv(vecs[size_t(subset[i])], vecs[size_t(subset[j])]);

    std::vector<bool> alive(n, true);
    for (;;) {
        double best = cutoff;
        int a = -1, b = -1;
        for (size_t i = 0; i < n; ++i) {
            if (!alive[i]) continue;
            for (size_t j = i + 1; j < n; ++j)
                if (alive[j] && sim[i][j] > best) { best = sim[i][j]; a = int(i); b = int(j); }
        }
        if (a < 0) break;
        const double na = double(groups[size_t(a)].size()), nb = double(groups[size_t(b)].size());
        for (size_t k = 0; k < n; ++k) {
            if (!alive[k] || int(k) == a || int(k) == b) continue;
            const double s = (na * sim[size_t(a)][k] + nb * sim[size_t(b)][k]) / (na + nb);
            sim[size_t(a)][k] = sim[k][size_t(a)] = s;
        }
        groups[size_t(a)].insert(groups[size_t(a)].end(), groups[size_t(b)].begin(), groups[size_t(b)].end());
        alive[size_t(b)] = false;
    }
    std::vector<std::vector<int>> out;
    for (size_t i = 0; i < n; ++i)
        if (alive[i]) { std::sort(groups[i].begin(), groups[i].end()); out.push_back(groups[i]); }
    std::sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.size() > y.size(); });
    return out;
}

std::vector<int> match(const std::vector<std::vector<float>> &docs, const std::vector<std::vector<float>> &points)
{
    std::vector<int> out(docs.size(), -1);
    if (points.empty()) return out;
    std::vector<std::vector<double>> sim(docs.size(), std::vector<double>(points.size()));
    std::vector<double> all;
    for (size_t d = 0; d < docs.size(); ++d)
        for (size_t p = 0; p < points.size(); ++p) { sim[d][p] = dotv(docs[d], points[p]); all.push_back(sim[d][p]); }
    const Spread sp = spread(all);
    const double cutoff = sp.at(0.35);
    for (size_t d = 0; d < docs.size(); ++d) {
        double best = cutoff;
        for (size_t p = 0; p < points.size(); ++p)
            if (sim[d][p] > best) { best = sim[d][p]; out[d] = int(p); }
    }
    return out;
}

// ---- A spec, read for matching ----

namespace {
std::string titlePath(const std::vector<podsort::SpecPoint> &points, int i)
{
    std::string t = points[size_t(i)].title;
    for (int p = points[size_t(i)].parent; p >= 0; p = points[size_t(p)].parent) t = points[size_t(p)].title + " \xE2\x80\xBA " + t;
    return t;
}
// z-scores of one document's scores across the points
std::vector<double> zscores(const std::vector<double> &v)
{
    double mean = 0, sd = 0;
    for (double x : v) mean += x;
    mean /= double(std::max<size_t>(1, v.size()));
    for (double x : v) sd += (x - mean) * (x - mean);
    sd = std::sqrt(sd / double(std::max<size_t>(1, v.size())));
    std::vector<double> out;
    out.reserve(v.size());
    for (double x : v) out.push_back(sd > 0 ? (x - mean) / sd : 0.0);
    return out;
}
const char kIndexMagic[8] = {'S', 'P', 'E', 'C', 'I', 'X', '0', '1'};
} // namespace

SpecIndex readSpec(Meaning &m, const std::vector<podsort::SpecPoint> &points, const Progress &progress)
{
    // BGE's instruction for a short query searching for passages: a point's titles are the query, a document the passage
    static const std::string query = "Represent this sentence for searching relevant passages: ";
    SpecIndex x;
    const auto leaves = podsort::leafPoints(points);
    std::vector<bool> leaf(points.size(), false);
    for (int l : leaves) leaf[size_t(l)] = true;
    x.title.resize(points.size());
    x.whole.resize(points.size());
    x.pieces.resize(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        if (progress && !progress(int(i), int(points.size()))) return {};
        x.title[i] = m.embed(query + titlePath(points, int(i)));
        if (!leaf[i]) continue;
        x.whole[i] = m.embed(podsort::pointText(points, int(i)));
        const std::string &body = points[i].body;
        const size_t chunk = 700;
        for (size_t at = 0; at < body.size() && x.pieces[i].size() < 6; at += chunk) {
            size_t end = std::min(body.size(), at + chunk);
            while (end < body.size() && (static_cast<unsigned char>(body[end]) & 0xC0) == 0x80) ++end;
            x.pieces[i].push_back(m.embed(titlePath(points, int(i)) + ": " + body.substr(at, end - at)));
        }
    }
    return x;
}

bool SpecIndex::save(const std::string &path) const
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto vec = [&](const std::vector<float> &v) { u32(uint32_t(v.size())); f.write(reinterpret_cast<const char *>(v.data()), std::streamsize(v.size() * 4)); };
    f.write(kIndexMagic, 8);
    u32(uint32_t(title.size()));
    for (size_t i = 0; i < title.size(); ++i) {
        vec(title[i]);
        vec(whole[i]);
        u32(uint32_t(pieces[i].size()));
        for (const auto &p : pieces[i]) vec(p);
    }
    return bool(f);
}

bool SpecIndex::load(const std::string &path, size_t points)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[8];
    if (!f.read(magic, 8) || std::memcmp(magic, kIndexMagic, 8) != 0) return false;
    auto u32 = [&](uint32_t &v) { return bool(f.read(reinterpret_cast<char *>(&v), 4)); };
    auto vec = [&](std::vector<float> &v) {
        uint32_t n = 0;
        if (!u32(n) || n > 8192) return false;
        v.resize(n);
        return bool(f.read(reinterpret_cast<char *>(v.data()), std::streamsize(n) * 4));
    };
    uint32_t n = 0;
    if (!u32(n) || n != points) return false;
    SpecIndex x;
    x.title.resize(n);
    x.whole.resize(n);
    x.pieces.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t k = 0;
        if (!vec(x.title[i]) || !vec(x.whole[i]) || !u32(k) || k > 64) return false;
        x.pieces[i].resize(k);
        for (auto &p : x.pieces[i]) if (!vec(p)) return false;
    }
    *this = std::move(x);
    return true;
}

const double kExampleWeight = 0.5;      // tests/match: more sure, no fewer right; heavier left more out

std::vector<Placement> matchSpec(const std::vector<std::vector<std::vector<float>>> &docPieces,
                                 const std::vector<podsort::SpecPoint> &points, const SpecIndex &spec,
                                 const std::vector<std::vector<double>> &words,
                                 const std::vector<int> &examples, double exampleWeight)
{
    const auto allLeaves = podsort::leafPoints(points);
    std::vector<Placement> out(docPieces.size());
    if (allLeaves.empty() || spec.title.size() != points.size()) return out;
    std::vector<int> leaves;      // the leaves documents can be filed under
    std::vector<size_t> column;   // each one's column in words[d], which covers every leaf
    for (size_t k = 0; k < allLeaves.size(); ++k)
        if (fileable(points[size_t(allLeaves[k])])) { leaves.push_back(allLeaves[k]); column.push_back(k); }
    if (leaves.empty()) return out;
    const size_t nl = leaves.size();
    Tuning t = tuningFor(spec.title.empty() ? 0 : spec.title[0].size());
    // How far one point can stand out depends on how many there are (at most sqrt(n - 1) standard deviations), so a
    // short spec (AQA GCSE Geography: 27 points) asks less of it than the long one the settings were made on (98).
    // Closeness to the point's own text (fitFloor) still keeps other subjects out.
    {
        const double s = std::clamp(std::sqrt(double(nl > 1 ? nl - 1 : 1) / 97.0), 0.55, 1.0);
        t.match *= s; t.sure *= s; t.standoutClose *= s; t.margin *= s;
    }
    // the documents you have put under a point yourself: each one's leaf (k), and the whole document's meaning
    std::map<int, size_t> leafPos;
    for (size_t k = 0; k < nl; ++k) leafPos[leaves[k]] = k;
    std::vector<int> exampleLeaf(docPieces.size(), -1);
    std::vector<std::vector<float>> whole(docPieces.size());
    size_t exampleLeaves = 0;
    {
        std::vector<bool> seen(nl, false);
        for (size_t d = 0; d < docPieces.size() && d < examples.size(); ++d) {
            const auto it = leafPos.find(examples[d]);
            if (it == leafPos.end()) continue;
            exampleLeaf[d] = int(it->second);
            if (!seen[it->second]) { seen[it->second] = true; ++exampleLeaves; }
        }
        if (exampleLeaves > 0)
            for (size_t d = 0; d < docPieces.size(); ++d) {
                auto &m = whole[d];
                for (const auto &v : docPieces[d]) {
                    if (m.empty()) m.assign(v.size(), 0.f);
                    for (size_t i = 0; i < v.size() && i < m.size(); ++i) m[i] += v[i];
                }
                double len = 0;
                for (float x : m) len += double(x) * x;
                len = std::sqrt(len);
                if (len > 0) for (float &x : m) x = float(x / len);
            }
    }
    for (size_t d = 0; d < docPieces.size(); ++d) {
        const auto &P = docPieces[d];
        if (P.empty()) continue;
        auto best = [&](const std::vector<float> &v) {
            double b = -1;
            for (const auto &p : P) b = std::max(b, dotv(p, v));
            return b;
        };
        // Each part on its own scale, then added up: the document's best piece against the point's titles (as a
        // query), against the whole point, and against the point's own pieces of text; and the words they share.
        std::vector<double> byTitle(nl), byWhole(nl), byPieces(nl);
        for (size_t k = 0; k < nl; ++k) {
            const size_t l = size_t(leaves[k]);
            byTitle[k] = best(spec.title[l]);
            byWhole[k] = best(spec.whole[l]);
            double b = byTitle[k];
            for (const auto &c : spec.pieces[l]) b = std::max(b, best(c));
            byPieces[k] = b;
        }
        std::vector<double> total(nl, 0.0);
        auto add = [&](const std::vector<double> &v, double w) {
            const auto z = zscores(v);
            for (size_t k = 0; k < nl; ++k) total[k] += w * z[k];
        };
        add(byPieces, 1.0);
        add(byWhole, 0.5);
        add(byTitle, 0.5);
        if (d < words.size() && words[d].size() == allLeaves.size()) {
            std::vector<double> w(nl);
            for (size_t k = 0; k < nl; ++k) w[k] = words[d][column[k]];
            add(w, 0.6);
        }
        // Documents you have filed: how like them this one is, at each point that has some (on its own scale among
        // those points, so a point you have filed nothing under is neither helped nor held back)
        if (exampleLeaves >= 3 && exampleWeight > 0 && exampleLeaf[d] < 0) {
            std::vector<double> like(nl, -2.0);
            for (size_t e = 0; e < docPieces.size(); ++e)
                if (e != d && exampleLeaf[e] >= 0)
                    like[size_t(exampleLeaf[e])] = std::max(like[size_t(exampleLeaf[e])], dotv(whole[d], whole[e]));
            std::vector<double> have;
            for (double x : like) if (x > -2.0) have.push_back(x);
            const auto z = zscores(have);
            for (size_t k = 0, i = 0; k < nl; ++k) if (like[k] > -2.0) total[k] += exampleWeight * z[i++];
        }
        size_t b1 = 0, b2 = nl > 1 ? 1 : 0;
        for (size_t k = 0; k < nl; ++k) {
            if (total[k] > total[b1]) { b2 = b1; b1 = k; }
            else if (k != b1 && (b2 == b1 || total[k] > total[b2])) b2 = k;
        }
        const auto z = zscores(total);
        Placement &pl = out[d];
        pl.standout = z[b1];
        pl.best = leaves[b1];
        pl.fit = byPieces[b1];
        const bool fits = pl.fit >= t.fitFloor &&
                          (pl.standout >= t.match || (pl.standout >= t.standoutClose && pl.fit >= t.fitClose));
        pl.point = fits ? leaves[b1] : -1;
        pl.second = nl > 1 ? leaves[b2] : -1;
        pl.sure = pl.point >= 0 && pl.standout >= t.sure && z[b1] - z[b2] >= t.margin;
        if (exampleLeaf[d] >= 0) {      // you put it here: it stays
            pl.point = leaves[size_t(exampleLeaf[d])];
            pl.sure = true;
        }
    }
    return out;
}

std::vector<int> place(const std::vector<std::vector<float>> &vecs, const std::vector<std::vector<int>> &topics,
                       const std::vector<int> &fresh, int detail)
{
    std::vector<int> out(fresh.size(), -1);
    if (vecs.size() < 2) return out;
    const double cutoff = docSpread(vecs).at(reachFor(detail));
    for (size_t f = 0; f < fresh.size(); ++f) {
        double best = cutoff;
        for (size_t t = 0; t < topics.size(); ++t) {
            if (topics[t].empty()) continue;
            double s = 0;
            for (int d : topics[t]) s += dotv(vecs[size_t(fresh[f])], vecs[size_t(d)]);
            s /= double(topics[t].size());
            if (s > best) { best = s; out[f] = int(t); }
        }
    }
    return out;
}

// ---------------- Naming ----------------

namespace {
std::string trim(std::string s)
{
    const char *ws = " \t\r\n\"'*`#:-.";
    s.erase(0, s.find_first_not_of(ws));
    const size_t e = s.find_last_not_of(" \t\r\n\"'*`");
    s.erase(e == std::string::npos ? 0 : e + 1);
    return s;
}
std::string field(const std::string &text, const std::string &label)
{
    std::string low = text;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::string key = label;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const size_t p = low.find(key + ":");
    if (p == std::string::npos) return {};
    const size_t s = p + key.size() + 1;
    const size_t e = text.find('\n', s);
    return trim(text.substr(s, e == std::string::npos ? std::string::npos : e - s));
}
} // namespace

Named nameOnce(Writer &w, const std::vector<DocText> &docs, const std::vector<std::string> &others, int level, const std::string &parent, bool simple)
{
    // Each document gets an equal share of about 2,400 tokens (roughly 1,800 words in all): its name, headings and the
    // start of its text. Plenty for the writer to see what the topic is, short enough to be quick on a phone.
    const size_t share = std::max<size_t>(240, 9000 / std::max<size_t>(1, docs.size()));
    std::string list;
    for (size_t i = 0; i < docs.size() && i < 24; ++i) {
        list += "Document: " + clean(docs[i].title) + "\n";
        if (!docs[i].headings.empty()) list += "Headings: " + cut(clean(docs[i].headings), 0, 200) + "\n";
        list += cut(clean(docs[i].body), 0, share) + "\n\n";
    }
    std::string avoid;
    for (const auto &o : others) avoid += (avoid.empty() ? "" : ", ") + o;
    const std::string system =
        "You sort a student's documents into folders. You read documents that belong together and name their shared topic.";
    const std::string ask =
        level == 0 ? "These documents are all from one school or university subject. Name the subject in 1 to 3 words, like a folder name "
                     "(for example \"History\", \"Biology\", \"English Literature\" or \"Computer Science\"), and say in one sentence what they cover."
      : level == 2 ? "These documents are about one part of the topic \"" + parent + "\". Name that part in 1 to 4 words, more specific than \""
                     + parent + "\" (for example \"Cuban Missile Crisis\" within \"Cold War\"), and say in one sentence what they cover."
                   : std::string("These documents belong to one topic") + (parent.empty() ? std::string() : " within " + parent) +
                     ". Give the topic a short, specific name of 1 to 4 words, like a folder name "
                     "(for example \"Berlin Conference\", \"Cell Biology\" or \"Macbeth\"), and one sentence saying what they cover.";
    const std::string user = list + ask
        + (avoid.empty() ? std::string() : " Other folders are already called: " + avoid + ". Pick a name that is different from these.")
        + " The name must say the subject itself, never a general word like \"Documents\", \"Notes\" or \"Files\"."
        + "\nAnswer in exactly this form:\nName: ...\nSummary: ...";
    const std::string simpleUser = list + (level == 0 ? "Which school subject are these documents from? Answer with only the subject, for example: History"
                                                        : "What is the one topic these documents share? Answer with only the topic in 1 to 4 words, for example: Cell Biology");
    const std::string reply = simple ? "Name: " + w.chat(system, simpleUser, 16) : w.chat(system, user, 90);
    if (std::getenv("PODAI_TRACE")) std::fprintf(stderr, "writer (level %d): %s\n---\n", level, reply.c_str());
    Named n;
    n.name = field(reply, "Name");
    n.summary = field(reply, "Summary");
    if (n.name.empty()) n.name = trim(reply.substr(0, reply.find('\n')));
    if (n.name.size() > 60) n.name = n.name.substr(0, 60);
    // one sentence is enough
    const size_t stop = n.summary.find(". ");
    if (stop != std::string::npos) n.summary.resize(stop + 1);
    // "History Notes" → "History": a general word on the end is taken off
    for (bool again = true; again;) {
        again = false;
        const size_t sp = n.name.rfind(' ');
        if (sp == std::string::npos) break;
        std::string last = n.name.substr(sp + 1);
        std::transform(last.begin(), last.end(), last.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (const char *v : {"documents", "document", "notes", "files", "folder", "materials", "studies", "work", "topics", "subject"})
            if (last == v) { n.name.resize(sp); again = true; break; }
    }
    // A name that says nothing ("Documents", "Study Notes") is dropped; Pods then names the topic from its key words.
    std::string low = n.name;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char *vague : {"document", "folder", "file", "topic", "notes", "misc", "untitled", "various", "general", "student", "subject"})
        if (low.find(vague) != std::string::npos) { n.name.clear(); n.summary.clear(); break; }
    if (std::count(n.name.begin(), n.name.end(), ' ') > 4) { n.name.clear(); n.summary.clear(); }      // a sentence, not a name
    // the answer form copied back ("Name: ...") instead of filled in
    if (n.name.find("...") != std::string::npos || n.name.find("\xE2\x80\xA6") != std::string::npos || low.rfind("name", 0) == 0) {
        n.name.clear();
        n.summary.clear();
    }
    if (n.summary.find("...") != std::string::npos) n.summary.clear();
    return n;
}

// One more try with a plainer question when the first answer was no use
Named name(Writer &w, const std::vector<DocText> &docs, const std::vector<std::string> &others, int level, const std::string &parent)
{
    Named n = nameOnce(w, docs, others, level, parent, false);
    if (n.name.empty()) {
        const std::string summary = n.summary;
        n = nameOnce(w, docs, others, level, parent, true);
        if (n.summary.empty()) n.summary = summary;
    }
    return n;
}

} // namespace podai
