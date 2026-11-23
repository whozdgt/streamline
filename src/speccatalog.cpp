#include "speccatalog.h"
#include "pods.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace {

const char *kListUrl = "https://raw.githubusercontent.com/whozdgt/streamline/main/specs/catalog.json";
const qint64 kMaxBytes = 80LL * 1024 * 1024;

// The exam boards' own websites: the only places a spec is downloaded from
const char *kBoardSites[] = {"cambridgeinternational.org", "cie.org.uk", "cambridgeassessment.org.uk", "aqa.org.uk",
                             "pearson.com", "ocr.org.uk", "wjec.co.uk", "eduqas.co.uk", "ccea.org.uk"};

QString siteOf(const QString &url)
{
    QString host = QUrl(url).host();
    if (host.startsWith(QLatin1String("www."))) host = host.mid(4);
    return host;
}

// "Cambridge's website", "Pearson's website"
QString websiteOf(const QString &board)
{
    const QString who = board == QLatin1String("Edexcel") ? QStringLiteral("Pearson") : board;
    return who + QStringLiteral("’s website");
}

int stageOf(const QString &level)
{
    const QString l = level.toLower();
    if (l.contains(QLatin1String("gcse"))) return 0;      // GCSE, IGCSE, International GCSE
    return 1;                                              // AS & A Level, A-level, International A level
}

int boardOrder(const QString &board)
{
    static const QStringList order = {QStringLiteral("Cambridge"), QStringLiteral("AQA"), QStringLiteral("Edexcel"), QStringLiteral("OCR")};
    const int i = int(order.indexOf(board));
    return i < 0 ? int(order.size()) : i;
}

} // namespace

SpecCatalog::SpecCatalog(Pods *pods, QObject *parent) : QObject(parent), m_pods(pods)
{
    load();
}

bool SpecCatalog::boardSite(const QString &url)
{
    const QUrl u(url);
    if (u.scheme() != QLatin1String("https")) return false;
    const QString host = u.host().toLower();
    for (const char *site : kBoardSites) {
        const QString s = QString::fromLatin1(site);
        if (host == s || host.endsWith(QLatin1Char('.') + s)) return true;
    }
    return false;
}

QString SpecCatalog::displayName(const Entry &e)
{
    const QString board = e.board == QLatin1String("Edexcel") ? QStringLiteral("Pearson Edexcel")
                        : e.board == QLatin1String("Cambridge") && e.level.contains(QLatin1String("A Level")) ? QStringLiteral("Cambridge International")
                        : e.board;
    return board + QLatin1Char(' ') + e.level + QLatin1Char(' ') + e.subject + QStringLiteral(" (") + e.code + QLatin1Char(')');
}

int SpecCatalog::parse(const QByteArray &json, QVector<Entry> &out)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const int version = root.value(QLatin1String("version")).toInt();
    const QJsonArray specs = root.value(QLatin1String("specs")).toArray();
    if (version <= 0 || specs.isEmpty()) return 0;
    QVector<Entry> list;
    for (const QJsonValue &v : specs) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QLatin1String("id")).toString();
        e.board = o.value(QLatin1String("board")).toString();
        e.level = o.value(QLatin1String("level")).toString();
        e.subject = o.value(QLatin1String("subject")).toString();
        e.code = o.value(QLatin1String("code")).toString();
        e.exams = o.value(QLatin1String("exams")).toString();
        e.url = o.value(QLatin1String("url")).toString();
        e.page = o.value(QLatin1String("page")).toString();
        if (e.id.isEmpty() || e.subject.isEmpty() || !boardSite(e.url)) continue;
        if (!e.page.isEmpty() && !boardSite(e.page)) e.page.clear();
        list.push_back(e);
    }
    if (list.isEmpty()) return 0;
    out = list;
    return version;
}

QString SpecCatalog::cachePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/spec-catalog.json");
}

void SpecCatalog::load()
{
    QFile builtIn(QStringLiteral(":/specs/catalog.json"));
    if (builtIn.open(QIODevice::ReadOnly)) m_version = parse(builtIn.readAll(), m_entries);
    QFile cached(cachePath());
    if (cached.open(QIODevice::ReadOnly)) {
        QVector<Entry> list;
        const int v = parse(cached.readAll(), list);
        if (v > m_version) { m_version = v; m_entries = list; }
    }
}

void SpecCatalog::update()
{
    if (m_updated) return;
    m_updated = true;
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QString::fromLatin1(kListUrl))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;      // the built-in list will do
        const QByteArray data = reply->read(4 * 1024 * 1024);
        QVector<Entry> list;
        const int v = parse(data, list);
        if (v <= m_version) return;
        m_version = v;
        m_entries = list;
        QSaveFile f(cachePath());
        if (f.open(QIODevice::WriteOnly)) { f.write(data); f.commit(); }
        emit listChanged();
    });
}

QVariantList SpecCatalog::find(const QString &query, const QString &board, const QString &stage) const
{
    // the words typed, with the usual short forms
    QStringList words = query.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}&+]+")), Qt::SkipEmptyParts);
    for (QString &w : words) {
        if (w == QLatin1String("maths") || w == QLatin1String("math")) w = QStringLiteral("mathematics");
        else if (w == QLatin1String("cs") || w == QLatin1String("compsci")) w = QStringLiteral("computer");
        else if (w == QLatin1String("ict")) w = QStringLiteral("communication");
        else if (w == QLatin1String("cie") || w == QLatin1String("caie")) w = QStringLiteral("cambridge");
        else if (w == QLatin1String("pearson")) w = QStringLiteral("edexcel");
        else if (w == QLatin1String("alevel") || w == QLatin1String("a-level")) w = QStringLiteral("level");
        else if (w == QLatin1String("bio")) w = QStringLiteral("biology");
        else if (w == QLatin1String("chem")) w = QStringLiteral("chemistry");
        else if (w == QLatin1String("econ") || w == QLatin1String("econs")) w = QStringLiteral("economics");
        else if (w == QLatin1String("psych")) w = QStringLiteral("psychology");
        else if (w == QLatin1String("geog")) w = QStringLiteral("geography");
        else if (w == QLatin1String("lit")) w = QStringLiteral("literature");
        else if (w == QLatin1String("eng")) w = QStringLiteral("english");
    }
    QVector<const Entry *> hits;
    for (const Entry &e : m_entries) {
        if (!board.isEmpty() && e.board != board) continue;
        if (stage == QLatin1String("gcse") && stageOf(e.level) != 0) continue;
        if (stage == QLatin1String("alevel") && stageOf(e.level) != 1) continue;
        const QString hay = (e.board + QLatin1Char(' ') + (e.board == QLatin1String("Edexcel") ? QStringLiteral("pearson ") : QString()) +
                             e.level + (e.level.contains(QLatin1String("International GCSE")) ? QStringLiteral(" igcse") : QString()) +
                             QLatin1Char(' ') + e.subject + QLatin1Char(' ') + e.code).toLower();
        bool all = true;
        for (const QString &w : words) {
            if (w == QLatin1String("a") || w == QLatin1String("as") || w == QLatin1String("and") || w == QLatin1String("level")) continue;
            if (!hay.contains(w)) { all = false; break; }
        }
        if (all) hits.push_back(&e);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Entry *a, const Entry *b) {
        if (boardOrder(a->board) != boardOrder(b->board)) return boardOrder(a->board) < boardOrder(b->board);
        if (stageOf(a->level) != stageOf(b->level)) return stageOf(a->level) < stageOf(b->level);
        if (a->level != b->level) return a->level < b->level;
        if (a->subject != b->subject) return a->subject < b->subject;
        return a->exams < b->exams;
    });
    QVariantList out;
    for (const Entry *e : hits) {
        if (out.size() >= 80) break;
        out << QVariantMap{{"id", e->id}, {"name", displayName(*e)}, {"board", e->board}, {"level", e->level},
                           {"subject", e->subject}, {"code", e->code}, {"exams", e->exams}, {"site", siteOf(e->url)},
                           {"page", e->page.isEmpty() ? e->url : e->page}};
    }
    return out;
}

void SpecCatalog::fail(const QString &message)
{
    m_error = message;
    m_status.clear();
    m_progress = 0;
    bump();
}

void SpecCatalog::cancel()
{
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
    m_status.clear();
    m_error.clear();
    m_progress = 0;
    bump();
}

void SpecCatalog::use(const QString &podId, const QString &id)
{
    const auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const Entry &e) { return e.id == id; });
    if (it == m_entries.cend() || !m_pods) return;
    const Entry entry = *it;
    if (m_reply) cancel();
    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_podId = podId;
    m_error.clear();
    m_progress = -1;
    m_status = QStringLiteral("Downloading it from ") + websiteOf(entry.board) + QStringLiteral("…");

    QNetworkRequest req{QUrl(entry.url)};
    // a redirect is followed only to the boards' own websites
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Streamline/" APP_VERSION " (+https://github.com/whozdgt/streamline)"));
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_net->get(req);
    m_reply = reply;
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &to) {
        if (boardSite(to.toString())) emit reply->redirectAllowed();
        else reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 got, qint64 total) {
        if (reply != m_reply) return;
        if (got > kMaxBytes) { reply->abort(); return; }
        const double p = total > 0 ? double(got) / double(total) : -1;
        if (int(p * 50) != int(m_progress * 50)) { m_progress = p; bump(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, entry, podId]() {
        reply->deleteLater();
        if (reply != m_reply) return;      // cancelled, or another spec chosen since
        m_reply = nullptr;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            const QString why = code == 404 ? QStringLiteral("it isn't there any more")
                              : reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("error %1").arg(code);
            fail(QStringLiteral("Couldn't download it from ") + websiteOf(entry.board) + QStringLiteral(" (") + why +
                 QStringLiteral("). Check your connection and try again, or open the board’s page, download the spec and choose it as a file."));
            return;
        }
        const QByteArray data = reply->read(kMaxBytes);
        const qsizetype at = data.indexOf("%PDF-");
        if (at < 0 || at > 1024) {
            fail(websiteOf(entry.board) + QStringLiteral(" sent a web page instead of the spec. Open the board’s page, download the spec and choose it as a file."));
            return;
        }
        m_status.clear();
        m_progress = 0;
        bump();
        m_pods->loadSpecData(podId, data, displayName(entry), entry.url);      // named as the list names it
    });
    bump();
}
