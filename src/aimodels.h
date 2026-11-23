#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// The free AI models Pods can use, downloaded when you choose to (the welcome screen, or Pods › On-device AI) from
// the app's "ai-models" release, checked against a known checksum, and kept in the app's data folder.
//   meaning: BGE small (BAAI, MIT licence), 37 MB: groups documents by what they mean
//   writer:  Qwen2.5 0.5B Instruct (Alibaba, Apache 2.0 licence), 491 MB: names topics and sums them up
class AiModels : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool offered READ offered WRITE setOffered NOTIFY changed)      // the welcome screen has been shown
    Q_PROPERTY(bool busy READ busy NOTIFY changed)                             // something is downloading
public:
    explicit AiModels(QObject *parent = nullptr);
    int revision() const { return m_rev; }
    bool offered() const;
    void setOffered(bool on);
    bool busy() const { return !m_jobs.isEmpty(); }

    // [{ id, name, about, size (bytes), sizeLabel, installed, downloading, received, progress (0..1), error }]
    Q_INVOKABLE QVariantList models() const;
    Q_INVOKABLE bool installed(const QString &id) const;
    Q_INVOKABLE void download(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE QString sizeLabel(qint64 bytes) const;
    QString path(const QString &id) const;           // where an installed model is ("" when it isn't)

signals:
    void changed();

private:
    struct Model { QString id, name, about, file, sha256; qint64 size; };
    struct Job { QPointer<QNetworkReply> reply; QFile *file = nullptr; qint64 received = 0; bool checking = false; };
    const Model *find(const QString &id) const;
    QString dir() const;
    void start(const Model &m);
    void verify(const QString &id);
    void fail(const QString &id, const QString &error);
    void bump() { ++m_rev; emit changed(); }

    QVector<Model> m_models;
    QHash<QString, Job> m_jobs;
    QHash<QString, QString> m_errors;
    QNetworkAccessManager *m_net = nullptr;
    int m_rev = 0;
};
