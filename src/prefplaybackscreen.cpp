/*  smplayer, GUI front-end for mplayer.
    Copyright (C) 2006-2026 Ricardo Villalba <ricardo@smplayer.info>
    Dual-screen 3D/2D playback viewport extensions (C) 2026 spacehopperX

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "prefplaybackscreen.h"
#include "playbackviewport.h"
#include "preferences.h"
#include "images.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

PrefPlaybackScreen::PrefPlaybackScreen(QWidget * parent, Qt::WindowFlags f)
	: PrefWidget(parent, f)
{
	QVBoxLayout * main_layout = new QVBoxLayout(this);

	QGroupBox * group = new QGroupBox(tr("Dedicated Playback Screen (2D / 3D)"), this);
	QVBoxLayout * group_layout = new QVBoxLayout(group);

	QLabel * desc = new QLabel(
		tr("Select a dedicated display for distraction-free fullscreen black video playback. "
		   "SMPlayer's controls and playlist stay on your operator display."),
		group);
	desc->setWordWrap(true);
	group_layout->addWidget(desc);

	QHBoxLayout * row = new QHBoxLayout();
	screen_combo = new QComboBox(group);
	refresh_button = new QPushButton(tr("Refresh displays"), group);
	row->addWidget(screen_combo, 1);
	row->addWidget(refresh_button);
	group_layout->addLayout(row);

	remember_check = new QCheckBox(
		tr("Remember this playback screen (skip startup prompt)"), group);
	group_layout->addWidget(remember_check);

	main_layout->addWidget(group);
	main_layout->addStretch(1);

	connect(refresh_button, SIGNAL(clicked()), this, SLOT(refreshScreens()));

	refreshScreens();
	retranslateStrings();
}

PrefPlaybackScreen::~PrefPlaybackScreen()
{
}

QString PrefPlaybackScreen::sectionName()
{
	return tr("Playback Screen");
}

QPixmap PrefPlaybackScreen::sectionIcon()
{
	return Images::icon("send_to_screen");
}

void PrefPlaybackScreen::refreshScreens()
{
	const QString prev = screen_combo->currentData().toString();
	screen_combo->clear();
	screen_combo->addItem(tr("Single window (embedded in SMPlayer)"), QString());

	const QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) {
		screen_combo->addItem(PlaybackViewport::screenDescription(screens[i], i), screens[i]->name());
	}

	int idx = screen_combo->findData(prev);
	if (idx >= 0) screen_combo->setCurrentIndex(idx);
}

void PrefPlaybackScreen::setData(Preferences * pref)
{
	refreshScreens();
	remember_check->setChecked(pref->remember_playback_screen);

	if (!pref->use_dedicated_playback_screen || pref->playback_screen_name.isEmpty()) {
		screen_combo->setCurrentIndex(0);
	} else {
		int idx = screen_combo->findData(pref->playback_screen_name);
		if (idx >= 0) {
			screen_combo->setCurrentIndex(idx);
		} else {
			// Screen currently disconnected; show it so user sees saved value
			screen_combo->addItem(
				tr("%1 (disconnected)").arg(pref->playback_screen_name),
				pref->playback_screen_name);
			screen_combo->setCurrentIndex(screen_combo->count() - 1);
		}
	}
}

void PrefPlaybackScreen::getData(Preferences * pref)
{
	requires_restart = false;

	const QString selected = screen_combo->currentData().toString();
	const bool use_dedicated = !selected.isEmpty();

	pref->use_dedicated_playback_screen = use_dedicated;
	pref->playback_screen_name = selected;
	pref->remember_playback_screen = remember_check->isChecked();
}

void PrefPlaybackScreen::retranslateStrings()
{
	createHelp();
}

void PrefPlaybackScreen::createHelp()
{
	clearHelp();
	addSectionTitle(tr("Playback Screen"));
	setWhatsThis(screen_combo, tr("Playback display"),
		tr("Choose which monitor or TV hosts the dedicated borderless black playback viewport, "
		   "or select Single window to keep video embedded inside SMPlayer."));
	setWhatsThis(remember_check, tr("Remember playback screen"),
		tr("When checked, SMPlayer opens directly with the saved playback screen instead of "
		   "asking at startup."));
}

#include "moc_prefplaybackscreen.cpp"
