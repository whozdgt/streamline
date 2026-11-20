#pragma once
#include <QMargins>
#include <QObject>
#include <QTimer>
#include <QWindow>

// How much of the window the phone's own bars cover: the status bar and camera notch at the top, the gesture bar
// at the bottom. Android 15+ (for apps that target it) and iOS draw the app right up to the screen's edges, so the
// app keeps its buttons out of these strips. Zero on computers.
class SafeArea : public QObject {
    Q_OBJECT
    Q_PROPERTY(int top READ top NOTIFY changed)
    Q_PROPERTY(int bottom READ bottom NOTIFY changed)
    Q_PROPERTY(int left READ left NOTIFY changed)
    Q_PROPERTY(int right READ right NOTIFY changed)
public:
    using QObject::QObject;
    int top() const { return m_m.top(); }
    int bottom() const { return m_m.bottom(); }
    int left() const { return m_m.left(); }
    int right() const { return m_m.right(); }

    void watch(QWindow *w)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        if (!w) return;
        // Phones report the bars a moment after start, and again after a rotation. Reading them is cheap, so look
        // again whenever the window changes size and, to be sure, once a second.
        // STREAMLINE_FAKE_INSETS=top,bottom pretends a phone's bars are there, for checking the layout on a computer.
        const QByteArray fake = qgetenv("STREAMLINE_FAKE_INSETS");
        const auto read = [this, w, fake]() {
            if (!fake.isEmpty()) {
                const auto p = fake.split(',');
                set(QMargins(0, p.value(0).toInt(), 0, p.value(1).toInt()));
            } else {
                set(w->safeAreaMargins());
            }
        };
        read();
        connect(w, &QWindow::widthChanged, this, read);
        connect(w, &QWindow::heightChanged, this, read);
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, read);
        t->start(1000);
#else
        Q_UNUSED(w)
#endif
    }

signals:
    void changed();

private:
    void set(const QMargins &m)
    {
        if (m == m_m) return;
        m_m = m;
        emit changed();
    }
    QMargins m_m;
};
