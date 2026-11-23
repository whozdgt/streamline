#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVector>

class Pods;
class QNetworkAccessManager;
class QNetworkReply;

// The exam boards' specifications Streamline can find for you (specs/catalog.json: Cambridge, AQA, Pearson Edexcel
// and OCR). The list holds only each board's own link; choosing a spec downloads it from the board's website, reads
// it on this device and matches the pod to it. The list is built into the app, and a newer one is picked up from the
// app's repository when the finder opens, so a document a board moves is found again without an update.
class SpecCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)              // downloading a spec
    Q_PROPERTY(double progress READ progress NOTIFY changed)    // 0..1, or -1 when the size isn't known
    Q_PROPERTY(QString status READ status NOTIFY changed)       // "Downloading from AQA's website…"
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString podId READ podId NOTIFY changed)         // the pod the download is for
    Q_PROPERTY(int count READ count NOTIFY changed)
public:
    explicit SpecCatalog(Pods *pods, QObject *parent = nullptr);
    int revision() const { return m_rev; }
    bool busy() const { return !m_reply.isNull(); }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString podId() const { return m_podId; }
    int count() const { return int(m_entries.size()); }

    // [{ id, name, board, level, subject, code, exams, site, page }], best first. stage: "" | "gcse" | "alevel"
    Q_INVOKABLE QVariantList find(const QString &query, const QString &board, const QString &stage) const;
    Q_INVOKABLE void use(const QString &podId, const QString &id);      // download it, read it, match the pod to it
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void update();          // picks up a newer list from the app's repository (once a run)

    struct Entry { QString id, board, level, subject, code, exams, url, page; };
    // Reads a list (catalog.json); only links to the boards' own websites are kept. Returns its version, or 0.
    static int parse(const QByteArray &json, QVector<Entry> &out);
    static bool boardSite(const QString &url);
    static QString displayName(const Entry &e);

signals:
    void changed();

private:
    void load();
    void fail(const QString &message);
    void bump() { ++m_rev; emit changed(); }
    QString cachePath() const;

    QPointer<Pods> m_pods;
    QVector<Entry> m_entries;
    int m_version = 0;
    int m_rev = 0;
    bool m_updated = false;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    double m_progress = 0;
    QString m_status, m_error, m_podId;
};
