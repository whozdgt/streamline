#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Account;
class QJsonDocument;

// The to-do list from Dayline, kept as the same records Dayline keeps (id, title, notes, due, color, flag, status…),
// saved in todos.json. Signed in, it syncs with the account's "dayline_items" rows exactly as Dayline does: pull what
// changed since last time, keep whichever copy was edited most recently, push what changed here. So the same to-dos
// show up in Dayline on a phone and in Streamline. Calendar events live in the same table and are left untouched.
class Todos : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncChanged)
    Q_PROPERTY(QString syncLine READ syncLine NOTIFY syncChanged)
public:
    explicit Todos(Account *account, QObject *parent = nullptr);
    int revision() const { return m_rev; }
    bool syncing() const { return m_syncing; }
    QString syncLine() const;

    // Each to-do is { id, title, notes, snippet, due, dueLabel, overdue, color, hex, flag, doing, done }.
    // filter: all | today | doing | overdue | flagged.   sort: manual | due | flag | color | az.
    Q_INVOKABLE QVariantList open(const QString &filter, const QString &sort, const QString &query, const QString &color) const;
    Q_INVOKABLE QVariantList done(const QString &filter, const QString &query, const QString &color) const;
    Q_INVOKABLE QVariantMap item(const QString &id) const;
    Q_INVOKABLE int attention() const;          // not done, and due today or earlier
    Q_INVOKABLE QVariantList colors() const;    // { key, name, hex }

    Q_INVOKABLE QString add(const QString &title, const QString &filter, const QString &color);
    Q_INVOKABLE void toggle(const QString &id);
    Q_INVOKABLE bool update(const QString &id, const QString &title, const QString &notes, const QString &due,
                            const QString &color, bool flag, bool doing);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE int clearDone();

    // dates are "YYYY-MM-DD"
    Q_INVOKABLE QString dayFromToday(int days) const;
    Q_INVOKABLE bool validDate(const QString &ymd) const;
    Q_INVOKABLE QString dueLabel(const QString &ymd) const;

    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void refresh();                 // the page came into view: sync if it has been a while

signals:
    void revisionChanged();
    void syncChanged();

private:
    int indexOf(const QString &id) const;
    QVariantMap view(const QJsonObject &o) const;
    bool matches(const QJsonObject &o, const QString &filter, const QString &query, const QString &color) const;
    QString filePath() const;
    void load();
    void save() const;
    void stamp(QJsonObject &o);                  // mark as edited now, and queue it for sync
    void changed();
    void accountChanged();
    void scheduleSync(int ms);
    void sync();
    void pull(int offset, const QString &uid);
    bool mergeRow(const QJsonObject &row);
    void push(const QString &uid);
    void pushChunk(int from, const QString &uid);
    void finish(bool ok, int status, const QJsonDocument &doc);

    Account *m_account;
    QVector<QJsonObject> m_todos;
    QHash<QString, qint64> m_gone;               // deleted here, not yet told to the server: id -> when
    QSet<QString> m_dirty;                        // ids to push
    QString m_uid, m_cursor;                      // the account these were last synced with, and how far
    QString m_lastColor = QStringLiteral("amber");
    QTimer m_timer, m_every;
    // one sync run
    QString m_since, m_newCursor, m_error;
    qint64 m_newCursorMs = 0, m_lastSync = 0;
    QJsonArray m_pushRows;
    QHash<QString, qint64> m_sent;
    bool m_syncing = false, m_again = false, m_pullChanged = false;
    int m_rev = 0;
};
