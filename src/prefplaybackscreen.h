// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef PREFPLAYBACKSCREEN_H
#define PREFPLAYBACKSCREEN_H
#include "prefwidget.h"
#include "playbackscreendialog.h"
#include "paths.h"
#include "images.h"
#include <QSettings>
class Preferences;

struct PlaybackScreenChoice {
    bool dedicated = false;
    QString name;
    bool remember = false;
    static PlaybackScreenChoice load() {
        QSettings settings(Paths::configPath() + "/playback-screen.ini", QSettings::IniFormat);
        PlaybackScreenChoice choice;
        choice.dedicated = settings.value("dedicated", false).toBool();
        choice.name = settings.value("screenName").toString();
        choice.remember = settings.value("remember", false).toBool();
        return choice;
    }
    void save() const {
        QSettings settings(Paths::configPath() + "/playback-screen.ini", QSettings::IniFormat);
        settings.setValue("dedicated", dedicated);
        settings.setValue("screenName", name);
        settings.setValue("remember", remember);
        settings.sync();
    }
};

class PrefPlaybackScreen : public PrefWidget {
public:
    explicit PrefPlaybackScreen(QWidget *parent = nullptr) : PrefWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        selector = new PlaybackScreenSelector(this);
        layout->addWidget(selector);
        layout->addStretch();
        setData(nullptr);
    }
    QString sectionName() override { return tr("Playback Screen"); }
    QPixmap sectionIcon() override { return Images::icon("send_to_screen"); }
    void setChoice(const PlaybackScreenChoice &choice) {
        selector->setSelection(choice.dedicated, choice.name, choice.remember);
    }
    PlaybackScreenChoice choice() const {
        PlaybackScreenChoice result;
        result.dedicated = selector->dedicated();
        result.name = selector->screenName();
        result.remember = selector->remembered();
        return result;
    }
    void setData(Preferences *) { setChoice(PlaybackScreenChoice::load()); }
    void getData(Preferences *) { choice().save(); }
private:
    PlaybackScreenSelector *selector;
};
#endif
