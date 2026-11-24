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

#include "playbackviewport.h"
#include "mplayerwindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QPalette>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

PlaybackViewport::PlaybackViewport(QWidget * parent)
	: QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
	, root_layout(0)
	, attached_window(0)
{
	setObjectName("playback_viewport");
	setWindowTitle(tr("SMPlayer Playback Viewport"));

	// Pure black background so the playback display stays blank before/after playback
	setAutoFillBackground(true);
	QPalette pal = palette();
	pal.setColor(QPalette::Window, Qt::black);
	setPalette(pal);
	setStyleSheet("background-color: black;");

	// Hide mouse cursor on the dedicated playback screen
	setCursor(Qt::BlankCursor);

	root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(0, 0, 0, 0);
	root_layout->setSpacing(0);
}

PlaybackViewport::~PlaybackViewport()
{
}

void PlaybackViewport::attachVideo(MplayerWindow * mpw, QScreen * screen)
{
	if (!mpw || !screen) return;

	if (attached_window != mpw) {
		if (mpw->parentWidget() && mpw->parentWidget()->layout()) {
			mpw->parentWidget()->layout()->removeWidget(mpw);
		}
		mpw->setParent(this);
		root_layout->addWidget(mpw);
		mpw->setLogoVisible(false);
		mpw->show();
		attached_window = mpw;
	}

	applyScreen(screen);
}

void PlaybackViewport::applyScreen(QScreen * screen)
{
	if (!screen) return;

	screen_name = screen->name();
	const QRect geom = screen->geometry();

	// Ensure native window exists so Qt binds it to the target QScreen
	winId();
	if (windowHandle()) {
		windowHandle()->setScreen(screen);
	}

	setGeometry(geom);
	showFullScreen();
	// Re-enforce exact screen geometry after showFullScreen on Windows multi-DPI setups
	setGeometry(geom);
	raise();
}

void PlaybackViewport::detachVideo(MplayerWindow * mpw, QWidget * target_parent)
{
	if (attached_window && mpw == attached_window) {
		root_layout->removeWidget(mpw);
		if (target_parent) {
			mpw->setParent(target_parent);
			if (target_parent->layout()) {
				target_parent->layout()->addWidget(mpw);
			}
			mpw->show();
		}
		attached_window = 0;
	}
	screen_name.clear();
	hide();
}

QScreen * PlaybackViewport::findScreenByName(const QString & name)
{
	if (name.isEmpty()) return 0;
	const QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) {
		if (screens[i]->name() == name) {
			return screens[i];
		}
	}
	return 0;
}

QString PlaybackViewport::screenDescription(QScreen * screen, int index)
{
	if (!screen) return QString();
	const QRect g = screen->geometry();
	const int hz = qRound(screen->refreshRate());
	QString label = tr("Display %1: %2 (%3x%4 @ %5 Hz)")
		.arg(index + 1)
		.arg(screen->name())
		.arg(g.width())
		.arg(g.height())
		.arg(hz);
	if (screen == QGuiApplication::primaryScreen()) {
		label += tr(" [Primary]");
	}
	return label;
}

void PlaybackViewport::closeEvent(QCloseEvent * e)
{
	// Ignore direct Alt+F4 on the viewport so the control window owns app shutdown
	e->ignore();
}

#include "moc_playbackviewport.cpp"
