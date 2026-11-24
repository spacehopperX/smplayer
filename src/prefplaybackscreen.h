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

#ifndef PREFPLAYBACKSCREEN_H
#define PREFPLAYBACKSCREEN_H

#include "prefwidget.h"

class QComboBox;
class QCheckBox;
class QPushButton;
class Preferences;

/*!
 * Preferences page for configuring the dedicated fullscreen playback display
 * (2D & 3D) separately from SMPlayer's operator window.
 */
class PrefPlaybackScreen : public PrefWidget
{
	Q_OBJECT

public:
	explicit PrefPlaybackScreen(QWidget * parent = 0, Qt::WindowFlags f = QFlag(0));
	~PrefPlaybackScreen();

	virtual QString sectionName();
	virtual QPixmap sectionIcon();

	void setData(Preferences * pref);
	void getData(Preferences * pref);

protected:
	virtual void createHelp();
	virtual void retranslateStrings();

private slots:
	void refreshScreens();

private:
	QComboBox * screen_combo;
	QCheckBox * remember_check;
	QPushButton * refresh_button;
};

#endif
