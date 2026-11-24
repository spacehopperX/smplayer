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

#ifndef PLAYBACKVIEWPORT_H
#define PLAYBACKVIEWPORT_H

#include <QWidget>
#include <QString>

class QScreen;
class QVBoxLayout;
class MplayerWindow;

/*!
 * Borderless fullscreen black viewport pinned to a dedicated playback display
 * (3D TV, projector, or secondary 2D monitor). Keeps SMPlayer's main control UI
 * on the operator's screen with zero OSD/UI distractions on the playback screen.
 */
class PlaybackViewport : public QWidget
{
	Q_OBJECT

public:
	explicit PlaybackViewport(QWidget * parent = 0);
	~PlaybackViewport();

	//! Attach MplayerWindow into this fullscreen black viewport on \a screen.
	void attachVideo(MplayerWindow * mpw, QScreen * screen);

	//! Reposition/resize the viewport to match \a screen's full geometry.
	void applyScreen(QScreen * screen);

	//! Detach MplayerWindow back to \a target_parent's layout and hide viewport.
	void detachVideo(MplayerWindow * mpw, QWidget * target_parent);

	bool isAttached() const { return attached_window != 0; }
	QString currentScreenName() const { return screen_name; }

	//! Find a QScreen by its saved name, returning 0 if disconnected.
	static QScreen * findScreenByName(const QString & name);

	//! Human-readable label for a QScreen (e.g. "Display 2: \\.\DISPLAY2 (1920x2205 @ 30 Hz)").
	static QString screenDescription(QScreen * screen, int index);

protected:
	virtual void closeEvent(QCloseEvent * e);

private:
	QVBoxLayout * root_layout;
	MplayerWindow * attached_window;
	QString screen_name;
};

#endif
