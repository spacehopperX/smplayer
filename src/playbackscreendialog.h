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

#ifndef PLAYBACKSCREENDIALOG_H
#define PLAYBACKSCREENDIALOG_H

#include <QDialog>
#include <QString>

class QComboBox;
class QCheckBox;
class QPushButton;

/*!
 * Modal dialog shown on first launch (or from Options -> Playback Screen)
 * letting the user pick which display hosts the borderless black playback
 * viewport, or choose embedded single-window playback.
 */
class PlaybackScreenDialog : public QDialog
{
	Q_OBJECT

public:
	explicit PlaybackScreenDialog(QWidget * parent = 0);
	~PlaybackScreenDialog();

	void setCurrentSelection(bool use_dedicated, const QString & screen_name, bool remember);

	bool useDedicatedScreen() const;
	QString selectedScreenName() const;
	bool rememberChoice() const;

private slots:
	void refreshScreens();

private:
	QComboBox * screen_combo;
	QCheckBox * remember_check;
	QPushButton * refresh_button;
};

#endif
