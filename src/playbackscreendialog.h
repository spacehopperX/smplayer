// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PLAYBACKSCREENDIALOG_H
#define PLAYBACKSCREENDIALOG_H
#include "playbackviewport.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>

class PlaybackScreenSelector : public QWidget {
public:
    explicit PlaybackScreenSelector(QWidget *parent = nullptr) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(tr("Select the fullscreen playback display for 2D or 3D video. Controls stay in the main window. Changing the playback display requires stopped playback."), this);
        label->setWordWrap(true);
        layout->addWidget(label);
        combo = new QComboBox(this);
        layout->addWidget(combo);
        remember = new QCheckBox(tr("Remember this playback screen for future sessions"), this);
        layout->addWidget(remember);
        auto *refresh = new QPushButton(tr("Refresh displays"), this);
        layout->addWidget(refresh);
        QObject::connect(refresh, &QPushButton::clicked, this, [this]() {
            setSelection(dedicated(), screenName(), remembered());
        });
        setSelection(false, QString(), false);
    }
    void setSelection(bool dedicated, const QString &name, bool persist) {
        combo->clear();
        combo->addItem(tr("Single window (embedded in SMPlayer)"), QString());
        int index = 0;
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i) {
            combo->addItem(PlaybackViewport::screenDescription(screens[i], i), screens[i]->name());
            if (dedicated && screens[i]->name() == name) index = i + 1;
        }
        if (dedicated && index == 0 && !name.isEmpty()) {
            combo->addItem(tr("%1 (disconnected)").arg(name), name);
            index = combo->count() - 1;
        }
        combo->setCurrentIndex(index);
        remember->setChecked(persist);
    }
    bool dedicated() const { return !screenName().isEmpty(); }
    QString screenName() const { return combo->currentData().toString(); }
    bool remembered() const { return remember->isChecked(); }
private:
    QComboBox *combo;
    QCheckBox *remember;
};

class PlaybackScreenDialog : public QDialog {
public:
    explicit PlaybackScreenDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle(tr("Select Playback Screen"));
        setMinimumWidth(480);
        auto *layout = new QVBoxLayout(this);
        selector = new PlaybackScreenSelector(this);
        layout->addWidget(selector);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }
    void setCurrentSelection(bool dedicated, const QString &name, bool remember) {
        selector->setSelection(dedicated, name, remember);
    }
    bool useDedicatedScreen() const { return selector->dedicated(); }
    QString selectedScreenName() const { return selector->screenName(); }
    bool rememberChoice() const { return selector->remembered(); }
private:
    PlaybackScreenSelector *selector;
};
#endif
