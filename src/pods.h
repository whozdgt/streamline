#pragma once
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class AiModels;

// Pods: pick a folder of documents and Streamline sorts them into topics.
// Sorting happens on this device: by key words (podsort.h), or by the free on-device AI once its models are
// downloaded (podai.h, aimodels.h).
// The topics live in the app (pods.json in the app's data folder); your files stay where they are unless you
// choose "Move files into topic folders", which can be undone.
class Pods : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    explicit Pods(AiModels *models, QObject *parent = nullptr);
    int revision() const { return m_rev; }

    // pods: each is { id, name, folder, docs, topics, busy, status }
    Q_INVOKABLE QVariantList pods() const;
    // one pod: { id, name, folder, detail, method, sortedLabel, busy, status, error, moved, canMove,
    //            topics: [{ id, name, summary, keywords, other, docs: [{ path, name, ext, snippet, words, readable, folder }] }] }
    Q_INVOKABLE QVariantMap pod(const QString &id, const QString &query = QString()) const;
    Q_INVOKABLE QVariantList topicChoices(const QString &podId) const;     // [{ id, name }] for "Move to topic"

    Q_INVOKABLE QString create(const QString &folder);      // starts sorting; returns the new pod's id
    Q_INVOKABLE void remove(const QString &id);             // forgets the pod; never touches the files
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void resort(const QString &id, int detail);  // 0 broad, 1 balanced, 2 specific; uses the AI when downloaded
    // How a pod is laid out: "topics" (one level), "nested" (Subjects › Topics › Subtopics) or "spec" (matched to a
    // specification's points). Takes effect on the next sort.
    Q_INVOKABLE void setLayout(const QString &id, const QString &layout);
    Q_INVOKABLE QVariantMap setSpec(const QString &id, const QString &name, const QString &text);   // { points } or { error }
    Q_INVOKABLE QVariantMap readSpecFile(const QString &url) const;   // { name, text, board } or { error }: PDF, Word, OpenDocument, text
    static QVariantMap specFromData(const QByteArray &data, const QString &fileName);
    // Reads a spec (in the background) and matches the pod to it; specLoaded() says how it went. A file chosen on
    // this device, or a spec downloaded from its board (source: where from; name: what the list calls it).
    Q_INVOKABLE void loadSpecFile(const QString &id, const QString &url);
    void loadSpecData(const QString &id, const QByteArray &data, const QString &name, const QString &source = QString());
    Q_INVOKABLE void refresh(const QString &id);             // picks up new and deleted files, keeps your topics
    Q_INVOKABLE void renameTopic(const QString &podId, const QString &topicId, const QString &name);
    Q_INVOKABLE void moveDoc(const QString &podId, const QString &path, const QString &topicId);   // topicId "new" makes a topic
    Q_INVOKABLE QVariantMap moveFiles(const QString &id);    // { moved, failed, folders }
    Q_INVOKABLE QVariantMap undoMove(const QString &id);     // { moved, failed }
    Q_INVOKABLE QVariantMap movePlan(const QString &id) const;   // { files, folders, names } what moveFiles would do

    Q_INVOKABLE QString docText(const QString &path) const;  // the document's text, for a preview
    Q_INVOKABLE bool openDoc(const QString &path) const;
    Q_INVOKABLE int docCount(const QString &dir) const;      // documents directly in dir (the folder chooser shows it)


    // Android 11+: reading documents (not just music) needs "All files access"
    Q_INVOKABLE bool filesAllowed() const;
    Q_INVOKABLE void askFiles() const;

    struct Scanned {
        QString path, rel, name, ext, title, headings, body;
        qint64 modified = 0;
        int words = 0;
        bool readable = false;
    };

signals:
    void revisionChanged();
    void specLoaded(const QString &podId, const QVariantMap &result);     // { name, points } or { error }

private:
    struct Doc {
        QString path, name, ext, folder, topic, snippet;
        int words = 0;
        bool readable = false;
    };
    struct Topic {
        QString id, name, summary, number;      // number: a spec point's ("3.2.1")
        QStringList keywords;
        QString parent;                          // the topic it is inside ("" at the top)
        bool other = false;
    };
    struct Move { QString from, to; };
    struct Pod {
        QString id, name, folder, method = QStringLiteral("words");
        QString layout = QStringLiteral("nested");     // topics | nested (Subjects › Topics › Subtopics) | spec
        QString specName, specText;                    // layout "spec": the specification documents are matched to
        QString specSource;                            // where it was downloaded from ("" for a file or pasted text)
        mutable int specPoints = -1;                   // how many points it has (worked out when first asked)
        bool specBusy = false;                         // reading a spec (not saved)
        bool sortAgain = false;                        // a spec arrived while sorting: sort again after (not saved)
        int detail = 1;
        qint64 created = 0, sorted = 0;
        QVector<Topic> topics;
        QVector<Doc> docs;
        QVector<Move> moves;
        QStringList madeDirs;
        // not saved
        QString status, error;
        bool busy = false;
    };

    Pod *find(const QString &id);
    const Pod *find(const QString &id) const;
    void run(const QString &id, const QString &status, int detail, int kind);   // kind: 0 sort, 1 refresh, 2 AI
    void finish(Pod *p, const QString &error = QString());
    Topic &otherTopic(Pod &p);
    void dropEmptyTopics(Pod &p);
    QString topicDir(const Pod &p, const Topic &t) const;    // the folder a topic's files go in, with the topics above it
    QVector<const Topic *> ordered(const Pod &p) const;    // parents before children, in order
    QString filePath() const;
    void load();
    void save() const;
    void changed() { save(); ++m_rev; emit revisionChanged(); }

    QVector<Pod> m_pods;
    int m_rev = 0;
    AiModels *m_models = nullptr;
};
