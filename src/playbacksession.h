// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAYBACKSESSION_H
#define PLAYBACKSESSION_H
#include "basegui.h"
#include "preferencesdialog.h"
#include "prefplaybackscreen.h"
#include "autohidewidget.h"
#include "playerprocess.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QMap>
#include <QMessageBox>
#include <QTimer>

// Header-only integration uses typed Qt connections and needs no new moc target.
class PlaybackSession : public QObject {
public:
    explicit PlaybackSession(BaseGui *window) : QObject(nullptr), gui(window) {
        video = gui->findChild<MplayerWindow *>("mplayerwindow");
        if (video) originalParent = video->parentWidget();
        viewport = new PlaybackViewport(gui);
        qApp->installEventFilter(this);
        QObject::connect(gui->getCore(), &Core::stateChanged, this, [this](Core::State state) {
            if (pendingDetach && state == Core::Stopped) {
                QTimer::singleShot(0, this, [this]() { release(); pendingDetach = false; });
            } else if (active && viewport) {
                viewport->setVideoVisible(state != Core::Stopped);
                if (state == Core::Playing || state == Core::Paused) quietBackend();
            }
        });
        QObject::connect(gui->getCore(), &Core::mediaStartPlay, this, [this]() {
            if (active) quietBackend();
        });
        QObject::connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
            if (!active || screen->name() != current.name) return;
            viewport->hide();
            pendingDetach = true;
            gui->getCore()->stop();
            if (gui->getCore()->state() == Core::Stopped)
                QTimer::singleShot(0, this, [this]() { release(); pendingDetach = false; });
        });
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            if (viewport) viewport->hide();
        });
        auto *action = new QAction(tr("Playback Screen..."), gui);
        action->setObjectName("configure_playback_screen");
        gui->menuBar()->addAction(action);
        QObject::connect(action, &QAction::triggered, this, [this]() { choose(); });
    }
    ~PlaybackSession() override {
        qApp->removeEventFilter(this);
        if (viewport) viewport->hide();
        // The GUI owns the viewport. Core is destroyed before its video widget.
    }
    void initialize() {
        current = PlaybackScreenChoice::load();
        if (!current.remember || (current.dedicated && !PlaybackViewport::findScreenByName(current.name)))
            choose();
        else
            apply(current);
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            if (auto *dialog = qobject_cast<PreferencesDialog *>(object)) {
                if (!editors.contains(dialog)) {
                    auto *page = new PrefPlaybackScreen(dialog);
                    editors.insert(dialog, page);
                    dialog->addSection(page);
                    QObject::connect(dialog, &PreferencesDialog::applied, this, [this, page]() {
                        const auto next = page->choice();
                        if (apply(next)) { current = next; current.save(); }
                    });
                    QObject::connect(dialog, &QObject::destroyed, this, [this, dialog]() {
                        editors.remove(dialog);
                    });
                }
                editors.value(dialog)->setChoice(current);
            }
            if (active && video) {
                if (object->objectName() == "mplayerwindowlogo") {
                    if (auto *widget = qobject_cast<QWidget *>(object)) widget->hide();
                }
                if (auto *controls = qobject_cast<AutohideWidget *>(object)) {
                    if (video->isAncestorOf(controls)) controls->deactivate();
                }
            }
        }
        return QObject::eventFilter(object, event);
    }
private:
    void choose() {
        if (!gui) return;
        if (gui->getCore()->state() != Core::Stopped) {
            QMessageBox::information(gui, tr("Playback Screen"), tr("Stop playback before changing the playback display."));
            return;
        }
        PlaybackScreenDialog dialog(gui);
        dialog.setCurrentSelection(current.dedicated, current.name, current.remember);
        if (dialog.exec() != QDialog::Accepted) return;
        PlaybackScreenChoice next;
        next.dedicated = dialog.useDedicatedScreen();
        next.name = dialog.selectedScreenName();
        next.remember = dialog.rememberChoice();
        if (apply(next)) { current = next; current.save(); }
    }
    bool apply(const PlaybackScreenChoice &next) {
        if (!gui || !video || !viewport) return false;
        if (active == next.dedicated && (!active || current.name == next.name)) return true;
        if (gui->getCore()->state() != Core::Stopped) {
            QMessageBox::information(gui, tr("Playback Screen"), tr("Stop playback before changing the playback display. The previous selection remains active."));
            return false;
        }
        QScreen *screen = next.dedicated ? PlaybackViewport::findScreenByName(next.name) : nullptr;
        if (next.dedicated && !screen) {
            QMessageBox::warning(gui, tr("Playback Screen"), tr("The selected playback display is disconnected."));
            return false;
        }
        release();
        if (!next.dedicated) return true;
        const auto screens = QGuiApplication::screens();
        if (screens.size() > 1 && gui->windowHandle() && gui->windowHandle()->screen() == screen) {
            for (QScreen *other : screens) {
                if (other == screen) continue;
                gui->move(other->availableGeometry().topLeft() + QPoint(40, 40));
                break;
            }
        }
        active = true;
        current = next;
        for (QAction *action : gui->findChildren<QAction *>()) {
            const QString name = action->objectName();
            if (name == "fullscreen" || name == "compact" || name == "send_to_screen_menu") {
                disabled.insert(action, action->isEnabled());
                action->setEnabled(false);
            }
        }
        for (AutohideWidget *controls : video->findChildren<AutohideWidget *>()) controls->deactivate();
        viewport->attachVideo(video, screen);
        viewport->setVideoVisible(false);
        return true;
    }
    void release() {
        if (viewport && video && originalParent && viewport->isAttached())
            viewport->detachVideo(video, originalParent);
        for (auto it = disabled.begin(); it != disabled.end(); ++it) {
            if (gui && gui->findChildren<QAction *>().contains(it.key())) it.key()->setEnabled(it.value());
        }
        disabled.clear();
        active = false;
    }
    void quietBackend() {
        if (!gui) return;
        if (auto *process = gui->getCore()->findChild<PlayerProcess *>()) {
            process->enableOSDInCommands(false);
            process->setOSD(0);
            if (process->isMPV()) process->sendCommand("no-osd set osd-bar no");
        }
    }
    QPointer<BaseGui> gui;
    QPointer<MplayerWindow> video;
    QPointer<QWidget> originalParent;
    QPointer<PlaybackViewport> viewport;
    QMap<PreferencesDialog *, PrefPlaybackScreen *> editors;
    QMap<QAction *, bool> disabled;
    PlaybackScreenChoice current;
    bool active = false;
    bool pendingDetach = false;
};
#endif
