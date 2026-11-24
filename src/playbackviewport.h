// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAYBACKVIEWPORT_H
#define PLAYBACKVIEWPORT_H
#include "mplayerwindow.h"
#include <QCloseEvent>
#include <QGuiApplication>
#include <QLayout>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

class PlaybackViewport : public QWidget {
public:
    explicit PlaybackViewport(QWidget *owner = nullptr)
        : QWidget(owner, Qt::Window | Qt::FramelessWindowHint) {
        setObjectName("playback_viewport");
        setWindowTitle(tr("SMPlayer Playback"));
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::black);
        setPalette(p);
        setCursor(Qt::BlankCursor);
        root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
    }
    void attachVideo(MplayerWindow *video, QScreen *screen) {
        if (!video || !screen) return;
        if (attached != video) {
            if (video->parentWidget() && video->parentWidget()->layout())
                video->parentWidget()->layout()->removeWidget(video);
            video->setParent(this);
            root->addWidget(video);
            attached = video;
        }
        video->setLogoVisible(false);
        applyScreen(screen);
    }
    void applyScreen(QScreen *screen) {
        if (!screen) return;
        if (target != screen) {
            if (target) QObject::disconnect(target.data(), nullptr, this, nullptr);
            target = screen;
            QObject::connect(screen, &QScreen::geometryChanged, this,
                [this](const QRect &) { if (target) applyScreen(target); });
        }
        winId();
        if (windowHandle()) windowHandle()->setScreen(screen);
        setGeometry(screen->geometry());
        showFullScreen();
    }
    void setVideoVisible(bool visible) {
        if (attached) attached->setVisible(visible);
    }
    void detachVideo(MplayerWindow *video, QWidget *parent) {
        if (attached == video && video && parent) {
            root->removeWidget(video);
            video->setParent(parent);
            if (parent->layout()) parent->layout()->addWidget(video);
            attached.clear();
            video->show();
        }
        if (target) QObject::disconnect(target.data(), nullptr, this, nullptr);
        target.clear();
        hide();
    }
    bool isAttached() const { return !attached.isNull(); }
    QString currentScreenName() const { return target ? target->name() : QString(); }
    static QScreen *findScreenByName(const QString &name) {
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == name) return s;
        return nullptr;
    }
    static QString screenDescription(QScreen *s, int index) {
        if (!s) return QString();
        const QSize size = s->size() * s->devicePixelRatio();
        return tr("Display %1: %2 (%3x%4, %5 Hz)%6")
            .arg(index + 1).arg(s->name()).arg(size.width()).arg(size.height())
            .arg(s->refreshRate(), 0, 'f', 3)
            .arg(s == QGuiApplication::primaryScreen() ? tr(" [Primary]") : QString());
    }
protected:
    void closeEvent(QCloseEvent *event) override { event->ignore(); }
private:
    QVBoxLayout *root;
    QPointer<MplayerWindow> attached;
    QPointer<QScreen> target;
};
#endif
