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

#include "playbackscreendialog.h"
#include "playbackviewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

PlaybackScreenDialog::PlaybackScreenDialog(QWidget * parent)
	: QDialog(parent)
{
	setWindowTitle(tr("Select Playback Screen"));
	setMinimumWidth(460);

	QVBoxLayout * layout = new QVBoxLayout(this);

	QLabel * info = new QLabel(
		tr("Choose the display where video playback will appear in a dedicated "
		   "borderless fullscreen black viewport (for 2D or 3D playback). "
		   "SMPlayer's controls will remain on your operator screen."),
		this);
	info->setWordWrap(true);
	layout->addWidget(info);

	QHBoxLayout * row = new QHBoxLayout();
	screen_combo = new QComboBox(this);
	refresh_button = new QPushButton(tr("Refresh"), this);
	row->addWidget(screen_combo, 1);
	row->addWidget(refresh_button);
	layout->addLayout(row);

	remember_check = new QCheckBox(tr("Remember this playback screen for future sessions"), this);
	remember_check->setChecked(true);
	layout->addWidget(remember_check);

	QDialogButtonBox * buttons = new QDialogButtonBox(
		QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);

	connect(refresh_button, SIGNAL(clicked()), this, SLOT(refreshScreens()));
	connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
	connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));

	refreshScreens();
}

PlaybackScreenDialog::~PlaybackScreenDialog()
{
}

void PlaybackScreenDialog::refreshScreens()
{
	const QString prev_data = screen_combo->currentData().toString();
	screen_combo->clear();

	screen_combo->addItem(tr("Single window (embedded in SMPlayer)"), QString());

	const QList<QScreen *> screens = QGuiApplication::screens();
	int secondary_idx = -1;
	for (int i = 0; i < screens.size(); ++i) {
		QScreen * s = screens[i];
		screen_combo->addItem(PlaybackViewport::screenDescription(s, i), s->name());
		if (secondary_idx < 0 && s != QGuiApplication::primaryScreen()) {
			secondary_idx = i + 1;
		}
	}

	int restore_idx = screen_combo->findData(prev_data);
	if (restore_idx >= 0) {
		screen_combo->setCurrentIndex(restore_idx);
	} else if (secondary_idx >= 0) {
		// Default to the first secondary display (typically the TV/projector)
		screen_combo->setCurrentIndex(secondary_idx);
	}
}

void PlaybackScreenDialog::setCurrentSelection(bool use_dedicated, const QString & screen_name, bool remember)
{
	remember_check->setChecked(remember);
	if (!use_dedicated || screen_name.isEmpty()) {
		if (screen_combo->count() > 1 && !remember) {
			// On first launch with multiple monitors, keep secondary preselected
			return;
		}
		screen_combo->setCurrentIndex(0);
		return;
	}
	int idx = screen_combo->findData(screen_name);
	if (idx >= 0) {
		screen_combo->setCurrentIndex(idx);
	}
}

bool PlaybackScreenDialog::useDedicatedScreen() const
{
	return !screen_combo->currentData().toString().isEmpty();
}

QString PlaybackScreenDialog::selectedScreenName() const
{
	return screen_combo->currentData().toString();
}

bool PlaybackScreenDialog::rememberChoice() const
{
	return remember_check->isChecked();
}

#include "moc_playbackscreendialog.cpp"
