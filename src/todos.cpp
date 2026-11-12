#include "todos.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <iterator>

#include "account.h"

namespace {
struct Colour { const char *key, *name, *hex; };
const Colour kColours[] = {
    {"amber", "Amber", "#E5AE3F"}, {"teal", "Teal", "#3FC4D1"}, {"coral", "Coral", "#F0806A"}, {"violet", "Violet", "#9D8CFF"},
    {"green", "Green", "#5CC98A"}, {"blue", "Blue", "#4C9BFF"}, {"pink", "Pink", "#F27DB5"}, {"grey", "Grey", "#9AA0AE"}};

int colourIndex(const QString &key)
{
    for (int i = 0; i < int(std::size(kColours)); ++i)
        if (key == QLatin1String(kColours[i].key)) return i;
    return int(std::size(kColours)) - 1;         // anything unknown is drawn grey
}

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
QString today() { return QDate::currentDate().toString(Qt::ISODate); }
QString str(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); }
qint64 num(const QJsonObject &o, const char *k) { return qint64(o.value(QLatin1String(k)).toDouble()); }
bool isDone(const QJsonObject &o) { return str(o, "status") == QLatin1String("done"); }

// The server's timestamps look like 2026-11-12T22:08:01.123456+00:00. Milliseconds since 1970, or 0 if unreadable.
qint64 parseStamp(const QString &s)
{
    static const QRegularExpression re(QStringLiteral(
        "^(\\d{4})-(\\d\\d)-(\\d\\d)[T ](\\d\\d):(\\d\\d):(\\d\\d)(?:\\.(\\d+))?\\s*(Z|z|[+-]\\d\\d(?::?\\d\\d)?)?$"));
    const QRegularExpressionMatch m = re.match(s.trimmed());
    if (!m.hasMatch()) return 0;
    const QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    const QTime t(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt());
    if (!d.isValid() || !t.isValid()) return 0;
    qint64 ms = QDateTime(d, t, QTimeZone::UTC).toMSecsSinceEpoch();
    const QString frac = (m.captured(7) + QStringLiteral("000")).left(3);
    ms += frac.toInt();
    const QString tz = m.captured(8);
    if (tz.size() >= 3 && (tz[0] == QLatin1Char('+') || tz[0] == QLatin1Char('-'))) {
        const QString digits = QString(tz).remove(QLatin1Char(':'));          // +HH or +HHMM
        const qint64 off = (digits.mid(1, 2).toInt() * 60 + digits.mid(3, 2).toInt()) * 60000LL;
        ms += tz[0] == QLatin1Char('+') ? -off : off;
    }
    return ms;
}
} // namespace

Todos::Todos(Account *account, QObject *parent) : QObject(parent), m_account(account)
{
    load();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Todos::sync);
    m_every.setInterval(5 * 60 * 1000);          // picks up what was changed on another device
    connect(&m_every, &QTimer::timeout, this, [this]() { if (!m_uid.isEmpty()) sync(); });
    m_every.start();
    connect(m_account, &Account::changed, this, &Todos::accountChanged);
    const QString before = m_uid;
    accountChanged();
    if (!m_uid.isEmpty() && m_uid == before) scheduleSync(3000);
}

QString Todos::filePath() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + QStringLiteral("/todos.json");
}

void Todos::load()
{
    m_todos.clear(); m_gone.clear(); m_dirty.clear(); m_uid.clear(); m_cursor.clear();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value(QLatin1String("todos")).toArray()) {
        const QJsonObject o = v.toObject();
        if (!str(o, "id").isEmpty() && o.value(QLatin1String("title")).isString()) m_todos.push_back(o);
    }
    const QJsonObject gone = root.value(QLatin1String("gone")).toObject();
    for (auto it = gone.begin(); it != gone.end(); ++it) m_gone.insert(it.key(), qint64(it.value().toDouble()));
    for (const QJsonValue &v : root.value(QLatin1String("dirty")).toArray()) m_dirty.insert(v.toString());
    m_uid = root.value(QLatin1String("uid")).toString();
    m_cursor = root.value(QLatin1String("cursor")).toString();
    const QString lc = root.value(QLatin1String("lastColor")).toString();
    if (!lc.isEmpty()) m_lastColor = lc;
}

void Todos::save() const
{
    QJsonArray todos;
    for (const QJsonObject &o : m_todos) todos.append(o);
    QJsonObject gone;
    for (auto it = m_gone.begin(); it != m_gone.end(); ++it) gone.insert(it.key(), QJsonValue(it.value()));
    QJsonArray dirty;
    for (const QString &id : m_dirty) dirty.append(id);
    QJsonObject root;
    root.insert(QLatin1String("todos"), todos);
    root.insert(QLatin1String("gone"), gone);
    root.insert(QLatin1String("dirty"), dirty);
    root.insert(QLatin1String("uid"), m_uid);
    root.insert(QLatin1String("cursor"), m_cursor);
    root.insert(QLatin1String("lastColor"), m_lastColor);
    QSaveFile f(filePath());                     // a temporary file first, so a crash can't leave half a file
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

int Todos::indexOf(const QString &id) const
{
    for (int i = 0; i < m_todos.size(); ++i)
        if (str(m_todos[i], "id") == id) return i;
    return -1;
}

void Todos::stamp(QJsonObject &o)
{
    o.insert(QLatin1String("updated"), QJsonValue(now()));
    if (m_uid.isEmpty()) return;
    const QString id = str(o, "id");
    m_dirty.insert(id);
    m_gone.remove(id);
}

void Todos::changed()
{
    save();
    ++m_rev;
    emit revisionChanged();
    if (!m_uid.isEmpty() && !m_dirty.isEmpty()) scheduleSync(2500);
}

// ---------------------------------------------------------------- reading

QString Todos::dayFromToday(int days) const { return QDate::currentDate().addDays(days).toString(Qt::ISODate); }

bool Todos::validDate(const QString &ymd) const
{
    static const QRegularExpression re(QStringLiteral("^\\d{4}-\\d\\d-\\d\\d$"));
    return re.match(ymd).hasMatch() && QDate::fromString(ymd, Qt::ISODate).isValid();
}

QString Todos::dueLabel(const QString &ymd) const
{
    const QDate d = QDate::fromString(ymd, Qt::ISODate);
    if (!d.isValid()) return {};
    const QDate t = QDate::currentDate();
    if (d == t) return QStringLiteral("Today");
    if (d == t.addDays(1)) return QStringLiteral("Tomorrow");
    if (d == t.addDays(-1)) return QStringLiteral("Yesterday");
    return QLocale().toString(d, d.year() == t.year() ? QStringLiteral("ddd d MMM") : QStringLiteral("d MMM yyyy"));
}

QVariantMap Todos::view(const QJsonObject &o) const
{
    const QString due = str(o, "due"), status = str(o, "status"), notes = str(o, "notes");
    const bool done = status == QLatin1String("done");
    const QString flat = notes.simplified();
    QVariantMap m;
    m.insert(QStringLiteral("id"), str(o, "id"));
    m.insert(QStringLiteral("title"), str(o, "title"));
    m.insert(QStringLiteral("notes"), notes);
    m.insert(QStringLiteral("snippet"), flat.size() > 80 ? flat.left(80) + QChar(0x2026) : flat);
    m.insert(QStringLiteral("due"), due);
    m.insert(QStringLiteral("dueLabel"), dueLabel(due));
    m.insert(QStringLiteral("overdue"), !done && !due.isEmpty() && due < today());
    m.insert(QStringLiteral("color"), QString::fromLatin1(kColours[colourIndex(str(o, "color"))].key));
    m.insert(QStringLiteral("hex"), QString::fromLatin1(kColours[colourIndex(str(o, "color"))].hex));
    m.insert(QStringLiteral("flag"), o.value(QLatin1String("flag")).toBool());
    m.insert(QStringLiteral("doing"), status == QLatin1String("doing"));
    m.insert(QStringLiteral("done"), done);
    return m;
}

bool Todos::matches(const QJsonObject &o, const QString &filter, const QString &query, const QString &color) const
{
    if (!color.isEmpty() && QString::fromLatin1(kColours[colourIndex(str(o, "color"))].key) != color) return false;
    const QString q = query.trimmed().toLower();
    if (!q.isEmpty() && !(str(o, "title") + QLatin1Char(' ') + str(o, "notes")).toLower().contains(q)) return false;
    const QString due = str(o, "due");
    if (filter == QLatin1String("today")) return due == today();
    if (filter == QLatin1String("doing")) return str(o, "status") == QLatin1String("doing");
    if (filter == QLatin1String("overdue")) return !due.isEmpty() && due < today();
    if (filter == QLatin1String("flagged")) return o.value(QLatin1String("flag")).toBool();
    return true;
}

QVariantList Todos::open(const QString &filter, const QString &sort, const QString &query, const QString &color) const
{
    QVector<QJsonObject> list;
    for (const QJsonObject &o : m_todos)
        if (!isDone(o) && matches(o, filter, query, color)) list.push_back(o);
    if (sort == QLatin1String("due")) {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const QString x = str(a, "due"), y = str(b, "due");
            return (x.isEmpty() ? QStringLiteral("9") : x) < (y.isEmpty() ? QStringLiteral("9") : y);      // no date goes last
        });
    } else if (sort == QLatin1String("flag")) {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QLatin1String("flag")).toBool() && !b.value(QLatin1String("flag")).toBool();
        });
    } else if (sort == QLatin1String("color")) {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return colourIndex(str(a, "color")) < colourIndex(str(b, "color"));
        });
    } else if (sort == QLatin1String("az")) {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return QString::compare(str(a, "title"), str(b, "title"), Qt::CaseInsensitive) < 0;
        });
    } else {
        std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return num(a, "created") > num(b, "created");                                                   // newest first
        });
    }
    QVariantList out;
    for (const QJsonObject &o : list) out.append(view(o));
    return out;
}

QVariantList Todos::done(const QString &filter, const QString &query, const QString &color) const
{
    // "In progress" and "Overdue" can't hold finished to-dos, so there the Done list shows them all
    const QString f = filter == QLatin1String("doing") || filter == QLatin1String("overdue") ? QStringLiteral("all") : filter;
    QVector<QJsonObject> list;
    for (const QJsonObject &o : m_todos)
        if (isDone(o) && matches(o, f, query, color)) list.push_back(o);
    std::stable_sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) { return num(a, "doneAt") > num(b, "doneAt"); });
    QVariantList out;
    for (const QJsonObject &o : list) out.append(view(o));
    return out;
}

QVariantMap Todos::item(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? QVariantMap() : view(m_todos[i]);
}

int Todos::attention() const
{
    const QString t = today();
    int n = 0;
    for (const QJsonObject &o : m_todos) {
        const QString due = str(o, "due");
        if (!isDone(o) && !due.isEmpty() && due <= t) ++n;
    }
    return n;
}

QVariantList Todos::colors() const
{
    QVariantList out;
    for (const Colour &c : kColours) {
        QVariantMap m;
        m.insert(QStringLiteral("key"), QString::fromLatin1(c.key));
        m.insert(QStringLiteral("name"), QString::fromLatin1(c.name));
        m.insert(QStringLiteral("hex"), QString::fromLatin1(c.hex));
        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------- changing

QString Todos::add(const QString &title, const QString &filter, const QString &color)
{
    const QString t = title.simplified().left(300);
    if (t.isEmpty()) return {};
    QJsonObject o;
    o.insert(QLatin1String("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    o.insert(QLatin1String("title"), t);
    o.insert(QLatin1String("notes"), QString());
    o.insert(QLatin1String("due"), filter == QLatin1String("today") ? today() : QString());
    o.insert(QLatin1String("color"), color.isEmpty() ? m_lastColor : color);
    o.insert(QLatin1String("flag"), filter == QLatin1String("flagged"));
    o.insert(QLatin1String("status"), filter == QLatin1String("doing") ? QStringLiteral("doing") : QStringLiteral("todo"));
    o.insert(QLatin1String("created"), QJsonValue(now()));
    stamp(o);
    m_todos.push_front(o);
    changed();
    return str(o, "id");
}

void Todos::toggle(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) return;
    QJsonObject &o = m_todos[i];
    if (isDone(o)) {
        o.insert(QLatin1String("status"), QStringLiteral("todo"));
        o.remove(QLatin1String("doneAt"));
    } else {
        o.insert(QLatin1String("status"), QStringLiteral("done"));
        o.insert(QLatin1String("doneAt"), QJsonValue(now()));
    }
    stamp(o);
    changed();
}

bool Todos::update(const QString &id, const QString &title, const QString &notes, const QString &due,
                   const QString &color, bool flag, bool doing)
{
    const int i = indexOf(id);
    const QString t = title.simplified().left(300);
    if (i < 0 || t.isEmpty() || (!due.isEmpty() && !validDate(due))) return false;
    QJsonObject o = m_todos[i];
    o.insert(QLatin1String("title"), t);
    o.insert(QLatin1String("notes"), notes.trimmed().left(2000));
    o.insert(QLatin1String("due"), due);
    o.insert(QLatin1String("color"), color.isEmpty() ? QStringLiteral("amber") : color);
    o.insert(QLatin1String("flag"), flag);
    o.insert(QLatin1String("status"), doing ? QStringLiteral("doing") : isDone(o) ? QStringLiteral("done") : QStringLiteral("todo"));
    if (doing) o.remove(QLatin1String("doneAt"));
    if (!color.isEmpty()) m_lastColor = color;
    if (o == m_todos[i]) { save(); return true; }           // nothing changed: don't send it anywhere
    stamp(o);
    m_todos[i] = o;
    changed();
    return true;
}

void Todos::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0) return;
    m_todos.remove(i);
    if (!m_uid.isEmpty()) { m_gone.insert(id, now()); m_dirty.insert(id); }
    changed();
}

int Todos::clearDone()
{
    int n = 0;
    const qint64 t = now();
    for (int i = m_todos.size() - 1; i >= 0; --i) {
        if (!isDone(m_todos[i])) continue;
        const QString id = str(m_todos[i], "id");
        m_todos.remove(i);
        if (!m_uid.isEmpty()) { m_gone.insert(id, t); m_dirty.insert(id); }
        ++n;
    }
    if (n > 0) changed();
    return n;
}

// ---------------------------------------------------------------- account and sync

// Signing in sends everything on this device into the account on the first sync (as Dayline does).
// Signing out keeps the to-dos here and forgets where the sync had got to.
void Todos::accountChanged()
{
    const QString uid = m_account->signedIn() ? m_account->uid() : QString();
    if (uid == m_uid) return;
    m_uid = uid;
    m_cursor.clear();
    m_gone.clear();
    m_dirty.clear();
    m_error.clear();
    m_lastSync = 0;
    m_timer.stop();
    if (!m_uid.isEmpty()) {
        for (const QJsonObject &o : m_todos) m_dirty.insert(str(o, "id"));
        scheduleSync(1500);
    }
    save();
    emit syncChanged();
}

QString Todos::syncLine() const
{
    if (m_uid.isEmpty()) return {};
    if (m_syncing) return QStringLiteral("Syncing…");
    if (!m_error.isEmpty()) return m_error;
    if (m_lastSync > 0) return QStringLiteral("Synced at %1").arg(QLocale().toString(QDateTime::fromMSecsSinceEpoch(m_lastSync).time(), QLocale::ShortFormat));
    return QStringLiteral("Not synced yet");
}

void Todos::scheduleSync(int ms)
{
    if (!m_uid.isEmpty()) m_timer.start(ms);
}

void Todos::syncNow() { sync(); }

void Todos::refresh()
{
    if (!m_uid.isEmpty() && !m_syncing && now() - m_lastSync > 60000) sync();
}

void Todos::sync()
{
    m_timer.stop();
    if (m_uid.isEmpty()) return;
    if (m_syncing) { m_again = true; return; }
    m_syncing = true;
    m_pullChanged = false;
    m_newCursor = m_cursor;
    m_newCursorMs = parseStamp(m_cursor);
    // everything changed since last time, with a few seconds of overlap in case two devices wrote at once
    m_since = m_newCursorMs > 0
        ? QDateTime::fromMSecsSinceEpoch(m_newCursorMs - 10000, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"))
        : QString();
    emit syncChanged();
    pull(0, m_uid);
}

void Todos::pull(int offset, const QString &uid)
{
    QString path = QStringLiteral("/rest/v1/dayline_items?select=id,kind,data,deleted,updated_at&order=updated_at.asc,id.asc&limit=500&offset=%1").arg(offset);
    if (!m_since.isEmpty()) path += QStringLiteral("&updated_at=gt.") + QString::fromLatin1(QUrl::toPercentEncoding(m_since));
    m_account->call("GET", path, QJsonDocument(), [this, offset, uid](bool ok, const QJsonDocument &d, int status) {
        if (uid != m_uid) { m_syncing = false; m_again = false; emit syncChanged(); return; }      // signed out or switched meanwhile
        if (!ok || !d.isArray()) { finish(false, ok ? -1 : status, d); return; }
        const QJsonArray rows = d.array();
        for (const QJsonValue &v : rows) {
            const QJsonObject row = v.toObject();
            if (mergeRow(row)) m_pullChanged = true;
            const QString at = row.value(QLatin1String("updated_at")).toString();
            const qint64 ms = parseStamp(at);
            if (ms > m_newCursorMs) { m_newCursorMs = ms; m_newCursor = at; }
        }
        if (rows.size() >= 500) pull(offset + 500, uid);
        else push(uid);
    });
}

// True when the row changed what is on this device.
bool Todos::mergeRow(const QJsonObject &row)
{
    if (row.value(QLatin1String("kind")).toString() != QLatin1String("todo")) return false;      // calendar events: not ours to touch
    const QString id = row.value(QLatin1String("id")).toString();
    const QJsonObject data = row.value(QLatin1String("data")).toObject();
    const qint64 ru = num(data, "updated");
    const int i = indexOf(id);
    const bool gone = m_gone.contains(id);
    if (row.value(QLatin1String("deleted")).toBool()) {
        if (i >= 0 && ru >= num(m_todos[i], "updated")) { m_todos.remove(i); m_dirty.remove(id); return true; }
        if (gone && ru >= m_gone.value(id)) { m_gone.remove(id); m_dirty.remove(id); }
        return false;
    }
    if (str(data, "id") != id || !data.value(QLatin1String("title")).isString()) return false;
    if (i >= 0 ? ru <= num(m_todos[i], "updated") : (gone && ru <= m_gone.value(id))) return false;     // ours is newer
    if (i >= 0) m_todos[i] = data; else m_todos.push_back(data);
    m_gone.remove(id);
    m_dirty.remove(id);
    return true;
}

void Todos::push(const QString &uid)
{
    m_pushRows = QJsonArray();
    m_sent.clear();
    const QSet<QString> dirty = m_dirty;
    for (const QString &id : dirty) {
        const int i = indexOf(id);
        QJsonObject row;
        row.insert(QLatin1String("user_id"), uid);
        row.insert(QLatin1String("id"), id);
        row.insert(QLatin1String("kind"), QStringLiteral("todo"));
        if (i >= 0) {
            row.insert(QLatin1String("data"), m_todos[i]);
            row.insert(QLatin1String("deleted"), false);
            m_sent.insert(id, num(m_todos[i], "updated"));
        } else if (m_gone.contains(id)) {
            row.insert(QLatin1String("data"), QJsonObject{{QStringLiteral("updated"), QJsonValue(m_gone.value(id))}});
            row.insert(QLatin1String("deleted"), true);
            m_sent.insert(id, m_gone.value(id));
        } else {
            m_dirty.remove(id);
            continue;
        }
        m_pushRows.append(row);
    }
    pushChunk(0, uid);
}

void Todos::pushChunk(int from, const QString &uid)
{
    if (from >= m_pushRows.size()) {
        // anything edited again while the request was on its way stays queued
        for (auto it = m_sent.begin(); it != m_sent.end(); ++it) {
            const int i = indexOf(it.key());
            const qint64 cur = i >= 0 ? num(m_todos[i], "updated") : m_gone.value(it.key(), it.value());
            if (cur != it.value()) continue;
            m_dirty.remove(it.key());
            if (i < 0) m_gone.remove(it.key());
        }
        m_cursor = m_newCursor;
        finish(true, 200, QJsonDocument());
        return;
    }
    QJsonArray chunk;
    for (int i = from; i < m_pushRows.size() && i < from + 200; ++i) chunk.append(m_pushRows.at(i));
    m_account->call("POST", QStringLiteral("/rest/v1/dayline_items?on_conflict=user_id,id"), QJsonDocument(chunk),
        [this, from, uid](bool ok, const QJsonDocument &d, int status) {
            if (uid != m_uid) { m_syncing = false; m_again = false; emit syncChanged(); return; }
            if (!ok) { finish(false, status, d); return; }
            pushChunk(from + 200, uid);
        }, {{"Prefer", "resolution=merge-duplicates,return=minimal"}});
}

void Todos::finish(bool ok, int status, const QJsonDocument &doc)
{
    m_syncing = false;
    m_pushRows = QJsonArray();
    m_sent.clear();
    if (ok) {
        m_error.clear();
        m_lastSync = now();
    } else {
        const QString code = doc.object().value(QLatin1String("code")).toString();
        m_error = status == 0 ? QStringLiteral("Offline. Your changes will sync when you're back online.")
                : status == 404 || code == QLatin1String("PGRST205") || code == QLatin1String("42P01")
                    ? QStringLiteral("Sync isn't switched on for this account's server yet.")
                : status == 401 ? QString()
                : QStringLiteral("Sync didn't finish (error %1). It will try again.").arg(status);
    }
    save();
    if (m_pullChanged) { m_pullChanged = false; ++m_rev; emit revisionChanged(); }
    emit syncChanged();
    if (m_again) { m_again = false; scheduleSync(500); }
}
