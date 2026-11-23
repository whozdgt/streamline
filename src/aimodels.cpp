#include "aimodels.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace {
const char *kBase = "https://github.com/whozdgt/streamline/releases/download/ai-models/";
}

AiModels::AiModels(QObject *parent) : QObject(parent)
{
    m_models = {
        {QStringLiteral("meaning"), QStringLiteral("Meaning model"),
         QStringLiteral("Groups documents by what they mean, even when they share no words. Reads each document in full."),
         QStringLiteral("meaning-bge-small-en-v1.5-q8_0.gguf"),
         QStringLiteral("ec38e8da142596baa913124ae50550de284b6916bf59577ef2f0cb9660c2f514"), 36806944},
        {QStringLiteral("writer"), QStringLiteral("Writer model"),
         QStringLiteral("Reads the documents in each topic, then names the topic and sums it up in a sentence."),
         QStringLiteral("writer-qwen2.5-0.5b-instruct-q4_k_m.gguf"),
         QStringLiteral("74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db"), 491400032},
    };
}

bool AiModels::offered() const { return QSettings().value(QStringLiteral("ai/offered"), false).toBool(); }
void AiModels::setOffered(bool on)
{
    if (on == offered()) return;
    QSettings().setValue(QStringLiteral("ai/offered"), on);
    bump();
}

const AiModels::Model *AiModels::find(const QString &id) const
{
    for (const Model &m : m_models) if (m.id == id) return &m;
    return nullptr;
}

QString AiModels::dir() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models");
    QDir().mkpath(d);
    return d;
}

QString AiModels::path(const QString &id) const { return installed(id) ? dir() + QLatin1Char('/') + find(id)->file : QString(); }

bool AiModels::installed(const QString &id) const
{
    const Model *m = find(id);
    return m && !m_jobs.contains(id) && QFileInfo(dir() + QLatin1Char('/') + m->file).size() == m->size;
}

QString AiModels::sizeLabel(qint64 bytes) const
{
    if (bytes >= 1000LL * 1000 * 1000) return QString::number(double(bytes) / 1e9, 'f', 1) + QStringLiteral(" GB");
    return QString::number(qRound(double(bytes) / 1e6)) + QStringLiteral(" MB");
}

QVariantList AiModels::models() const
{
    QVariantList out;
    for (const Model &m : m_models) {
        const auto j = m_jobs.constFind(m.id);
        const bool dl = j != m_jobs.constEnd();
        out << QVariantMap{{"id", m.id}, {"name", m.name}, {"about", m.about}, {"size", double(m.size)},
                           {"sizeLabel", sizeLabel(m.size)}, {"installed", installed(m.id)}, {"downloading", dl},
                           {"checking", dl && j->checking},
                           {"progress", dl ? double(j->received) / double(m.size) : 0.0},
                           {"received", dl ? sizeLabel(j->received) : QString()}, {"error", m_errors.value(m.id)}};
    }
    return out;
}

void AiModels::download(const QString &id)
{
    const Model *m = find(id);
    if (!m || m_jobs.contains(id) || installed(id)) return;
    m_errors.remove(id);
    start(*m);
}

void AiModels::start(const Model &m)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    // A download that stopped half way carries on from where it got to.
    auto *file = new QFile(dir() + QLatin1Char('/') + m.file + QStringLiteral(".part"), this);
    if (!file->open(QIODevice::ReadWrite)) { delete file; fail(m.id, QStringLiteral("Couldn't save the download: is the device full?")); return; }
    qint64 have = file->size();
    if (have >= m.size) { file->resize(0); have = 0; }
    file->seek(have);
    QNetworkRequest req(QUrl(QString::fromLatin1(kBase) + m.file));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (have > 0) req.setRawHeader("Range", "bytes=" + QByteArray::number(have) + "-");
    QNetworkReply *reply = m_net->get(req);
    Job job;
    job.reply = reply;
    job.file = file;
    job.received = have;
    m_jobs.insert(m.id, job);
    const QString id = m.id;
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, id, reply]() {
        // The server started from the beginning after all: throw away what was there.
        auto it = m_jobs.find(id);
        if (it == m_jobs.end() || it->reply != reply) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 200 && it->received > 0) { it->file->resize(0); it->file->seek(0); it->received = 0; }
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply]() {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end() || it->reply != reply) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code != 200 && code != 206) return;
        const QByteArray data = reply->readAll();
        if (it->file->write(data) != data.size()) { reply->abort(); return; }
        const qint64 before = it->received;
        it->received += data.size();
        if (it->received / (1 << 20) != before / (1 << 20)) bump();      // a new megabyte: move the progress bar
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
        reply->deleteLater();
        auto it = m_jobs.find(id);
        if (it == m_jobs.end() || it->reply != reply) return;
        it->file->close();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::OperationCanceledError) return;     // cancel() tidies up
        if (reply->error() != QNetworkReply::NoError || (code != 200 && code != 206)) {
            fail(id, QStringLiteral("The download stopped (%1). Try again: it carries on from where it got to.")
                         .arg(reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("error %1").arg(code)));
            return;
        }
        verify(id);
    });
    bump();
}

void AiModels::verify(const QString &id)
{
    const Model *m = find(id);
    auto it = m_jobs.find(id);
    if (!m || it == m_jobs.end()) return;
    it->checking = true;
    bump();
    const QString part = it->file->fileName();
    const QString target = dir() + QLatin1Char('/') + m->file;
    const QString sha = m->sha256;
    const qint64 size = m->size;
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, id, part, target]() {
        const bool ok = w->result();
        w->deleteLater();
        auto it = m_jobs.find(id);
        if (it != m_jobs.end()) { it->file->deleteLater(); m_jobs.erase(it); }
        if (!ok) {
            QFile::remove(part);
            fail(id, QStringLiteral("The download was damaged on the way. Try again."));
            return;
        }
        QFile::remove(target);
        if (!QFile::rename(part, target)) { fail(id, QStringLiteral("Couldn't save the model.")); return; }
        bump();
    });
    w->setFuture(QtConcurrent::run([part, sha, size]() {
        QFile f(part);
        if (f.size() != size || !f.open(QIODevice::ReadOnly)) return false;
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!h.addData(&f)) return false;
        return h.result().toHex() == sha.toLatin1();
    }));
}

void AiModels::fail(const QString &id, const QString &error)
{
    auto it = m_jobs.find(id);
    if (it != m_jobs.end()) {
        if (it->file) it->file->deleteLater();
        m_jobs.erase(it);
    }
    m_errors.insert(id, error);
    bump();
}

void AiModels::cancel(const QString &id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || it->checking) return;
    QNetworkReply *r = it->reply;
    QFile *f = it->file;
    m_jobs.erase(it);
    if (r) r->abort();
    if (f) { f->close(); f->remove(); f->deleteLater(); }      // cancelling throws the half download away
    m_errors.remove(id);
    bump();
}

void AiModels::remove(const QString &id)
{
    const Model *m = find(id);
    if (!m || m_jobs.contains(id)) return;
    QFile::remove(dir() + QLatin1Char('/') + m->file);
    QFile::remove(dir() + QLatin1Char('/') + m->file + QStringLiteral(".part"));
    m_errors.remove(id);
    bump();
}
