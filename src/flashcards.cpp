#include "flashcards.h"

#include <QClipboard>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTime>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
const qint64 kDay = qint64(24) * 3600 * 1000;

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
qint64 endOfToday() { return QDateTime(QDate::currentDate(), QTime(23, 59, 59, 999)).toMSecsSinceEpoch(); }
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString oneLine(QString s) { return s.replace(QLatin1Char('\t'), QLatin1Char(' ')).simplified(); }
} // namespace

Flashcards::Flashcards(QObject *parent) : QObject(parent) { load(); }

QString Flashcards::filePath() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + QStringLiteral("/flashcards.json");
}

void Flashcards::load()
{
    m_decks.clear();
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).object().value(QLatin1String("decks")).toArray();
    for (const QJsonValue &dv : arr) {
        const QJsonObject o = dv.toObject();
        Deck d;
        d.id = o.value(QLatin1String("id")).toString();
        d.name = o.value(QLatin1String("name")).toString();
        d.created = qint64(o.value(QLatin1String("created")).toDouble());
        d.updated = qint64(o.value(QLatin1String("updated")).toDouble());
        if (d.id.isEmpty()) continue;
        for (const QJsonValue &cv : o.value(QLatin1String("cards")).toArray()) {
            const QJsonObject co = cv.toObject();
            Card c;
            c.id = co.value(QLatin1String("id")).toString();
            c.front = co.value(QLatin1String("front")).toString();
            c.back = co.value(QLatin1String("back")).toString();
            c.due = qint64(co.value(QLatin1String("due")).toDouble());
            c.ivl = co.value(QLatin1String("ivl")).toInt();
            c.ease = co.value(QLatin1String("ease")).toDouble(2.5);
            c.reps = co.value(QLatin1String("reps")).toInt();
            c.updated = qint64(co.value(QLatin1String("updated")).toDouble());
            if (!c.id.isEmpty()) d.cards.push_back(c);
        }
        m_decks.push_back(d);
    }
}

void Flashcards::save() const
{
    QJsonArray arr;
    for (const Deck &d : m_decks) {
        QJsonArray cards;
        for (const Card &c : d.cards) {
            QJsonObject co;
            co.insert(QLatin1String("id"), c.id);
            co.insert(QLatin1String("front"), c.front);
            co.insert(QLatin1String("back"), c.back);
            co.insert(QLatin1String("due"), double(c.due));
            co.insert(QLatin1String("ivl"), c.ivl);
            co.insert(QLatin1String("ease"), c.ease);
            co.insert(QLatin1String("reps"), c.reps);
            co.insert(QLatin1String("updated"), double(c.updated));
            cards.append(co);
        }
        QJsonObject o;
        o.insert(QLatin1String("id"), d.id);
        o.insert(QLatin1String("name"), d.name);
        o.insert(QLatin1String("created"), double(d.created));
        o.insert(QLatin1String("updated"), double(d.updated));
        o.insert(QLatin1String("cards"), cards);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QLatin1String("decks"), arr);
    // written to a temporary file first, so a crash or a full disk can't leave half a file behind
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

Flashcards::Deck *Flashcards::find(const QString &id)
{
    for (Deck &d : m_decks) if (d.id == id) return &d;
    return nullptr;
}

const Flashcards::Deck *Flashcards::find(const QString &id) const
{
    for (const Deck &d : m_decks) if (d.id == id) return &d;
    return nullptr;
}

Flashcards::Card *Flashcards::findCard(Deck *d, const QString &id)
{
    if (!d) return nullptr;
    for (Card &c : d->cards) if (c.id == id) return &c;
    return nullptr;
}

QVariantMap Flashcards::summary(const Deck &d)
{
    const qint64 end = endOfToday();
    int due = 0, fresh = 0;
    for (const Card &c : d.cards) {
        if (c.due == 0) ++fresh;
        else if (c.due <= end) ++due;
    }
    QVariantMap m;
    m.insert(QStringLiteral("id"), d.id);
    m.insert(QStringLiteral("name"), d.name);
    m.insert(QStringLiteral("total"), int(d.cards.size()));
    m.insert(QStringLiteral("due"), due);
    m.insert(QStringLiteral("fresh"), fresh);
    m.insert(QStringLiteral("todo"), due + fresh);
    return m;
}

QVariantList Flashcards::decks() const
{
    QVariantList out;
    for (const Deck &d : m_decks) out.append(summary(d));
    return out;
}

QVariantMap Flashcards::deck(const QString &id) const
{
    const Deck *d = find(id);
    return d ? summary(*d) : QVariantMap();
}

int Flashcards::todoTotal() const
{
    int n = 0;
    for (const Deck &d : m_decks) n += summary(d).value(QStringLiteral("todo")).toInt();
    return n;
}

QString Flashcards::createDeck(const QString &name)
{
    const QString n = name.simplified();
    if (n.isEmpty()) return {};
    Deck d;
    d.id = newId();
    d.name = n.left(80);
    d.created = d.updated = now();
    m_decks.push_back(d);
    changed();
    return d.id;
}

QString Flashcards::createSampleDeck()
{
    static const char *const pairs[][2] = {
        {"What is the function of the mitochondria?", "Producing ATP through aerobic respiration"},
        {"Which organelle is the site of protein synthesis?", "The ribosome"},
        {"What does the Golgi apparatus do?", "Modifies, sorts and packages proteins for transport"},
        {"What is osmosis?", "Net movement of water across a partially permeable membrane, from high to low water potential"},
        {"Name the four bases in DNA", "Adenine, thymine, cytosine, guanine"},
        {"What happens during the S phase of the cell cycle?", "DNA replication"},
    };
    Deck d;
    d.id = newId();
    d.name = QStringLiteral("Example: Cell Biology");
    d.created = d.updated = now();
    for (const auto &p : pairs) {
        Card c;
        c.id = newId();
        c.front = QString::fromUtf8(p[0]);
        c.back = QString::fromUtf8(p[1]);
        c.updated = d.created;
        d.cards.push_back(c);
    }
    m_decks.push_back(d);
    changed();
    return d.id;
}

void Flashcards::renameDeck(const QString &id, const QString &name)
{
    Deck *d = find(id);
    const QString n = name.simplified().left(80);
    if (!d || n.isEmpty() || d->name == n) return;
    d->name = n;
    d->updated = now();
    changed();
}

void Flashcards::removeDeck(const QString &id)
{
    const auto it = std::find_if(m_decks.begin(), m_decks.end(), [&](const Deck &d) { return d.id == id; });
    if (it == m_decks.end()) return;
    m_decks.erase(it);
    changed();
}

QVariantList Flashcards::cards(const QString &deckId) const
{
    QVariantList out;
    const Deck *d = find(deckId);
    if (!d) return out;
    for (const Card &c : d->cards) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), c.id);
        m.insert(QStringLiteral("front"), c.front);
        m.insert(QStringLiteral("back"), c.back);
        m.insert(QStringLiteral("fresh"), c.due == 0);
        out.append(m);
    }
    return out;
}

QVariantMap Flashcards::card(const QString &deckId, const QString &cardId) const
{
    QVariantMap m;
    const Deck *d = find(deckId);
    if (!d) return m;
    for (const Card &c : d->cards) {
        if (c.id != cardId) continue;
        m.insert(QStringLiteral("id"), c.id);
        m.insert(QStringLiteral("front"), c.front);
        m.insert(QStringLiteral("back"), c.back);
        m.insert(QStringLiteral("fresh"), c.due == 0);
        break;
    }
    return m;
}

bool Flashcards::addCard(const QString &deckId, const QString &front, const QString &back)
{
    Deck *d = find(deckId);
    const QString f = front.trimmed(), b = back.trimmed();
    if (!d || f.isEmpty() || b.isEmpty()) return false;
    Card c;
    c.id = newId();
    c.front = f;
    c.back = b;
    c.updated = now();
    d->cards.push_back(c);
    d->updated = c.updated;
    changed();
    return true;
}

void Flashcards::updateCard(const QString &deckId, const QString &cardId, const QString &front, const QString &back)
{
    Deck *d = find(deckId);
    Card *c = findCard(d, cardId);
    const QString f = front.trimmed(), b = back.trimmed();
    if (!c || f.isEmpty() || b.isEmpty()) return;
    c->front = f;
    c->back = b;
    c->updated = d->updated = now();
    changed();
}

void Flashcards::removeCard(const QString &deckId, const QString &cardId)
{
    Deck *d = find(deckId);
    if (!d) return;
    const auto it = std::find_if(d->cards.begin(), d->cards.end(), [&](const Card &c) { return c.id == cardId; });
    if (it == d->cards.end()) return;
    d->cards.erase(it);
    d->updated = now();
    changed();
}

int Flashcards::importText(const QString &deckId, const QString &text)
{
    Deck *d = find(deckId);
    if (!d) return 0;
    static const QString seps[] = {QStringLiteral("\t"), QStringLiteral(";"), QStringLiteral(" - ")};
    int added = 0;
    const qint64 t = now();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.trimmed().isEmpty()) continue;
        for (const QString &sep : seps) {
            const qsizetype at = line.indexOf(sep);
            if (at <= 0) continue;
            const QString f = line.left(at).trimmed(), b = line.mid(at + sep.size()).trimmed();
            if (f.isEmpty() || b.isEmpty()) continue;
            Card c;
            c.id = newId();
            c.front = f;
            c.back = b;
            c.updated = t;
            d->cards.push_back(c);
            ++added;
            break;
        }
    }
    if (added > 0) { d->updated = t; changed(); }
    return added;
}

QString Flashcards::exportText(const QString &deckId) const
{
    const Deck *d = find(deckId);
    if (!d) return {};
    QStringList lines;
    for (const Card &c : d->cards) lines << oneLine(c.front) + QLatin1Char('\t') + oneLine(c.back);
    return lines.join(QLatin1Char('\n'));
}

void Flashcards::copyText(const QString &text) const
{
    if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(text);
}

QString Flashcards::clipboardText() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    return cb ? cb->text() : QString();
}

QVariantList Flashcards::studyQueue(const QString &deckId, bool all) const
{
    QVariantList out;
    const Deck *d = find(deckId);
    if (!d) return out;
    const qint64 end = endOfToday();
    QVector<QString> ids;
    for (const Card &c : d->cards)
        if (all || c.due <= end) ids.push_back(c.id);
    for (qsizetype i = ids.size() - 1; i > 0; --i)
        std::swap(ids[i], ids[QRandomGenerator::global()->bounded(quint32(i + 1))]);
    for (const QString &id : ids) out.append(id);
    return out;
}

Flashcards::Next Flashcards::schedule(const Card &c, const QString &rating)
{
    Next n{c.ivl, c.ease, c.reps};
    if (rating == QLatin1String("again")) {
        n.ivl = 0;
        n.reps = 0;
        n.ease = std::max(1.3, n.ease - 0.2);
    } else if (rating == QLatin1String("hard")) {
        n.ivl = std::max(1, int(std::lround(c.ivl * 1.2)));
        n.ease = std::max(1.3, n.ease - 0.15);
        n.reps += 1;
    } else if (rating == QLatin1String("easy")) {
        n.ivl = c.reps == 0 ? 4 : std::max(c.ivl + 2, int(std::lround(c.ivl * c.ease * 1.3)));
        n.ease += 0.15;
        n.reps += 1;
    } else {
        n.ivl = c.reps == 0 ? 1 : c.reps == 1 ? 3 : std::max(c.ivl + 1, int(std::lround(c.ivl * c.ease)));
        n.reps += 1;
    }
    return n;
}

void Flashcards::rate(const QString &deckId, const QString &cardId, const QString &rating)
{
    Deck *d = find(deckId);
    Card *c = findCard(d, cardId);
    if (!c) return;
    const Next n = schedule(*c, rating);
    c->ivl = n.ivl;
    c->ease = n.ease;
    c->reps = n.reps;
    c->due = now() + qint64(n.ivl) * kDay;
    c->updated = d->updated = now();
    changed();
}

QString Flashcards::daysLabel(int days)
{
    if (days <= 0) return QStringLiteral("again today");
    if (days == 1) return QStringLiteral("1 day");
    if (days < 30) return QStringLiteral("%1 days").arg(days);
    if (days < 365) return QStringLiteral("%1 mo").arg(int(std::lround(days / 30.0)));
    return QStringLiteral("%1 yr").arg(QString::number(days / 365.0, 'f', 1));
}

QString Flashcards::intervalLabel(const QString &deckId, const QString &cardId, const QString &rating) const
{
    const Deck *d = find(deckId);
    if (!d) return {};
    for (const Card &c : d->cards)
        if (c.id == cardId) return daysLabel(schedule(c, rating).ivl);
    return {};
}

// When the next card that isn't due today comes back, e.g. "3 days". Empty when every card is still due.
QString Flashcards::nextDueLabel(const QString &deckId) const
{
    const Deck *d = find(deckId);
    if (!d) return {};
    const qint64 end = endOfToday();
    qint64 soonest = 0;
    for (const Card &c : d->cards)
        if (c.due > end && (soonest == 0 || c.due < soonest)) soonest = c.due;
    if (soonest == 0) return {};
    return daysLabel(std::max(1, int(std::lround(double(soonest - now()) / double(kDay)))));
}
