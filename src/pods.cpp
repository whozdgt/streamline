#include "pods.h"
#include "podsort.h"
#include "aimodels.h"
#include "podai.h"
#include "pdftext.h"
#include "specread.h"
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
#include <QRegularExpression>
#include <QBuffer>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>
// Qt's zip reader: part of Qt Gui up to 6.10 or so, Qt Core in newer versions
#if __has_include(<QtCore/private/qzipreader_p.h>)
#include <QtCore/private/qzipreader_p.h>
#else
#include <QtGui/private/qzipreader_p.h>
#endif
#include <algorithm>
#include <cmath>
#include <map>
#include <functional>

namespace {

const int kMaxFiles = 1500;
const int kMaxChars = 200000;          // per document: plenty to tell what it is about

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

// Files a pod reads. The first five are read; the rest are sorted by their name and folder only.
const QStringList &readExts() { static const QStringList l = {"docx", "odt", "pdf", "txt", "md"}; return l; }
const QStringList &nameExts() { static const QStringList l = {"doc", "rtf", "pages", "dotx"}; return l; }
QStringList filters()
{
    QStringList f;
    for (const QString &e : readExts()) f << QStringLiteral("*.") + e;
    for (const QString &e : nameExts()) f << QStringLiteral("*.") + e;
    return f;
}

// Word (.docx): the text is in word/document.xml, one <w:p> per paragraph, the words in <w:t>.
// A paragraph whose style is a heading or the title goes into the headings as well.
void readDocxZip(QZipReader &zip, QString &title, QString &headings, QString &body)
{
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
void readOdtZip(QZipReader &zip, QString &headings, QString &body)
{
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

void readDocx(const QString &path, QString &title, QString &headings, QString &body)
{
    QZipReader zip(path);
    readDocxZip(zip, title, headings, body);
}
void readOdt(const QString &path, QString &headings, QString &body)
{
    QZipReader zip(path);
    readOdtZip(zip, headings, body);
}

// PDF: the text of its first pages (a long book's opening chapters say what it is about), and the lines set larger
// than the rest as its headings. Scanned pages (pictures of text) and locked PDFs have no text to give.
const qint64 kMaxPdfBytes = 80LL * 1024 * 1024;
void readPdf(const QString &path, QString &headings, QString &body)
{
    QFile f(path);
    if (f.size() > kMaxPdfBytes || !f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    const auto doc = pdftext::read(std::string(data.constData(), size_t(data.size())), 120);
    if (!doc.error.empty()) return;
    body = QString::fromStdString(pdftext::text(doc));
    headings = QString::fromStdString(pdftext::headings(doc)).left(20000);
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
    else if (s.ext == QLatin1String("pdf")) readPdf(s.path, s.headings, s.body);
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
    std::vector<podsort::Node> nodes;     // sorting: the topics, parents before children
    QString method = QStringLiteral("words");   // words | ai | ai-writer
    QString note;                         // something went wrong with the AI and key words were used instead
    std::vector<int> placed;              // refreshing: the topic each new document joins, -1 for none
    std::vector<int> fresh;               // refreshing: which documents are new
    std::vector<bool> check;              // spec: documents whose point Streamline isn't sure of
    bool spec = false;                    // matched to a spec: docPoint says where each document goes
    std::vector<int> docPoint, second;    // spec: each document's point and the next closest (-1 for none)
};

std::vector<podai::DocText> docTexts(const QVector<Pods::Scanned> &docs, const std::vector<int> &which)
{
    std::vector<podai::DocText> v;
    for (int i : which) {
        const auto &d = docs[i];
        v.push_back({(d.title.isEmpty() || d.title == d.name ? d.name : d.name + QStringLiteral(": ") + d.title).toStdString(),
                     d.headings.toStdString(), d.body.toStdString()});
    }
    return v;
}

} // namespace

Pods::Pods(AiModels *models, QObject *parent) : QObject(parent), m_models(models)
{
    load();
    podai::quiet();
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
        int topics = 0, check = 0;
        for (const Topic &t : p.topics) if (!t.other) ++topics;
        for (const Doc &d : p.docs) if (d.check) ++check;
        out << QVariantMap{{"id", p.id}, {"name", p.name}, {"folder", QDir::toNativeSeparators(p.folder)},
                           {"docs", int(p.docs.size())}, {"topics", topics}, {"busy", p.busy}, {"status", p.status},
                           {"error", p.error}, {"colour", p.colour}, {"check", check}, {"layout", p.layout},
                           {"specName", p.specName}};
    }
    return out;
}

QVector<const Pods::Topic *> Pods::ordered(const Pod &p) const
{
    QVector<const Topic *> out;
    std::function<void(const QString &)> walk = [&](const QString &parent) {
        for (const Topic &t : p.topics)
            if (t.parent == parent && !t.other) { out << &t; walk(t.id); }
    };
    walk(QString());
    for (const Topic &t : p.topics) if (t.other) out << &t;      // Other last
    return out;
}

QVariantMap Pods::pod(const QString &id, const QString &query) const
{
    const Pod *p = find(id);
    if (!p) return {};
    const QString q = query.trimmed();
    // the documents each topic holds itself that match the search
    QHash<QString, QVariantList> direct;
    QHash<QString, int> matches;      // in the topic and every topic inside it
    QHash<QString, QString> parentOf;
    QSet<QString> hits;               // topics whose own name matches: all their documents show
    for (const Topic &t : p->topics) {
        parentOf.insert(t.id, t.parent);
        if (!q.isEmpty() && (t.name.contains(q, Qt::CaseInsensitive) || t.number.startsWith(q))) hits.insert(t.id);
    }
    auto underHit = [&](QString tid) {
        for (; !tid.isEmpty(); tid = parentOf.value(tid)) if (hits.contains(tid)) return true;
        return false;
    };
    for (const Doc &d : p->docs) {
        if (!q.isEmpty() && !underHit(d.topic) && !d.name.contains(q, Qt::CaseInsensitive) && !d.snippet.contains(q, Qt::CaseInsensitive)) continue;
        direct[d.topic] << QVariantMap{{"path", d.path}, {"name", d.name}, {"ext", d.ext}, {"snippet", d.snippet},
                                       {"words", d.words}, {"readable", d.readable}, {"folder", d.folder},
                                       {"check", d.check}, {"kept", d.kept}, {"second", d.second}};
        for (QString tid = d.topic; !tid.isEmpty(); tid = parentOf.value(tid)) ++matches[tid];
    }
    QSet<QString> parents;
    for (const Topic &t : p->topics) if (!t.parent.isEmpty()) parents.insert(t.parent);
    QVariantList topics;
    int shown = 0;
    for (const Topic *t : ordered(*p)) {
        if (matches.value(t->id) == 0) continue;
        QStringList ancestors;
        for (QString a = t->parent; !a.isEmpty(); a = parentOf.value(a)) ancestors << a;
        shown += int(direct.value(t->id).size());
        topics << QVariantMap{{"id", t->id}, {"name", t->name}, {"number", t->number}, {"summary", t->summary},
                              {"keywords", t->keywords}, {"other", t->other}, {"depth", int(ancestors.size())},
                              {"ancestors", ancestors}, {"hasChildren", parents.contains(t->id)},
                              {"total", matches.value(t->id)}, {"docs", direct.value(t->id)}};
    }
    QString when;
    if (p->sorted > 0) {
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(p->sorted);
        when = dt.date() == QDate::currentDate() ? dt.toString(QStringLiteral("HH:mm")) : dt.toString(QStringLiteral("d MMM"));
    }
    const QStringList details = {QStringLiteral("broad"), QStringLiteral("balanced"), QStringLiteral("specific")};
    const QString how = p->method == QLatin1String("ai-writer") ? QStringLiteral("Sorted and named by on-device AI")
                      : p->method == QLatin1String("ai") ? QStringLiteral("Sorted by on-device AI") : QStringLiteral("Sorted by key words");
    const QString shape = p->layout == QLatin1String("spec") ? QStringLiteral("matched to ") + (p->specName.isEmpty() ? QStringLiteral("your spec") : p->specName)
                        : p->layout == QLatin1String("nested") ? QStringLiteral("subjects › topics") : details.value(p->detail) + QStringLiteral(" topics");
    const QString sortedLabel = p->sorted == 0 ? QString() : how + QStringLiteral(" · ") + shape + QStringLiteral(" · ") + when;
    if (p->specPoints < 0) p->specPoints = p->specText.isEmpty() ? 0 : int(podsort::parseSpec(p->specText.toStdString()).size());
    return {{"id", p->id}, {"name", p->name}, {"folder", QDir::toNativeSeparators(p->folder)}, {"detail", p->detail},
            {"layout", p->layout}, {"specName", p->specName}, {"specPoints", p->specPoints}, {"specSource", p->specSource},
            {"specBusy", p->specBusy}, {"colour", p->colour},
            {"checkCount", int(std::count_if(p->docs.begin(), p->docs.end(), [](const Doc &d) { return d.check; }))},
            {"method", p->method}, {"sortedLabel", sortedLabel}, {"busy", p->busy}, {"status", p->status},
            {"error", p->error}, {"moved", !p->moves.isEmpty()}, {"docCount", int(p->docs.size())},
            {"shown", shown}, {"topics", topics}};
}

QVariantList Pods::topicChoices(const QString &podId) const
{
    QVariantList out;
    const Pod *p = find(podId);
    if (!p) return out;
    if (p->layout == QLatin1String("spec") && !p->specText.isEmpty()) {      // every point of the spec, filed under or not
        const auto points = podsort::parseSpec(p->specText.toStdString());
        for (int l : podsort::leafPoints(points)) {
            QStringList path;
            for (int a = l; a >= 0; a = points[size_t(a)].parent) {
                const auto &pt = points[size_t(a)];
                path.prepend(QString::fromStdString(pt.number.empty() ? pt.title : pt.number + " " + pt.title));
            }
            out << QVariantMap{{"id", QStringLiteral("pt:%1").arg(l)}, {"name", path.join(QStringLiteral(" › "))},
                               {"number", QString::fromStdString(points[size_t(l)].number)},
                               {"title", QString::fromStdString(points[size_t(l)].title)}};
        }
        for (const Topic &t : p->topics) if (t.other) out << QVariantMap{{"id", t.id}, {"name", t.name}, {"number", ""}, {"title", t.name}};
        return out;
    }
    QHash<QString, const Topic *> byId;
    for (const Topic &t : p->topics) byId.insert(t.id, &t);
    for (const Topic *t : ordered(*p)) {
        QStringList path;
        for (const Topic *a = t; a; a = byId.value(a->parent)) path.prepend(a->number.isEmpty() ? a->name : a->number + QLatin1Char(' ') + a->name);
        out << QVariantMap{{"id", t->id}, {"name", path.join(QStringLiteral(" › "))}};
    }
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

void Pods::setLayout(const QString &id, const QString &layout)
{
    Pod *p = find(id);
    if (!p || p->busy || (layout != QLatin1String("topics") && layout != QLatin1String("nested") && layout != QLatin1String("spec"))) return;
    if (p->layout == layout) return;
    p->layout = layout;
    changed();
}

QVariantMap Pods::setSpec(const QString &id, const QString &name, const QString &text)
{
    Pod *p = find(id);
    if (!p) return {{"error", "That pod has gone."}};
    const auto pts = podsort::parseSpec(text.toStdString());
    if (pts.size() < 2) return {{"error", "That doesn't look like a specification: no numbered points (such as \"3.2.1 Cell structure\") or sections were found."}};
    p->specName = name.simplified().left(80);
    if (p->specName.isEmpty()) p->specName = QStringLiteral("Your spec");
    p->specText = text.left(600000);
    p->specSource.clear();
    p->specPoints = int(pts.size());
    p->layout = QStringLiteral("spec");
    changed();
    return {{"points", int(pts.size())}};
}

void Pods::loadSpecFile(const QString &id, const QString &url)
{
    const QUrl u(url);
    const QString path = u.isLocalFile() ? u.toLocalFile() : url;      // Android hands over content:// addresses
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { emit specLoaded(id, {{"error", "Couldn't open that file."}}); return; }
    QString name = u.fileName();
    if (name.isEmpty() || name.contains(QLatin1Char('%'))) name = QFileInfo(path).fileName();
    loadSpecData(id, f.read(kMaxPdfBytes), QFileInfo(name).completeBaseName());
}

void Pods::loadSpecData(const QString &id, const QByteArray &data, const QString &name, const QString &source)
{
    Pod *p = find(id);
    if (!p) return;
    p->specBusy = true;
    ++m_rev;
    emit revisionChanged();
    auto *w = new QFutureWatcher<QVariantMap>(this);
    connect(w, &QFutureWatcher<QVariantMap>::finished, this, [this, w, id, name, source]() {
        w->deleteLater();
        QVariantMap r = w->result();
        Pod *p = find(id);
        if (!p) return;
        p->specBusy = false;
        if (r.contains(QLatin1String("error"))) {
            ++m_rev;
            emit revisionChanged();
        } else {
            // a spec from the list keeps the list's name; a file is named after the course it says it is for
            p->specName = (source.isEmpty() ? r.value(QLatin1String("name")).toString() : name).simplified().left(80);
            if (p->specName.isEmpty()) p->specName = QStringLiteral("Your spec");
            p->specText = r.value(QLatin1String("text")).toString().left(600000);
            p->specPoints = r.value(QLatin1String("points")).toInt();
            p->specSource = source;
            p->layout = QStringLiteral("spec");
            r.remove(QLatin1String("text"));
            r.insert(QStringLiteral("name"), p->specName);
            changed();
            if (p->busy) p->sortAgain = true;      // still sorting: match once that's done
            else run(id, QStringLiteral("Matching your documents to the spec…"), p->detail, 0);
        }
        emit specLoaded(id, r);
    });
    w->setFuture(QtConcurrent::run([data, name]() {
        QVariantMap r = Pods::specFromData(data, name);
        if (r.contains(QLatin1String("error"))) return r;
        const auto pts = podsort::parseSpec(r.value(QLatin1String("text")).toString().toStdString());
        if (pts.size() < 2)
            return QVariantMap{{"error", "That doesn't look like a specification: no numbered points (such as \"3.2.1 Cell structure\") or sections were found."}};
        r.insert(QStringLiteral("points"), int(pts.size()));
        return r;
    }));
}

QVariantMap Pods::readSpecFile(const QString &url) const
{
    const QUrl u(url);
    const QString path = u.isLocalFile() ? u.toLocalFile() : url;      // Android hands over content:// addresses
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {{"error", "Couldn't open that file."}};
    const QByteArray data = f.read(kMaxPdfBytes);
    QString name = u.fileName();
    if (name.isEmpty() || name.contains(QLatin1Char('%'))) name = QFileInfo(path).fileName();
    name = QFileInfo(name).completeBaseName();
    return specFromData(data, name);
}

// A spec's text from the file's bytes (a PDF, Word, OpenDocument or text file), named after the course it is
// for when the spec says ("AQA GCSE Biology (8461)"), otherwise after the file.
QVariantMap Pods::specFromData(const QByteArray &data, const QString &fileName)
{
    QString name = fileName;
    QString title, headings, body;
    if (data.contains("%PDF-") && data.indexOf("%PDF-") < 1024) {
        const auto doc = pdftext::read(std::string(data.constData(), size_t(data.size())));
        if (!doc.error.empty()) return {{"error", QString::fromStdString(doc.error)}};
        body = QString::fromStdString(pdftext::text(doc));
        if (body.trimmed().isEmpty())
            return {{"error", "This PDF has no text to read: its pages are probably scanned pictures. Try the board's own copy of the spec."}};
    } else if (data.startsWith("PK")) {      // Word or OpenDocument: both are zip files
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        QZipReader zip(&buf);
        const auto files = zip.fileInfoList();
        bool word = false;
        for (const auto &fi : files) if (fi.filePath == QLatin1String("word/document.xml")) word = true;
        if (word) readDocxZip(zip, title, headings, body);
        else readOdtZip(zip, headings, body);
    } else {
        body = QString::fromUtf8(data);
    }
    if (body.trimmed().isEmpty()) return {{"error", "No text found in that file."}};
    const auto info = specread::identify(body.toStdString());
    if (!info.name.empty()) name = QString::fromStdString(info.name);
    return {{"name", name}, {"text", body}, {"board", QString::fromStdString(info.board)}};
}
void Pods::refresh(const QString &id) { if (const Pod *p = find(id)) run(id, QStringLiteral("Looking for new documents…"), p->detail, 1); }
void Pods::run(const QString &id, const QString &status, int detail, int kind)
{
    Pod *p = find(id);
    if (!p || p->busy) return;
    p->busy = true;
    p->status = status;
    p->error.clear();
    ++m_rev;
    emit revisionChanged();

    // what the pod knows now, for a refresh: which topic each file is in, and the topics that hold documents
    QHash<QString, QString> known;
    QStringList leafIds;
    QSet<QString> parents;
    for (const Topic &t : p->topics) if (!t.parent.isEmpty()) parents.insert(t.parent);
    for (const Doc &d : p->docs) known.insert(d.path, d.topic);
    // spec pods: the point each document is under now, and the ones you put there yourself, each by its place in the
    // spec (its number, or its title, and those of the points above it: a spec can use a number twice)
    QHash<QString, QString> numberOf, knownNumber, keptNumber;
    {
        QHash<QString, const Topic *> byId;
        for (const Topic &t : p->topics) byId.insert(t.id, &t);
        for (const Topic &t : p->topics) {
            QStringList place;
            for (const Topic *a = &t; a; a = byId.value(a->parent)) place.prepend(a->number.isEmpty() ? a->name : a->number);
            numberOf.insert(t.id, place.join(QChar(0x203A)));
        }
    }
    for (const Doc &d : p->docs) {
        knownNumber.insert(d.path, numberOf.value(d.topic));
        if (d.kept) keptNumber.insert(d.path, numberOf.value(d.topic));
    }
    for (const Topic &t : p->topics) if (!t.other && !parents.contains(t.id)) leafIds << t.id;
    const QString folder = p->folder;
    const QString layout = p->layout;
    const std::string specText = p->specText.toStdString();
    const QString meaningPath = m_models ? m_models->path(QStringLiteral("meaning")) : QString();
    const QString writerPath = m_models ? m_models->path(QStringLiteral("writer")) : QString();
    // the spec as the meaning model reads it, kept after the first sort (it takes a while on a phone)
    QString specCache;
    if (layout == QLatin1String("spec") && !meaningPath.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/spec-index");
        QDir().mkpath(dir);
        QCryptographicHash h(QCryptographicHash::Sha1);
        h.addData(p->specText.toUtf8());
        h.addData(QFileInfo(meaningPath).fileName().toUtf8());
        h.addData("reader 2");      // the spec reader changes: read the spec afresh
        specCache = dir + QLatin1Char('/') + QString::fromLatin1(h.result().toHex().left(24)) + QStringLiteral(".bin");
    }

    // the worker says how far it has got; the pod's status line shows it
    auto say = [this, id](const QString &text) {
        QMetaObject::invokeMethod(this, [this, id, text]() {
            if (Pod *p = find(id); p && p->busy) { p->status = text; ++m_rev; emit revisionChanged(); }
        }, Qt::QueuedConnection);
    };

    auto *w = new QFutureWatcher<Result>(this);
    connect(w, &QFutureWatcher<Result>::finished, this, [this, w, id, detail, kind]() {
        const Result r = w->result();
        w->deleteLater();
        Pod *p = find(id);
        if (!p) return;
        if (r.docs.isEmpty()) { finish(p, QStringLiteral("No documents found in this folder (Word, OpenDocument, text or PDF).")); return; }
        if (kind == 0 || r.spec) {
            QHash<QString, Doc> before;      // what each document was: kept by you, and its note to check
            for (const Doc &d : p->docs) before.insert(d.path, d);
            const auto points = r.spec ? podsort::parseSpec(p->specText.toStdString()) : std::vector<podsort::SpecPoint>();
            auto label = [&](int pt) {
                if (pt < 0 || size_t(pt) >= points.size()) return QString();
                const auto &x = points[size_t(pt)];
                return QString::fromStdString(x.number.empty() ? x.title : x.number + " " + x.title);
            };
            p->topics.clear();
            p->docs.clear();
            QVector<QString> ids;
            for (const auto &n : r.nodes) {
                Topic tp;
                tp.id = newId();
                ids << tp.id;
                tp.name = QString::fromStdString(n.name);
                tp.summary = QString::fromStdString(n.summary);
                tp.number = QString::fromStdString(n.number);
                for (const auto &kw : n.keywords) tp.keywords << QString::fromStdString(kw);
                tp.parent = n.parent >= 0 && n.parent < ids.size() ? ids[n.parent] : QString();
                tp.other = n.other;
                p->topics.push_back(tp);
                for (int i : n.docs) {
                    const Scanned &s = r.docs[i];
                    Doc d{s.path, s.name, s.ext, s.rel, tp.id, snippetOf(s.body), s.words, s.readable};
                    const auto was = before.constFind(s.path);
                    d.kept = was != before.constEnd() && was->kept;
                    if (r.spec && size_t(i) < r.check.size()) {
                        const bool fresh = was == before.constEnd();
                        d.check = !d.kept && (kind == 0 || fresh ? r.check[size_t(i)] : was->check);
                        d.second = kind == 0 || fresh ? label(r.second[size_t(i)]) : was->second;
                    }
                    p->docs.push_back(d);
                }
            }
            if (kind == 0) {
                p->detail = detail;
                p->method = r.method;
                p->sorted = now();
            }
        } else {
            QHash<QString, QString> before;
            for (const Doc &d : p->docs) before.insert(d.path, d.topic);
            QSet<QString> parents;
            for (const Topic &t : p->topics) if (!t.parent.isEmpty()) parents.insert(t.parent);
            QStringList leafIds;
            for (const Topic &t : p->topics) if (!t.other && !parents.contains(t.id)) leafIds << t.id;
            QVector<Doc> docs;
            for (int i = 0; i < r.docs.size(); ++i) {
                const Scanned &s = r.docs[i];
                QString topic = before.value(s.path);
                if (topic.isEmpty()) {
                    const auto f = std::find(r.fresh.begin(), r.fresh.end(), i);
                    const int k = f == r.fresh.end() ? -1 : r.placed[size_t(f - r.fresh.begin())];
                    topic = k >= 0 && k < leafIds.size() ? leafIds[k] : otherTopic(*p).id;
                }
                docs.push_back({s.path, s.name, s.ext, s.rel, topic, snippetOf(s.body), s.words, s.readable});
            }
            p->docs = docs;
            dropEmptyTopics(*p);
        }
        finish(p, r.note);
    });
    w->setFuture(QtConcurrent::run([folder, kind, detail, layout, specText, specCache, known, leafIds, meaningPath, writerPath, say,
                                    knownNumber, keptNumber]() {
        Result r;
        r.docs = scanFolder(folder);
        const int n = int(r.docs.size());
        if (n == 0) return r;
        const auto words = toSortDocs(r.docs);
        // a spec pod is matched to its spec when sorted, and when checking for new files (new documents are matched;
        // the others stay where they are)
        const bool spec = layout == QLatin1String("spec") && !specText.empty();
        const auto points = spec ? podsort::parseSpec(specText) : std::vector<podsort::SpecPoint>();
        const auto leaves = podsort::leafPoints(points);
        std::vector<int> docPoint;          // spec: the point each document matched
        std::map<QString, int> pointByNumber;      // each point by its place in the spec, as the pod's folders have it
        for (size_t i = 0; i < points.size(); ++i) {
            QStringList place;
            for (int a = int(i); a >= 0; a = points[size_t(a)].parent)
                place.prepend(QString::fromStdString(points[size_t(a)].number.empty() ? points[size_t(a)].title : points[size_t(a)].number));
            pointByNumber.emplace(place.join(QChar(0x203A)), int(i));
        }
        auto pointOf = [&](const QHash<QString, QString> &h, const QString &path) {
            const auto it = h.constFind(path);
            if (it == h.constEnd() || it->isEmpty()) return -1;
            const auto f = pointByNumber.find(*it);
            return f == pointByNumber.end() ? -1 : f->second;
        };
        std::vector<int> examples;          // spec: the point you put each document under yourself, or -1
        for (int i = 0; i < n; ++i) examples.push_back(pointOf(keptNumber, r.docs[i].path));

        // The meaning model reads every document (its name, headings and text, in a few pieces when it is long)
        std::vector<std::vector<float>> vecs;
        if (!meaningPath.isEmpty() && n > 1) {
            podai::Meaning m;
            std::string err;
            if (m.load(meaningPath.toStdString(), err)) {
                std::vector<std::vector<std::vector<float>>> pieces;      // each document's pieces, for a spec
                for (int i = 0; i < n; ++i) {
                    if (i % 4 == 0) say(QStringLiteral("Reading documents with on-device AI… %1 of %2").arg(i + 1).arg(n));
                    const auto &d = r.docs[i];
                    auto ps = m.readPieces((d.title.isEmpty() ? d.name : d.name + QStringLiteral(": ") + d.title).toStdString(),
                                           d.headings.toStdString(), d.body.toStdString());
                    std::vector<float> mean;
                    for (const auto &v : ps) {
                        if (mean.empty()) mean.assign(v.size(), 0.f);
                        for (size_t k = 0; k < v.size(); ++k) mean[k] += v[k];
                    }
                    double len = 0;
                    for (float x : mean) len += double(x) * x;
                    len = std::sqrt(len);
                    if (len > 0) for (float &x : mean) x = float(x / len);
                    vecs.push_back(mean);
                    if (spec) pieces.push_back(std::move(ps));
                }
                if (spec && !leaves.empty()) {      // and every point of the spec (read once, then kept)
                    podai::SpecIndex index;
                    if (specCache.isEmpty() || !index.load(specCache.toStdString(), points.size())) {
                        index = podai::readSpec(m, points, [&](int done, int total) {
                            if (done % 10 == 0) say(QStringLiteral("Reading the spec with on-device AI… %1 of %2 points").arg(done + 1).arg(total));
                            return true;
                        });
                        if (!specCache.isEmpty()) index.save(specCache.toStdString());
                    }
                    say(QStringLiteral("Matching to the spec…"));
                    std::vector<std::string> targets;
                    for (int l : leaves) targets.push_back(podsort::pointText(points, l));
                    for (const auto &pl : podai::matchSpec(pieces, points, index, podsort::matchScores(words, targets), examples)) {
                        docPoint.push_back(pl.point);
                        r.second.push_back(pl.point >= 0 ? pl.second : pl.best);
                        r.check.push_back(pl.point >= 0 && !pl.sure);
                    }
                }
            } else {
                r.note = QStringLiteral("The meaning model couldn't start, so key words were used. Try removing and downloading it again.");
                vecs.clear();
            }
        }
        const bool byMeaning = vecs.size() == size_t(n);

        if (kind == 1 && !spec) {      // refresh: new documents join the topic (with nothing inside it) they fit best
            std::vector<std::vector<int>> groups(size_t(leafIds.size()));
            for (int i = 0; i < n; ++i) {
                const auto it = known.constFind(r.docs[i].path);
                if (it == known.constEnd()) { r.fresh.push_back(i); continue; }
                const qsizetype k = leafIds.indexOf(*it);
                if (k >= 0) groups[size_t(k)].push_back(i);
            }
            if (!r.fresh.empty())
                r.placed = byMeaning ? podai::place(vecs, groups, r.fresh, detail) : podsort::placeNew(words, groups, r.fresh, detail);
            return r;
        }

        if (spec) {
            if (docPoint.empty()) {
                say(QStringLiteral("Matching to the spec…"));
                std::vector<std::string> targets;
                for (int l : leaves) targets.push_back(podsort::pointText(points, l));
                for (int x : podsort::matchPoints(words, targets)) {
                    docPoint.push_back(x < 0 ? -1 : leaves[size_t(x)]);
                    r.second.push_back(-1);
                    r.check.push_back(x >= 0);      // by key words alone: always worth a look
                }
            }
            for (int i = 0; i < n; ++i) {
                if (examples[size_t(i)] >= 0) { docPoint[size_t(i)] = examples[size_t(i)]; r.check[size_t(i)] = false; }
                else if (kind == 1 && known.contains(r.docs[i].path)) docPoint[size_t(i)] = pointOf(knownNumber, r.docs[i].path);
            }
            r.nodes = podsort::specTree(points, docPoint);
            r.method = byMeaning ? QStringLiteral("ai") : QStringLiteral("words");
            r.spec = true;
            r.docPoint = docPoint;
            return r;      // the spec names its own points
        }

        if (layout == QLatin1String("nested")) {
            r.nodes = byMeaning ? podsort::nest(words, [&vecs](const std::vector<int> &subset, int level) { return podai::groupWithin(vecs, subset, level); })
                                : podsort::nest(words, podsort::wordGrouper(words));
        } else {
            const auto topics = byMeaning ? podsort::nameGroups(words, podai::group(vecs, detail)) : podsort::sortDocs(words, detail);
            for (const auto &t : topics) {
                podsort::Node nd;
                nd.name = t.name;
                nd.keywords = t.keywords;
                nd.docs = t.docs;
                nd.other = t.other;
                nd.level = 1;
                r.nodes.push_back(nd);
            }
        }
        r.method = byMeaning ? QStringLiteral("ai") : QStringLiteral("words");

        // The writer reads the documents in each topic and names it (subjects first, then the topics inside them)
        if (!writerPath.isEmpty()) {
            say(QStringLiteral("Starting the writer…"));
            podai::Writer wr;
            std::string err;
            if (wr.load(writerPath.toStdString(), err)) {
                int done = 0, total = 0;
                for (const auto &nd : r.nodes) if (!nd.other) ++total;
                for (size_t k = 0; k < r.nodes.size(); ++k) {
                    auto &nd = r.nodes[k];
                    if (nd.other) continue;
                    say(QStringLiteral("Naming topics… %1 of %2").arg(++done).arg(total));
                    std::vector<std::string> siblings;
                    for (size_t j = 0; j < k; ++j) if (r.nodes[j].parent == nd.parent && !r.nodes[j].other) siblings.push_back(r.nodes[j].name);
                    const std::string parent = nd.parent >= 0 ? r.nodes[size_t(nd.parent)].name : std::string();
                    const podai::Named nm = podai::name(wr, docTexts(r.docs, podsort::subtree(r.nodes, int(k))), siblings,
                                                        layout == QLatin1String("nested") ? nd.level : 1, parent);
                    bool clash = nm.name.empty();
                    for (const auto &o : siblings) if (QString::fromStdString(o).compare(QString::fromStdString(nm.name), Qt::CaseInsensitive) == 0) clash = true;
                    if (!parent.empty() && QString::fromStdString(parent).compare(QString::fromStdString(nm.name), Qt::CaseInsensitive) == 0) clash = true;
                    if (!clash) nd.name = nm.name;
                    nd.summary = nm.summary;
                }
                if (byMeaning) r.method = QStringLiteral("ai-writer");
            } else if (r.note.isEmpty()) {
                r.note = QStringLiteral("The writer model couldn't start, so topics were named by their key words.");
            }
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
    if (p->sortAgain) {      // a spec was chosen while this sort ran: match to it now
        p->sortAgain = false;
        run(p->id, QStringLiteral("Matching your documents to the spec…"), p->detail, 0);
    }
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
    // a topic with no documents and no topics inside it goes; that can empty its parent, so go round again
    for (bool again = true; again;) {
        again = false;
        QSet<QString> used;
        for (const Doc &d : p.docs) used.insert(d.topic);
        for (const Topic &t : p.topics) if (!t.parent.isEmpty()) used.insert(t.parent);
        for (int i = p.topics.size() - 1; i >= 0; --i)
            if (!used.contains(p.topics[i].id)) { p.topics.remove(i); again = true; }
    }
}

QString Pods::topicDir(const Pod &p, const Topic &t) const
{
    QStringList parts;
    for (const Topic *a = &t; a;) {
        parts.prepend(folderName(a->number.isEmpty() ? a->name : a->number + QLatin1Char(' ') + a->name));
        const Topic *up = nullptr;
        for (const Topic &x : p.topics) if (x.id == a->parent) up = &x;
        a = up;
    }
    return QDir(p.folder).filePath(parts.join(QLatin1Char('/')));
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

void Pods::keepDoc(const QString &podId, const QString &path)
{
    Pod *p = find(podId);
    if (!p) return;
    for (Doc &d : p->docs)
        if (d.path == path) { d.kept = true; d.check = false; changed(); return; }
}

void Pods::setColour(const QString &podId, const QString &colour)
{
    Pod *p = find(podId);
    if (!p || p->colour == colour) return;
    p->colour = colour;
    changed();
}

QString Pods::pointTopic(Pod &p, int point)
{
    const auto points = podsort::parseSpec(p.specText.toStdString());
    if (point < 0 || size_t(point) >= points.size()) return {};
    QString parent;
    std::vector<int> chain;
    for (int a = point; a >= 0; a = points[size_t(a)].parent) chain.insert(chain.begin(), a);
    for (int a : chain) {
        const QString number = QString::fromStdString(points[size_t(a)].number);
        const QString title = QString::fromStdString(points[size_t(a)].title);
        QString id;
        for (const Topic &t : p.topics)
            if (!t.other && t.parent == parent && t.number == number && (number.isEmpty() ? t.name == title : true)) { id = t.id; break; }
        if (id.isEmpty()) {
            Topic t;
            t.id = newId();
            t.number = number;
            t.name = title;
            t.parent = parent;
            auto at = std::find_if(p.topics.begin(), p.topics.end(), [](const Topic &x) { return x.other; });
            p.topics.insert(at, t);
            id = t.id;
        }
        parent = id;
    }
    return parent;
}

void Pods::moveDoc(const QString &podId, const QString &path, const QString &topicId)
{
    Pod *p = find(podId);
    if (!p) return;
    QString target = topicId;
    if (topicId.startsWith(QLatin1String("pt:"))) {
        target = pointTopic(*p, topicId.mid(3).toInt());
        if (target.isEmpty()) return;
    } else if (topicId == QLatin1String("new")) {
        Topic t;
        t.id = newId();
        t.name = QStringLiteral("New topic");
        auto at = std::find_if(p->topics.begin(), p->topics.end(), [](const Topic &x) { return x.other; });
        p->topics.insert(at, t);
        target = t.id;
    }
    for (Doc &d : p->docs)
        if (d.path == path) { d.topic = target; d.kept = true; d.check = false; d.second.clear(); break; }
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
    for (const Topic *t : ordered(*p)) {
        if (t->other) continue;
        const QString dir = topicDir(*p, *t);
        bool any = false;
        for (const Doc &d : p->docs)
            if (d.topic == t->id && QFileInfo(d.path).absolutePath() != dir) { ++files; any = true; }
        if (any) names << QDir(p->folder).relativeFilePath(dir);
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
    for (const Topic *t : ordered(*p)) {
        if (t->other) continue;       // documents that fit nowhere stay where they are
        const QString dir = topicDir(*p, *t);
        const QString rel = root.relativeFilePath(dir);
        for (Doc &d : p->docs) {
            if (d.topic != t->id || QFileInfo(d.path).absolutePath() == dir) continue;
            if (!QFileInfo::exists(dir)) {
                // remember each folder made on the way down, deepest last, so Undo can take them away again
                QStringList made;
                for (QString x = dir; !QFileInfo::exists(x) && x.startsWith(p->folder) && x != p->folder; x = QFileInfo(x).absolutePath())
                    made.prepend(x);
                if (!root.mkpath(rel)) { ++failed; continue; }
                p->madeDirs << made;
            }
            const QFileInfo fi(d.path);
            QString to = QDir(dir).filePath(fi.fileName());
            for (int n = 2; QFileInfo::exists(to); ++n)
                to = QDir(dir).filePath(QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
            if (QFile::rename(d.path, to)) {
                moves.push_back({d.path, to});
                d.path = to;
                d.folder = rel;
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
    for (int i = p->madeDirs.size() - 1; i >= 0; --i) {      // deepest first; a folder goes only when it is empty
        const QString &dir = p->madeDirs[i];
        if (!QDir().rmdir(dir) && QFileInfo::exists(dir)) kept.prepend(dir);
    }
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
        p.method = o.value(QLatin1String("method")).toString(QStringLiteral("words"));
        if (p.method == QLatin1String("device")) p.method = QStringLiteral("words");
        p.layout = o.value(QLatin1String("layout")).toString(QStringLiteral("topics"));      // pods from 0.4 were one level
        p.specName = o.value(QLatin1String("specName")).toString();
        p.specText = o.value(QLatin1String("specText")).toString();
        p.specSource = o.value(QLatin1String("specSource")).toString();
        p.colour = o.value(QLatin1String("colour")).toString();
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
            tp.number = t.value(QLatin1String("number")).toString();
            tp.parent = t.value(QLatin1String("parent")).toString();
            p.topics.push_back(tp);
        }
        for (const QJsonValue &dv : o.value(QLatin1String("docs")).toArray()) {
            const QJsonObject d = dv.toObject();
            Doc doc{d.value(QLatin1String("path")).toString(), d.value(QLatin1String("name")).toString(),
                    d.value(QLatin1String("ext")).toString(), d.value(QLatin1String("folder")).toString(),
                    d.value(QLatin1String("topic")).toString(), d.value(QLatin1String("snippet")).toString(),
                    d.value(QLatin1String("words")).toInt(), d.value(QLatin1String("readable")).toBool()};
            doc.check = d.value(QLatin1String("check")).toBool();
            doc.kept = d.value(QLatin1String("kept")).toBool();
            doc.second = d.value(QLatin1String("second")).toString();
            p.docs.push_back(doc);
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
            topics << QJsonObject{{"id", t.id}, {"name", t.name}, {"summary", t.summary}, {"number", t.number},
                                  {"parent", t.parent}, {"keywords", QJsonArray::fromStringList(t.keywords)}, {"other", t.other}};
        for (const Doc &d : p.docs)
            docs << QJsonObject{{"path", d.path}, {"name", d.name}, {"ext", d.ext}, {"folder", d.folder}, {"topic", d.topic},
                                {"snippet", d.snippet}, {"words", d.words}, {"readable", d.readable},
                                {"check", d.check}, {"kept", d.kept}, {"second", d.second}};
        for (const Move &m : p.moves) moves << QJsonObject{{"from", m.from}, {"to", m.to}};
        arr << QJsonObject{{"id", p.id}, {"name", p.name}, {"folder", p.folder}, {"method", p.method}, {"detail", p.detail},
                           {"layout", p.layout}, {"specName", p.specName}, {"specText", p.specText}, {"specSource", p.specSource},
                           {"colour", p.colour},
                           {"created", double(p.created)}, {"sorted", double(p.sorted)}, {"topics", topics}, {"docs", docs},
                           {"moves", moves}, {"madeDirs", QJsonArray::fromStringList(p.madeDirs)}};
    }
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(QJsonObject{{"pods", arr}}).toJson(QJsonDocument::Compact));
    f.commit();
}
