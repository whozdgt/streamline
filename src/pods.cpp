#include "pods.h"
#include "podsort.h"
#ifdef Q_OS_ANDROID
#include "android.h"
#endif

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui/private/qzipreader_p.h>
#include <algorithm>

namespace {

const int kMaxFiles = 1500;
const int kMaxChars = 200000;          // per document: plenty to tell what it is about
// Claude, for "Sort with AI". The cheapest current model is plenty for naming topics.
const char *kModel = "claude-haiku-5-5";

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

// Files a pod reads. The first four are read in full; the rest are sorted by their name and folder only.
const QStringList &readExts() { static const QStringList l = {"docx", "odt", "txt", "md"}; return l; }
const QStringList &nameExts() { static const QStringList l = {"doc", "rtf", "pdf", "pages", "dotx"}; return l; }
QStringList filters()
{
    QStringList f;
    for (const QString &e : readExts()) f << QStringLiteral("*.") + e;
    for (const QString &e : nameExts()) f << QStringLiteral("*.") + e;
    return f;
}

// Word (.docx): the text is in word/document.xml, one <w:p> per paragraph, the words in <w:t>.
// A paragraph whose style is a heading or the title goes into the headings as well.
void readDocx(const QString &path, QString &title, QString &headings, QString &body)
{
    QZipReader zip(path);
    if (zip.status() != QZipReader::NoError) return;
    {
        QXmlStreamReader x(zip.fileData(QStringLiteral("docProps/core.xml")));
        while (!x.atEnd())
            if (x.readNext() == QXmlStreamReader::StartElement && x.name() == QLatin1String("title"))
                title = x.readElementText().simplified();
    }
    QXmlStreamReader x(zip.fileData(QStringLiteral("word/document.xml")));
    QString para;
    bool heading = false, inText = false;
    while (!x.atEnd() && body.size() < kMaxChars) {
        switch (x.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto n = x.name();
            if (n == QLatin1String("p")) { para.clear(); heading = false; }
            else if (n == QLatin1String("t")) inText = true;
            else if (n == QLatin1String("tab")) para += QLatin1Char(' ');
            else if (n == QLatin1String("br")) para += QLatin1Char('\n');
            else if (n == QLatin1String("pStyle")) {
                for (const auto &a : x.attributes())
                    if (a.name() == QLatin1String("val")) {
                        const QString v = a.value().toString();
                        if (v.contains(QLatin1String("Heading"), Qt::CaseInsensitive) || v.contains(QLatin1String("Title"), Qt::CaseInsensitive)) heading = true;
                    }
            }
            break;
        }
        case QXmlStreamReader::Characters:
            if (inText) para += x.text();
            break;
        case QXmlStreamReader::EndElement:
            if (x.name() == QLatin1String("t")) inText = false;
            else if (x.name() == QLatin1String("p")) {
                const QString s = para.trimmed();
                if (!s.isEmpty()) {
                    body += s + QLatin1Char('\n');
                    if (heading) headings += s + QLatin1Char('\n');
                }
            }
            break;
        default:
            break;
        }
    }
}

// OpenDocument (.odt): content.xml, <text:p> paragraphs and <text:h> headings.
void readOdt(const QString &path, QString &headings, QString &body)
{
    QZipReader zip(path);
    if (zip.status() != QZipReader::NoError) return;
    QXmlStreamReader x(zip.fileData(QStringLiteral("content.xml")));
    QString para;
    int depth = 0;
    bool heading = false;
    while (!x.atEnd() && body.size() < kMaxChars) {
        switch (x.readNext()) {
        case QXmlStreamReader::StartElement:
            if (x.name() == QLatin1String("p") || x.name() == QLatin1String("h")) {
                if (depth++ == 0) { para.clear(); heading = x.name() == QLatin1String("h"); }
            } else if (depth > 0 && (x.name() == QLatin1String("s") || x.name() == QLatin1String("tab"))) {
                para += QLatin1Char(' ');
            } else if (depth > 0 && x.name() == QLatin1String("line-break")) {
                para += QLatin1Char('\n');
            }
            break;
        case QXmlStreamReader::Characters:
            if (depth > 0) para += x.text();
            break;
        case QXmlStreamReader::EndElement:
            if ((x.name() == QLatin1String("p") || x.name() == QLatin1String("h")) && depth > 0 && --depth == 0) {
                const QString s = para.trimmed();
                if (!s.isEmpty()) {
                    body += s + QLatin1Char('\n');
                    if (heading) headings += s + QLatin1Char('\n');
                }
            }
            break;
        default:
            break;
        }
    }
}

Pods::Scanned readOne(const QString &path, const QString &root)
{
    Pods::Scanned s;
    const QFileInfo fi(path);
    s.path = fi.absoluteFilePath();
    s.name = fi.completeBaseName();
    s.ext = fi.suffix().toLower();
    s.modified = fi.lastModified().toMSecsSinceEpoch();
    s.rel = QDir(root).relativeFilePath(fi.absolutePath());
    if (s.rel == QLatin1String(".")) s.rel.clear();
    if (s.ext == QLatin1String("docx")) readDocx(s.path, s.title, s.headings, s.body);
    else if (s.ext == QLatin1String("odt")) readOdt(s.path, s.headings, s.body);
    else if (s.ext == QLatin1String("txt") || s.ext == QLatin1String("md")) {
        QFile f(s.path);
        if (f.open(QIODevice::ReadOnly)) s.body = QString::fromUtf8(f.read(kMaxChars));
        if (s.ext == QLatin1String("md")) {
            static const QRegularExpression h(QStringLiteral("^#+\\s*(.+)$"), QRegularExpression::MultilineOption);
            auto it = h.globalMatch(s.body);
            while (it.hasNext()) s.headings += it.next().captured(1) + QLatin1Char('\n');
        }
    }
    s.body.truncate(kMaxChars);
    s.readable = !s.body.trimmed().isEmpty();
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    s.words = s.readable ? int(s.body.split(ws, Qt::SkipEmptyParts).size()) : 0;
    return s;
}

QVector<Pods::Scanned> scanFolder(const QString &root)
{
    QVector<Pods::Scanned> out;
    QDirIterator it(root, filters(), QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    QStringList paths;
    while (it.hasNext() && paths.size() < kMaxFiles) {
        const QString p = it.next();
        const QString n = it.fileName();
        if (n.startsWith(QLatin1String("~$")) || n.startsWith(QLatin1Char('.'))) continue;    // Word's lock files
        if (p.contains(QLatin1String("/."))) continue;                                       // hidden folders
        paths << p;
    }
    std::sort(paths.begin(), paths.end());
    for (const QString &p : paths) out.push_back(readOne(p, root));
    return out;
}

std::vector<podsort::Doc> toSortDocs(const QVector<Pods::Scanned> &docs)
{
    std::vector<podsort::Doc> v;
    v.reserve(size_t(docs.size()));
    for (const auto &d : docs)
        v.push_back({(d.title.isEmpty() || d.title == d.name ? d.name : d.name + QLatin1Char(' ') + d.title).toStdString(),
                     d.headings.toStdString(), d.body.toStdString(), d.rel.toStdString()});
    return v;
}

QString snippetOf(const QString &body)
{
    QString s = body.simplified();
    if (s.size() > 220) s = s.left(220).trimmed() + QStringLiteral("…");
    return s;
}

// A topic's name as a folder name: no characters Windows forbids, not too long.
QString folderName(const QString &topic)
{
    QString s = topic;
    static const QRegularExpression bad(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    s.replace(bad, QStringLiteral("-"));
    s = s.simplified();
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))) s.chop(1);
    if (s.size() > 60) s = s.left(60).trimmed();
    return s.isEmpty() ? QStringLiteral("Topic") : s;
}

QString mimeOf(const QString &ext)
{
    if (ext == QLatin1String("docx")) return QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    if (ext == QLatin1String("doc")) return QStringLiteral("application/msword");
    if (ext == QLatin1String("odt")) return QStringLiteral("application/vnd.oasis.opendocument.text");
    if (ext == QLatin1String("pdf")) return QStringLiteral("application/pdf");
    if (ext == QLatin1String("rtf")) return QStringLiteral("application/rtf");
    return QStringLiteral("text/plain");
}

struct Result {
    QVector<Pods::Scanned> docs;
    std::vector<podsort::Topic> topics;   // sorting
    std::vector<int> placed;              // refreshing: the topic each new document joins, -1 for none
    std::vector<int> fresh;               // refreshing: which documents are new
};

} // namespace

Pods::Pods(QObject *parent) : QObject(parent)
{
    load();
    m_key = QSettings().value(QStringLiteral("pods/apiKey")).toString();
}

Pods::Pod *Pods::find(const QString &id)
{
    for (Pod &p : m_pods) if (p.id == id) return &p;
    return nullptr;
}
const Pods::Pod *Pods::find(const QString &id) const
{
    for (const Pod &p : m_pods) if (p.id == id) return &p;
    return nullptr;
}

QVariantList Pods::pods() const
{
    QVariantList out;
    for (const Pod &p : m_pods) {
        int topics = 0;
        for (const Topic &t : p.topics) if (!t.other) ++topics;
        out << QVariantMap{{"id", p.id}, {"name", p.name}, {"folder", QDir::toNativeSeparators(p.folder)},
                           {"docs", int(p.docs.size())}, {"topics", topics}, {"busy", p.busy}, {"status", p.status},
                           {"error", p.error}};
    }
    return out;
}

QVariantMap Pods::pod(const QString &id, const QString &query) const
{
    const Pod *p = find(id);
    if (!p) return {};
    const QString q = query.trimmed();
    QVariantList topics;
    int shown = 0;
    for (const Topic &t : p->topics) {
        QVariantList docs;
        const bool topicHit = !q.isEmpty() && t.name.contains(q, Qt::CaseInsensitive);
        for (const Doc &d : p->docs) {
            if (d.topic != t.id) continue;
            if (!q.isEmpty() && !topicHit && !d.name.contains(q, Qt::CaseInsensitive) && !d.snippet.contains(q, Qt::CaseInsensitive)) continue;
            docs << QVariantMap{{"path", d.path}, {"name", d.name}, {"ext", d.ext}, {"snippet", d.snippet},
                                {"words", d.words}, {"readable", d.readable}, {"folder", d.folder}};
        }
        if (docs.isEmpty() && (!q.isEmpty() || t.other)) continue;
        shown += int(docs.size());
        topics << QVariantMap{{"id", t.id}, {"name", t.name}, {"summary", t.summary}, {"keywords", t.keywords},
                              {"other", t.other}, {"docs", docs}};
    }
    QString when;
    if (p->sorted > 0) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(p->sorted);
        when = dt.date() == QDate::currentDate() ? dt.toString(QStringLiteral("HH:mm")) : dt.toString(QStringLiteral("d MMM"));
    }
    const QStringList details = {QStringLiteral("broad"), QStringLiteral("balanced"), QStringLiteral("specific")};
    const QString sortedLabel = p->sorted == 0 ? QString()
        : (p->method == QLatin1String("ai") ? QStringLiteral("Sorted with Claude") : QStringLiteral("Sorted on this device"))
              + QStringLiteral(" · ") + details.value(p->detail) + QStringLiteral(" · ") + when;
    return {{"id", p->id}, {"name", p->name}, {"folder", QDir::toNativeSeparators(p->folder)}, {"detail", p->detail},
            {"method", p->method}, {"sortedLabel", sortedLabel}, {"busy", p->busy}, {"status", p->status},
            {"error", p->error}, {"moved", !p->moves.isEmpty()}, {"docCount", int(p->docs.size())},
            {"shown", shown}, {"topics", topics}};
}

QVariantList Pods::topicChoices(const QString &podId) const
{
    QVariantList out;
    const Pod *p = find(podId);
    if (!p) return out;
    for (const Topic &t : p->topics) out << QVariantMap{{"id", t.id}, {"name", t.name}};
    out << QVariantMap{{"id", "new"}, {"name", "+ New topic"}};
    return out;
}

QString Pods::create(const QString &folder)
{
    const QFileInfo fi(folder);
    if (!fi.isDir()) return {};
    for (const Pod &e : std::as_const(m_pods))      // that folder already has a pod: open it instead of making a second
        if (QFileInfo(e.folder) == fi) return e.id;
    Pod p;
    p.id = newId();
    p.folder = fi.absoluteFilePath();
    p.name = fi.fileName().isEmpty() ? QStringLiteral("My documents") : fi.fileName();
    p.created = now();
    m_pods.prepend(p);
    changed();
    run(p.id, QStringLiteral("Reading your documents…"), 1, 0);
    return p.id;
}

void Pods::remove(const QString &id)
{
    for (int i = 0; i < m_pods.size(); ++i)
        if (m_pods[i].id == id) { m_pods.remove(i); changed(); return; }
}

void Pods::rename(const QString &id, const QString &name)
{
    Pod *p = find(id);
    const QString n = name.simplified();
    if (!p || n.isEmpty() || n == p->name) return;
    p->name = n;
    changed();
}

void Pods::resort(const QString &id, int detail) { run(id, QStringLiteral("Sorting…"), qBound(0, detail, 2), 0); }
void Pods::refresh(const QString &id) { if (const Pod *p = find(id)) run(id, QStringLiteral("Looking for new documents…"), p->detail, 1); }
void Pods::sortWithAi(const QString &id, int detail)
{
    if (m_key.isEmpty()) {
        if (Pod *p = find(id)) { p->error = QStringLiteral("Add your Anthropic API key first."); ++m_rev; emit revisionChanged(); }
        return;
    }
    run(id, QStringLiteral("Reading your documents…"), qBound(0, detail, 2), 2);
}

void Pods::run(const QString &id, const QString &status, int detail, int kind)
{
    Pod *p = find(id);
    if (!p || p->busy) return;
    p->busy = true;
    p->status = status;
    p->error.clear();
    ++m_rev;
    emit revisionChanged();

    // what the pod knows now, for a refresh: which topic each file is in
    QHash<QString, QString> known;
    QStringList topicIds;
    for (const Doc &d : p->docs) known.insert(d.path, d.topic);
    for (const Topic &t : p->topics) if (!t.other) topicIds << t.id;
    const QString folder = p->folder;

    auto *w = new QFutureWatcher<Result>(this);
    connect(w, &QFutureWatcher<Result>::finished, this, [this, w, id, detail, kind]() {
        const Result r = w->result();
        w->deleteLater();
        Pod *p = find(id);
        if (!p) return;
        if (kind == 2) { askAi(id, r.docs, detail); return; }
        if (r.docs.isEmpty()) { finish(p, QStringLiteral("No documents found in this folder (Word, OpenDocument, text or PDF).")); return; }
        if (kind == 0) {
            p->topics.clear();
            p->docs.clear();
            for (const auto &t : r.topics) {
                Topic tp;
                tp.id = newId();
                tp.name = QString::fromStdString(t.name);
                for (const auto &k : t.keywords) tp.keywords << QString::fromStdString(k);
                tp.other = t.other;
                p->topics.push_back(tp);
                for (int i : t.docs) {
                    const Scanned &s = r.docs[i];
                    p->docs.push_back({s.path, s.name, s.ext, s.rel, tp.id, snippetOf(s.body), s.words, s.readable});
                }
            }
            p->detail = detail;
            p->method = QStringLiteral("device");
            p->sorted = now();
        } else {
            QHash<QString, QString> before;
            for (const Doc &d : p->docs) before.insert(d.path, d.topic);
            QStringList topicIds;
            for (const Topic &t : p->topics) if (!t.other) topicIds << t.id;
            QVector<Doc> docs;
            for (int i = 0; i < r.docs.size(); ++i) {
                const Scanned &s = r.docs[i];
                QString topic = before.value(s.path);
                if (topic.isEmpty()) {
                    const auto f = std::find(r.fresh.begin(), r.fresh.end(), i);
                    const int k = f == r.fresh.end() ? -1 : r.placed[size_t(f - r.fresh.begin())];
                    topic = k >= 0 && k < topicIds.size() ? topicIds[k] : otherTopic(*p).id;
                }
                docs.push_back({s.path, s.name, s.ext, s.rel, topic, snippetOf(s.body), s.words, s.readable});
            }
            p->docs = docs;
            dropEmptyTopics(*p);
        }
        finish(p);
    });
    w->setFuture(QtConcurrent::run([folder, kind, detail, known, topicIds]() {
        Result r;
        r.docs = scanFolder(folder);
        if (kind == 0 && !r.docs.isEmpty()) {
            r.topics = podsort::sortDocs(toSortDocs(r.docs), detail);
        } else if (kind == 1 && !r.docs.isEmpty()) {
            std::vector<std::vector<int>> groups(size_t(topicIds.size()));
            for (int i = 0; i < r.docs.size(); ++i) {
                const auto it = known.constFind(r.docs[i].path);
                if (it == known.constEnd()) { r.fresh.push_back(i); continue; }
                const qsizetype k = topicIds.indexOf(*it);
                if (k >= 0) groups[size_t(k)].push_back(i);
            }
            if (!r.fresh.empty()) r.placed = podsort::placeNew(toSortDocs(r.docs), groups, r.fresh, detail);
        }
        return r;
    }));
}

void Pods::finish(Pod *p, const QString &error)
{
    p->busy = false;
    p->status.clear();
    p->error = error;
    changed();
}

Pods::Topic &Pods::otherTopic(Pod &p)
{
    for (Topic &t : p.topics) if (t.other) return t;
    Topic t;
    t.id = newId();
    t.name = QStringLiteral("Other");
    t.other = true;
    p.topics.push_back(t);
    return p.topics.back();
}

void Pods::dropEmptyTopics(Pod &p)
{
    QSet<QString> used;
    for (const Doc &d : p.docs) used.insert(d.topic);
    for (int i = p.topics.size() - 1; i >= 0; --i)
        if (!used.contains(p.topics[i].id)) p.topics.remove(i);
}

// ---- Sorting with Claude ----

void Pods::askAi(const QString &id, const QVector<Scanned> &docs, int detail)
{
    Pod *p = find(id);
    if (!p) return;
    if (docs.isEmpty()) { finish(p, QStringLiteral("No documents found in this folder.")); return; }
    p->status = QStringLiteral("Asking Claude to sort %1 documents…").arg(docs.size());
    ++m_rev;
    emit revisionChanged();

    // Each document gets a share of about 120,000 characters: its name, folder, headings and the start of its text.
    const int each = qBound(300, 120000 / int(docs.size()), 2500);
    QString list;
    for (int i = 0; i < docs.size(); ++i) {
        const Scanned &d = docs[i];
        list += QStringLiteral("[%1] %2.%3").arg(i).arg(d.name, d.ext);
        if (!d.rel.isEmpty()) list += QStringLiteral(" (folder: %1)").arg(d.rel);
        list += QLatin1Char('\n');
        if (!d.headings.isEmpty()) list += QStringLiteral("Headings: ") + d.headings.left(400).simplified() + QLatin1Char('\n');
        if (d.readable) list += QStringLiteral("Text: ") + d.body.left(each).simplified() + QLatin1Char('\n');
        else list += QStringLiteral("(text not readable; sort by its name)\n");
        list += QLatin1Char('\n');
    }
    const QString how = detail == 0 ? QStringLiteral("Use a few broad topics, roughly one per subject.")
                       : detail == 2 ? QStringLiteral("Use narrow, specific topics: split a subject into its separate topics.")
                                     : QStringLiteral("Use topics about as specific as a unit or chapter of a course.");
    const QString prompt = QStringLiteral(
        "These are %1 documents from one folder of someone's files, often school or study notes. Group them into topics "
        "by what they are about. %2 Topic names are short (1 to 4 words) and specific, such as \"Berlin Conference\" or "
        "\"Cell Biology\". Every document goes in exactly one topic. Documents that fit no topic go in a topic named "
        "\"Other\".\n\nReply with JSON only, no other text, in this form:\n"
        "{\"topics\": [{\"name\": \"…\", \"summary\": \"one short sentence on what these documents cover\", \"docs\": [0, 4, 7]}]}\n\n"
        "The documents:\n\n%3").arg(docs.size()).arg(how, list);

    QJsonObject body{{"model", QString::fromLatin1(kModel)}, {"max_tokens", 8000},
                     {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}}}};
    QNetworkRequest req(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key", m_key.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setTransferTimeout(180000);
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, docs, detail]() {
        reply->deleteLater();
        Pod *p = find(id);
        if (!p) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject res = QJsonDocument::fromJson(reply->readAll()).object();
        if (code == 401) { finish(p, QStringLiteral("Your API key wasn't accepted. Check it under Pods › Sort with Claude.")); return; }
        if (code != 200) {
            const QString msg = res.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
            finish(p, QStringLiteral("Claude couldn't sort this pod: ") + (msg.isEmpty() ? reply->errorString() : msg));
            return;
        }
        QString text;
        for (const QJsonValue &c : res.value(QLatin1String("content")).toArray())
            if (c.toObject().value(QLatin1String("type")).toString() == QLatin1String("text")) text += c.toObject().value(QLatin1String("text")).toString();
        const qsizetype a = text.indexOf(QLatin1Char('{')), b = text.lastIndexOf(QLatin1Char('}'));
        const QJsonArray topics = a >= 0 && b > a
            ? QJsonDocument::fromJson(text.mid(a, b - a + 1).toUtf8()).object().value(QLatin1String("topics")).toArray()
            : QJsonArray();
        if (topics.isEmpty()) { finish(p, QStringLiteral("Claude's answer couldn't be read. Try again, or sort on this device.")); return; }

        p->topics.clear();
        p->docs.clear();
        QVector<bool> placed(docs.size(), false);
        for (const QJsonValue &tv : topics) {
            const QJsonObject t = tv.toObject();
            Topic tp;
            tp.id = newId();
            tp.name = t.value(QLatin1String("name")).toString().simplified().left(60);
            tp.summary = t.value(QLatin1String("summary")).toString().simplified();
            tp.other = tp.name.compare(QLatin1String("Other"), Qt::CaseInsensitive) == 0;
            if (tp.name.isEmpty()) tp.name = QStringLiteral("Untitled topic");
            bool any = false;
            for (const QJsonValue &dv : t.value(QLatin1String("docs")).toArray()) {
                const int i = dv.toInt(-1);
                if (i < 0 || i >= docs.size() || placed[i]) continue;
                placed[i] = true;
                any = true;
                const Scanned &s = docs[i];
                p->docs.push_back({s.path, s.name, s.ext, s.rel, tp.id, snippetOf(s.body), s.words, s.readable});
            }
            if (any) p->topics.push_back(tp);
        }
        for (int i = 0; i < docs.size(); ++i)      // anything Claude left out goes in Other
            if (!placed[i]) {
                const Scanned &s = docs[i];
                p->docs.push_back({s.path, s.name, s.ext, s.rel, otherTopic(*p).id, snippetOf(s.body), s.words, s.readable});
            }
        std::stable_sort(p->topics.begin(), p->topics.end(), [](const Topic &x, const Topic &y) { return !x.other && y.other; });
        p->detail = detail;
        p->method = QStringLiteral("ai");
        p->sorted = now();
        finish(p);
    });
}

// ---- Changing topics by hand ----

void Pods::renameTopic(const QString &podId, const QString &topicId, const QString &name)
{
    Pod *p = find(podId);
    const QString n = name.simplified().left(60);
    if (!p || n.isEmpty()) return;
    for (Topic &t : p->topics)
        if (t.id == topicId) { t.name = n; t.other = false; changed(); return; }
}

void Pods::moveDoc(const QString &podId, const QString &path, const QString &topicId)
{
    Pod *p = find(podId);
    if (!p) return;
    QString target = topicId;
    if (topicId == QLatin1String("new")) {
        Topic t;
        t.id = newId();
        t.name = QStringLiteral("New topic");
        auto at = std::find_if(p->topics.begin(), p->topics.end(), [](const Topic &x) { return x.other; });
        p->topics.insert(at, t);
        target = t.id;
    }
    for (Doc &d : p->docs)
        if (d.path == path) { d.topic = target; break; }
    dropEmptyTopics(*p);
    changed();
}

// ---- Moving the real files ----

QVariantMap Pods::movePlan(const QString &id) const
{
    const Pod *p = find(id);
    if (!p) return {};
    int files = 0;
    QStringList names;
    for (const Topic &t : p->topics) {
        if (t.other) continue;
        const QString dir = QDir(p->folder).filePath(folderName(t.name));
        bool any = false;
        for (const Doc &d : p->docs)
            if (d.topic == t.id && QFileInfo(d.path).absolutePath() != dir) { ++files; any = true; }
        if (any) names << folderName(t.name);
    }
    return {{"files", files}, {"folders", int(names.size())}, {"names", names}};
}

QVariantMap Pods::moveFiles(const QString &id)
{
    Pod *p = find(id);
    if (!p || p->busy) return {};
    int moved = 0, failed = 0;
    QDir root(p->folder);
    QVector<Move> moves;
    for (const Topic &t : p->topics) {
        if (t.other) continue;       // documents that fit nowhere stay where they are
        const QString name = folderName(t.name);
        const QString dir = root.filePath(name);
        for (Doc &d : p->docs) {
            if (d.topic != t.id || QFileInfo(d.path).absolutePath() == dir) continue;
            if (!QFileInfo::exists(dir)) {
                if (!root.mkpath(name)) { ++failed; continue; }
                p->madeDirs << dir;
            }
            const QFileInfo fi(d.path);
            QString to = QDir(dir).filePath(fi.fileName());
            for (int n = 2; QFileInfo::exists(to); ++n)
                to = QDir(dir).filePath(QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
            if (QFile::rename(d.path, to)) {
                moves.push_back({d.path, to});
                d.path = to;
                d.folder = name;
                ++moved;
            } else {
                ++failed;      // open in Word, or not ours to move
            }
        }
    }
    p->moves += moves;
    changed();
    return {{"moved", moved}, {"failed", failed}};
}

QVariantMap Pods::undoMove(const QString &id)
{
    Pod *p = find(id);
    if (!p || p->busy) return {};
    int moved = 0, failed = 0;
    QVector<Move> left;
    for (int i = p->moves.size() - 1; i >= 0; --i) {
        const Move &m = p->moves[i];
        QDir().mkpath(QFileInfo(m.from).absolutePath());
        if (!QFileInfo::exists(m.from) && QFile::rename(m.to, m.from)) {
            ++moved;
            for (Doc &d : p->docs)
                if (d.path == m.to) {
                    d.path = m.from;
                    d.folder = QDir(p->folder).relativeFilePath(QFileInfo(m.from).absolutePath());
                    if (d.folder == QLatin1String(".")) d.folder.clear();
                }
        } else {
            ++failed;
            left.prepend(m);
        }
    }
    p->moves = left;
    QStringList kept;
    for (const QString &dir : std::as_const(p->madeDirs))
        if (!QDir().rmdir(dir) && QFileInfo::exists(dir)) kept << dir;      // only removed when empty
    p->madeDirs = left.isEmpty() ? QStringList() : kept;
    changed();
    return {{"moved", moved}, {"failed", failed}};
}

// ---- Documents ----

QString Pods::docText(const QString &path) const
{
    const Scanned s = readOne(path, QFileInfo(path).absolutePath());
    return s.body.left(20000);
}

bool Pods::openDoc(const QString &path) const
{
#ifdef Q_OS_ANDROID
    return android::openFile(path, mimeOf(QFileInfo(path).suffix().toLower()));
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

int Pods::docCount(const QString &dir) const
{
    return int(QDir(dir).entryList(filters(), QDir::Files | QDir::Readable, QDir::NoSort).size());
}

void Pods::setKey(const QString &key)
{
    m_key = key.trimmed();
    QSettings st;
    if (m_key.isEmpty()) st.remove(QStringLiteral("pods/apiKey"));
    else st.setValue(QStringLiteral("pods/apiKey"), m_key);
    ++m_rev;
    emit revisionChanged();
}

QString Pods::keyHint() const
{
    if (m_key.size() < 12) return m_key.isEmpty() ? QString() : QStringLiteral("•••");
    return m_key.left(7) + QStringLiteral("…") + m_key.right(4);
}

bool Pods::filesAllowed() const
{
#ifdef Q_OS_ANDROID
    return android::filesAllowed();
#else
    return true;
#endif
}

void Pods::askFiles() const
{
#ifdef Q_OS_ANDROID
    android::askFiles();
#endif
}

// ---- Saving ----

QString Pods::filePath() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + QStringLiteral("/pods.json");
}

void Pods::load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const QJsonValue &pv : QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("pods")).toArray()) {
        const QJsonObject o = pv.toObject();
        Pod p;
        p.id = o.value(QLatin1String("id")).toString();
        p.name = o.value(QLatin1String("name")).toString();
        p.folder = o.value(QLatin1String("folder")).toString();
        p.method = o.value(QLatin1String("method")).toString(QStringLiteral("device"));
        p.detail = o.value(QLatin1String("detail")).toInt(1);
        p.created = qint64(o.value(QLatin1String("created")).toDouble());
        p.sorted = qint64(o.value(QLatin1String("sorted")).toDouble());
        if (p.id.isEmpty()) continue;
        for (const QJsonValue &tv : o.value(QLatin1String("topics")).toArray()) {
            const QJsonObject t = tv.toObject();
            Topic tp;
            tp.id = t.value(QLatin1String("id")).toString();
            tp.name = t.value(QLatin1String("name")).toString();
            tp.summary = t.value(QLatin1String("summary")).toString();
            for (const QJsonValue &k : t.value(QLatin1String("keywords")).toArray()) tp.keywords << k.toString();
            tp.other = t.value(QLatin1String("other")).toBool();
            p.topics.push_back(tp);
        }
        for (const QJsonValue &dv : o.value(QLatin1String("docs")).toArray()) {
            const QJsonObject d = dv.toObject();
            p.docs.push_back({d.value(QLatin1String("path")).toString(), d.value(QLatin1String("name")).toString(),
                              d.value(QLatin1String("ext")).toString(), d.value(QLatin1String("folder")).toString(),
                              d.value(QLatin1String("topic")).toString(), d.value(QLatin1String("snippet")).toString(),
                              d.value(QLatin1String("words")).toInt(), d.value(QLatin1String("readable")).toBool()});
        }
        for (const QJsonValue &mv : o.value(QLatin1String("moves")).toArray()) {
            const QJsonObject m = mv.toObject();
            p.moves.push_back({m.value(QLatin1String("from")).toString(), m.value(QLatin1String("to")).toString()});
        }
        for (const QJsonValue &v : o.value(QLatin1String("madeDirs")).toArray()) p.madeDirs << v.toString();
        m_pods.push_back(p);
    }
}

void Pods::save() const
{
    QJsonArray arr;
    for (const Pod &p : m_pods) {
        QJsonArray topics, docs, moves;
        for (const Topic &t : p.topics)
            topics << QJsonObject{{"id", t.id}, {"name", t.name}, {"summary", t.summary},
                                  {"keywords", QJsonArray::fromStringList(t.keywords)}, {"other", t.other}};
        for (const Doc &d : p.docs)
            docs << QJsonObject{{"path", d.path}, {"name", d.name}, {"ext", d.ext}, {"folder", d.folder}, {"topic", d.topic},
                                {"snippet", d.snippet}, {"words", d.words}, {"readable", d.readable}};
        for (const Move &m : p.moves) moves << QJsonObject{{"from", m.from}, {"to", m.to}};
        arr << QJsonObject{{"id", p.id}, {"name", p.name}, {"folder", p.folder}, {"method", p.method}, {"detail", p.detail},
                           {"created", double(p.created)}, {"sorted", double(p.sorted)}, {"topics", topics}, {"docs", docs},
                           {"moves", moves}, {"madeDirs", QJsonArray::fromStringList(p.madeDirs)}};
    }
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"pods", arr}}).toJson(QJsonDocument::Compact));
    f.commit();
}
