// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAYBACKSESSION_H
#define PLAYBACKSESSION_H
#include "smplayer.h"
#include "basegui.h"
#include "global.h"
#include "preferences.h"
#include "preferencesdialog.h"
#include "prefplaybackscreen.h"
#include "mvhevcbridge.h"
#include "stereo3dcontroller.h"
#include "autohidewidget.h"
#include "playerprocess.h"
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QMap>
#include <QMenuBar>
#include <QMessageBox>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Header-only integration uses typed Qt connections and needs no new moc target.
class PlaybackSession : public QObject {
public:
    explicit PlaybackSession(SMPlayer *instance) : QObject(nullptr), app(instance) {
        qApp->installEventFilter(this);
        QObject::connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
            stereo.onScreenRemoved(screen->name());
            if (!active || !gui || screen->name() != current.name) return;
            if (viewport) viewport->hide();
            pendingDetach = true;
            gui->getCore()->stop();
            if (gui->getCore()->state() == Core::Stopped)
                QTimer::singleShot(0, this, [this]() { release(); pendingDetach = false; });
        });
        QObject::connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            if (!screen || active || !gui) return;
            if (!current.dedicated || current.name.isEmpty() || screen->name() != current.name) return;
            if (gui->getCore()->state() == Core::Stopped) {
                QTimer::singleShot(300, this, [this]() {
                    if (!active && current.dedicated && PlaybackViewport::findScreenByName(current.name))
                        apply(current);
                });
            } else {
                pendingReattach = true;
            }
        });
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
            mvhevc.stop();
            stereo.restoreDisplayMode();
            if (viewport) viewport->hide();
        });
        bindGui(app ? app->gui() : nullptr);
    }
    ~PlaybackSession() override {
        qApp->removeEventFilter(this);
        mvhevc.stop();
        stereo.restoreDisplayMode();
        if (viewport) {
            viewport->hide();
            delete viewport;
            viewport.clear();
        }
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
        if (active && video && stereo.isFramePackedActive() &&
            object == video->videoLayer() &&
            (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
            if (QWidget *vl = video->videoLayer()) {
                if (vl->geometry() != video->rect()) {
                    QTimer::singleShot(0, this, [this]() {
                        if (active && video && stereo.isFramePackedActive()) {
                            video->setAspect(0);
                            if (QWidget *layer = video->videoLayer()) {
                                if (layer->geometry() != video->rect()) layer->setGeometry(video->rect());
                            }
                        }
                    });
                }
            }
        }
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
                    if (auto *widget = qobject_cast<QWidget *>(object)) {
                        widget->hide();
                        widget->lower();
                        QTimer::singleShot(0, this, [this]() {
                            if (active) hideLogo();
                        });
                    }
                }
                if (auto *controls = qobject_cast<AutohideWidget *>(object)) {
                    if (video->isAncestorOf(controls)) controls->deactivate();
                }
            }
        }
        return QObject::eventFilter(object, event);
    }
private:
    void hideLogo() {
        if (!video) return;
        video->setLogoVisible(false);
        if (auto *logo = video->findChild<QWidget *>("mplayerwindowlogo")) {
            logo->hide();
            logo->lower();
        }
        if (QWidget *vl = video->videoLayer()) {
            vl->raise();
#ifdef Q_OS_WIN
            if (vl->testAttribute(Qt::WA_WState_Created)) {
                SetWindowRgn(reinterpret_cast<HWND>(vl->winId()), nullptr, TRUE);
            }
#endif
        }
    }
    void scheduleLogoClear() {
        hideLogo();
        QTimer::singleShot(0, this, [this]() {
            if (gui && gui->getCore()->state() != Core::Stopped) hideLogo();
        });
        QTimer::singleShot(100, this, [this]() {
            if (gui && gui->getCore()->state() != Core::Stopped) hideLogo();
        });
    }
    void reapplyViewportGeometry() {
        if (!active || !viewport) return;
        QTimer::singleShot(150, this, [this]() {
            if (!active || !viewport) return;
            if (QScreen *screen = PlaybackViewport::findScreenByName(current.name)) {
                viewport->applyScreen(screen);
            }
            if (video && stereo.isFramePackedActive()) {
                video->setAspect(0);
                if (QWidget *vl = video->videoLayer()) {
                    if (vl->geometry() != video->rect()) vl->setGeometry(video->rect());
                }
            }
            if (gui && gui->getCore()->state() != Core::Stopped) scheduleLogoClear();
        });
    }
    QString activeScreenName() const {
        return active ? current.name : QString();
    }
    void bindGui(BaseGui *window) {
        if (!window) return;
        gui = window;
        video = gui->findChild<MplayerWindow *>("mplayerwindow");
        originalParent = video ? video->parentWidget() : nullptr;
        if (viewport) {
            viewport->hide();
            viewport->deleteLater();
        }
        viewport = new PlaybackViewport(gui);
        disabled.clear();
        active = false;
        pendingDetach = false;
        pendingReattach = false;
        redirectingMvHevc = false;
        savedForceFullscreen = gui->forceStartInFullscreen();

        QObject::connect(gui->getCore(), &Core::aboutToStartPlaying, this, [this]() {
            if (!gui || !gui->getCore()) return;
            const QString file = gui->getCore()->mdat.filename;
            if (mvhevc.isActiveUrl(file)) return;
            if (redirectingMvHevc) return;

            mvhevc.stop();
            stereo.setBridgeNote(QString());
            if (!stereo.settings().enableMvHevcBridge) return;

            const MvHevcProbe info = MvHevcBridge::probe(file, stereo.settings().ffmpegPath);
            if (!info.isMvHevc) return;

            QString err;
            if (!mvhevc.start(file, info, stereo.settings().ffmpegPath, &err,
                              [this](const QString &note) { stereo.setBridgeNote(note); })) {
                stereo.setBridgeNote(err);
                return;
            }

            redirectingMvHevc = true;
            const quint64 gen = ++mvHevcRedirectGen;
            const QString streamUrl = mvhevc.url();
            const QString title = QFileInfo(file).fileName() + QStringLiteral(" [MV-HEVC 3D]");

            gui->getCore()->stop();
            QTimer::singleShot(250, this, [this, gen, streamUrl, title]() {
                if (!gui || !gui->getCore() || mvHevcRedirectGen != gen || !mvhevc.isActiveUrl(streamUrl)) {
                    if (mvHevcRedirectGen == gen) redirectingMvHevc = false;
                    return;
                }
                gui->getCore()->addForcedTitle(streamUrl, title);
                gui->getCore()->openStream(streamUrl);
            });
            QTimer::singleShot(6000, this, [this, gen]() {
                if (mvHevcRedirectGen == gen) redirectingMvHevc = false;
            });
        });

        QObject::connect(gui->getCore(), &Core::stateChanged, this, [this](Core::State state) {
            if (pendingDetach && state == Core::Stopped) {
                QTimer::singleShot(0, this, [this]() { release(); pendingDetach = false; });
            } else if (state == Core::Stopped) {
                if (!redirectingMvHevc) {
                    mvhevc.stop();
                    if (active && viewport) viewport->setVideoVisible(false);
                    stereo.onPlaybackStopped(gui, [this]() { reapplyViewportGeometry(); });
                    if (pendingReattach && !active) {
                        pendingReattach = false;
                        QTimer::singleShot(200, this, [this]() {
                            if (!active && current.dedicated && PlaybackViewport::findScreenByName(current.name))
                                apply(current);
                        });
                    }
                }
            } else if (active && viewport) {
                viewport->setVideoVisible(true);
                scheduleLogoClear();
                if (state == Core::Playing || state == Core::Paused) quietBackend();
            } else if (state == Core::Playing || state == Core::Paused) {
                scheduleLogoClear();
            }
        });
        QObject::connect(gui->getCore(), &Core::mediaStartPlay, this, [this]() {
            if (redirectingMvHevc && gui && gui->getCore() &&
                mvhevc.isActiveUrl(gui->getCore()->mdat.filename)) {
                redirectingMvHevc = false;
            }
            scheduleLogoClear();
            if (active) quietBackend();
            stereo.applyForActiveMedia(gui, activeScreenName(), [this]() { reapplyViewportGeometry(); },
                                       mvhevc.isActive(), mvhevc.summary());
        });
        QObject::connect(gui->getCore(), &Core::mediaLoaded, this, [this]() {
            if (redirectingMvHevc && gui && gui->getCore() &&
                mvhevc.isActiveUrl(gui->getCore()->mdat.filename)) {
                redirectingMvHevc = false;
            }
            scheduleLogoClear();
            stereo.applyForActiveMedia(gui, activeScreenName(), [this]() { reapplyViewportGeometry(); },
                                       mvhevc.isActive(), mvhevc.summary());
        });
        QObject::connect(gui.data(), &QObject::destroyed, this, [this]() {
            if (viewport) {
                viewport->hide();
                viewport->deleteLater();
                viewport.clear();
            }
            if (QCoreApplication::closingDown() || !app) return;
            QTimer::singleShot(0, this, [this]() {
                if (QCoreApplication::closingDown() || !app) return;
                bindGui(app->gui());
                if (current.dedicated && PlaybackViewport::findScreenByName(current.name))
                    apply(current);
            });
        });

        auto *screenAction = new QAction(tr("Playback Screen..."), gui);
        screenAction->setObjectName("configure_playback_screen");
        gui->menuBar()->addAction(screenAction);
        QObject::connect(screenAction, &QAction::triggered, this, [this]() { choose(); });

        auto *stereoAction = new QAction(tr("3D Output..."), gui);
        stereoAction->setObjectName("configure_stereo_3d_output");
        gui->menuBar()->addAction(stereoAction);
        QObject::connect(stereoAction, &QAction::triggered, this, [this]() {
            stereo.showDialog(gui, activeScreenName(), [this]() { reapplyViewportGeometry(); },
                              mvhevc.isActive(), mvhevc.summary());
        });
    }
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
        pendingReattach = false;
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
        QScreen *guiScreen = gui->windowHandle()
            ? gui->windowHandle()->screen()
            : QGuiApplication::screenAt(gui->geometry().center());
        if (!guiScreen) guiScreen = QGuiApplication::primaryScreen();
        if (screens.size() > 1 && guiScreen == screen) {
            for (QScreen *other : screens) {
                if (other == screen) continue;
                gui->move(other->availableGeometry().topLeft() + QPoint(40, 40));
                break;
            }
        }

        savedForceFullscreen = gui->forceStartInFullscreen();
        gui->setForceStartInFullscreen(0);
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
        quietBackend();
        return true;
    }
    void release() {
        redirectingMvHevc = false;
        mvhevc.stop();
        stereo.restoreDisplayMode();
        if (viewport && video && originalParent && viewport->isAttached())
            viewport->detachVideo(video, originalParent);
        if (gui) {
            gui->setForceStartInFullscreen(savedForceFullscreen);
            const auto actions = gui->findChildren<QAction *>();
            for (auto it = disabled.begin(); it != disabled.end(); ++it) {
                if (actions.contains(it.key())) it.key()->setEnabled(it.value());
            }
        }
        disabled.clear();
        active = false;
        restoreBackend();
    }
    void quietBackend() {
        if (!gui) return;
        if (auto *process = gui->getCore()->playerProcess()) {
            process->enableOSDInCommands(false);
            process->setOSD(0);
            if (process->isMPV()) process->sendCommand("no-osd set osd-bar no");
        }
    }
    void restoreBackend() {
        if (!gui) return;
        if (auto *process = gui->getCore()->playerProcess()) {
            process->enableOSDInCommands(true);
            if (Global::pref) process->setOSD(Global::pref->osd);
            if (process->isMPV()) process->sendCommand("no-osd set osd-bar yes");
        }
    }
    QPointer<SMPlayer> app;
    QPointer<BaseGui> gui;
    QPointer<MplayerWindow> video;
    QPointer<QWidget> originalParent;
    QPointer<PlaybackViewport> viewport;
    QMap<PreferencesDialog *, PrefPlaybackScreen *> editors;
    QMap<QAction *, bool> disabled;
    PlaybackScreenChoice current;
    MvHevcBridge mvhevc;
    Stereo3DController stereo;
    int savedForceFullscreen = -1;
    bool active = false;
    bool pendingDetach = false;
    bool pendingReattach = false;
    bool redirectingMvHevc = false;
    quint64 mvHevcRedirectGen = 0;
};
#endif
