#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Flashcard decks with spaced repetition, saved as JSON in the app's data folder (flashcards.json).
// A card you rate "Again" comes back in the same session; the other ratings push it out by a growing number of days.
class Flashcards : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    explicit Flashcards(QObject *parent = nullptr);
    int revision() const { return m_rev; }

    // decks: each is { id, name, total, due, fresh, todo } (todo = due + fresh: what "Study" would show today)
    Q_INVOKABLE QVariantList decks() const;
    Q_INVOKABLE QVariantMap deck(const QString &id) const;
    Q_INVOKABLE int todoTotal() const;
    Q_INVOKABLE QString createDeck(const QString &name);
    Q_INVOKABLE QString createSampleDeck();
    Q_INVOKABLE void renameDeck(const QString &id, const QString &name);
    Q_INVOKABLE void removeDeck(const QString &id);

    // cards: each is { id, front, back, fresh } (fresh = never studied)
    Q_INVOKABLE QVariantList cards(const QString &deckId) const;
    Q_INVOKABLE QVariantMap card(const QString &deckId, const QString &cardId) const;
    Q_INVOKABLE bool addCard(const QString &deckId, const QString &front, const QString &back);
    Q_INVOKABLE void updateCard(const QString &deckId, const QString &cardId, const QString &front, const QString &back);
    Q_INVOKABLE void removeCard(const QString &deckId, const QString &cardId);

    // One card per line: "question ; answer", or two columns copied from a spreadsheet (tab between them).
    Q_INVOKABLE int importText(const QString &deckId, const QString &text);
    Q_INVOKABLE QString exportText(const QString &deckId) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE QString clipboardText() const;

    // studying
    Q_INVOKABLE QVariantList studyQueue(const QString &deckId, bool all) const;      // shuffled card ids
    Q_INVOKABLE void rate(const QString &deckId, const QString &cardId, const QString &rating);   // again | hard | good | easy
    Q_INVOKABLE QString intervalLabel(const QString &deckId, const QString &cardId, const QString &rating) const;
    Q_INVOKABLE QString nextDueLabel(const QString &deckId) const;

signals:
    void revisionChanged();

private:
    struct Card {
        QString id, front, back;
        qint64 due = 0;          // ms since 1970; 0 = never studied
        int ivl = 0;             // days until it returns
        double ease = 2.5;
        int reps = 0;
        qint64 updated = 0;
    };
    struct Deck { QString id, name; qint64 created = 0, updated = 0; QVector<Card> cards; };
    struct Next { int ivl; double ease; int reps; };

    Deck *find(const QString &id);
    const Deck *find(const QString &id) const;
    static Card *findCard(Deck *d, const QString &id);
    static Next schedule(const Card &c, const QString &rating);
    static QString daysLabel(int days);
    static QVariantMap summary(const Deck &d);
    QString filePath() const;
    void load();
    void save() const;
    void changed() { save(); ++m_rev; emit revisionChanged(); }

    QVector<Deck> m_decks;
    int m_rev = 0;
};
