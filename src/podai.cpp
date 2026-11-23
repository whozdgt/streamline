#include "podai.h"

#include "llama.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
    std::vector<float> sum;
    for (const std::string &p : parts) {
        const std::vector<float> e = embed(p);
        if (e.empty()) continue;
        if (sum.empty()) sum.assign(e.size(), 0.f);
        for (size_t i = 0; i < e.size(); ++i) sum[i] += e[i];
    }
    normalize(sum);
    return sum;
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

std::vector<std::vector<int>> group(const std::vector<std::vector<float>> &vecs, int detail)
{
    const size_t n = vecs.size();
    std::vector<std::vector<int>> groups(n);
    for (size_t i = 0; i < n; ++i) groups[i] = {int(i)};
    if (n < 2) return groups;

    std::vector<std::vector<double>> sim(n, std::vector<double>(n, 0.0));
    std::vector<double> all;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            double s = 0;
            if (!vecs[i].empty() && vecs[i].size() == vecs[j].size())
                for (size_t k = 0; k < vecs[i].size(); ++k) s += double(vecs[i][k]) * vecs[j][k];
            sim[i][j] = sim[j][i] = s;
            all.push_back(s);
        }
    // Meaning models never score two texts near zero: even unrelated ones come out fairly similar. So the cut-off is
    // set from this pod's own scores: how far above a typical pair two documents must be to share a topic.
    std::vector<double> sorted = all;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    const double high = sorted[std::min(sorted.size() - 1, size_t(double(sorted.size()) * 0.98))];
    const double reach = detail <= 0 ? 0.30 : detail == 1 ? 0.45 : 0.60;      // fraction of the way from typical to closest
    const double cutoff = median + (high - median) * reach;

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

std::vector<int> place(const std::vector<std::vector<float>> &vecs, const std::vector<std::vector<int>> &topics,
                       const std::vector<int> &fresh, int detail)
{
    std::vector<int> out(fresh.size(), -1);
    const size_t n = vecs.size();
    if (n < 2) return out;
    auto dot = [&](size_t i, size_t j) {
        if (vecs[i].empty() || vecs[i].size() != vecs[j].size()) return 0.0;
        double s = 0;
        for (size_t k = 0; k < vecs[i].size(); ++k) s += double(vecs[i][k]) * vecs[j][k];
        return s;
    };
    std::vector<double> all;
    for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) all.push_back(dot(i, j));
    std::sort(all.begin(), all.end());
    const double median = all[all.size() / 2];
    const double high = all[std::min(all.size() - 1, size_t(double(all.size()) * 0.98))];
    const double reach = detail <= 0 ? 0.30 : detail == 1 ? 0.45 : 0.60;
    const double cutoff = median + (high - median) * reach;
    for (size_t f = 0; f < fresh.size(); ++f) {
        double best = cutoff;
        for (size_t t = 0; t < topics.size(); ++t) {
            if (topics[t].empty()) continue;
            double s = 0;
            for (int d : topics[t]) s += dot(size_t(fresh[f]), size_t(d));
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

Named name(Writer &w, const std::vector<DocText> &docs, const std::vector<std::string> &others)
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
    const std::string user = list +
        "These documents belong to one topic. Give the topic a short, specific name of 1 to 4 words, like a folder name "
        "(for example \"Berlin Conference\", \"Cell Biology\" or \"Macbeth\"), and one sentence saying what they cover."
        + (avoid.empty() ? std::string() : " Other folders are already called: " + avoid + ". Pick a name that is different from these.")
        + " The name must say the subject itself, never a general word like \"Documents\", \"Notes\" or \"Files\"."
        + "\nAnswer in exactly this form:\nName: ...\nSummary: ...";
    const std::string reply = w.chat(system, user, 90);
    Named n;
    n.name = field(reply, "Name");
    n.summary = field(reply, "Summary");
    if (n.name.empty()) n.name = trim(reply.substr(0, reply.find('\n')));
    if (n.name.size() > 60) n.name = n.name.substr(0, 60);
    // one sentence is enough
    const size_t stop = n.summary.find(". ");
    if (stop != std::string::npos) n.summary.resize(stop + 1);
    // A name that says nothing ("Documents", "Study Notes") is dropped; Pods then names the topic from its key words.
    std::string low = n.name;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char *vague : {"document", "folder", "file", "topic", "notes", "misc", "untitled", "various", "general", "student"})
        if (low.find(vague) != std::string::npos) { n.name.clear(); n.summary.clear(); break; }
    if (std::count(n.name.begin(), n.name.end(), ' ') > 4) { n.name.clear(); n.summary.clear(); }      // a sentence, not a name
    return n;
}

} // namespace podai
