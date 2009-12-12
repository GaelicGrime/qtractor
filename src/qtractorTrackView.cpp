// qtractorTrackView.cpp
//
/****************************************************************************
   Copyright (C) 2005-2009, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorAbout.h"
#include "qtractorTrackView.h"
#include "qtractorTrackTime.h"
#include "qtractorTrackList.h"
#include "qtractorSession.h"
#include "qtractorTracks.h"
#include "qtractorFiles.h"

#include "qtractorAudioClip.h"
#include "qtractorAudioFile.h"
#include "qtractorMidiClip.h"
#include "qtractorMidiFile.h"
#include "qtractorSessionCursor.h"
#include "qtractorFileListView.h"
#include "qtractorClipSelect.h"
#include "qtractorClipCommand.h"
#include "qtractorClipCommand.h"

#include "qtractorMainForm.h"
#include "qtractorThumbView.h"

#include <QToolButton>
#include <QScrollBar>
#include <QToolTip>

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QKeyEvent>

#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QCursor>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>

#if QT_VERSION < 0x040300
#define lighter(x)	light(x)
#define darker(x)	dark(x)
#endif


//----------------------------------------------------------------------------
// qtractorTrackView::ClipBoard - Local clipaboard singleton.

// Singleton declaration.
qtractorTrackView::ClipBoard qtractorTrackView::g_clipboard;


//----------------------------------------------------------------------------
// qtractorTrackView -- Track view widget.

// Constructor.
qtractorTrackView::qtractorTrackView ( qtractorTracks *pTracks,
	QWidget *pParent ) : qtractorScrollView(pParent)
{
	m_pTracks = pTracks;

	m_pClipSelect    = new qtractorClipSelect();
	m_pSessionCursor = NULL;
	m_pRubberBand    = NULL;

	m_selectMode = SelectClip;

	m_bDropSpan = true;

	clear();

	// Zoom tool widgets
	m_pHzoomIn    = new QToolButton(this);
	m_pHzoomOut   = new QToolButton(this);
	m_pVzoomIn    = new QToolButton(this);
	m_pVzoomOut   = new QToolButton(this);
	m_pXzoomReset = new QToolButton(this);

	const QIcon& iconZoomIn = QIcon(":/icons/viewZoomIn.png");
	m_pHzoomIn->setIcon(iconZoomIn);
	m_pVzoomIn->setIcon(iconZoomIn);

	const QIcon& iconZoomOut = QIcon(":/icons/viewZoomOut.png");
	m_pHzoomOut->setIcon(iconZoomOut);
	m_pVzoomOut->setIcon(iconZoomOut);

	m_pXzoomReset->setIcon(QIcon(":/icons/viewZoomTool.png"));

	m_pHzoomIn->setAutoRepeat(true);
	m_pHzoomOut->setAutoRepeat(true);
	m_pVzoomIn->setAutoRepeat(true);
	m_pVzoomOut->setAutoRepeat(true);

	m_pHzoomIn->setToolTip(tr("Zoom in (horizontal)"));
	m_pHzoomOut->setToolTip(tr("Zoom out (horizontal)"));
	m_pVzoomIn->setToolTip(tr("Zoom in (vertical)"));
	m_pVzoomOut->setToolTip(tr("Zoom out (vertical)"));
	m_pXzoomReset->setToolTip(tr("Zoom reset"));

#if QT_VERSION >= 0x040201
	int iScrollBarExtent
		= qtractorScrollView::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	m_pHzoomIn->setFixedWidth(iScrollBarExtent);
	m_pHzoomOut->setFixedWidth(iScrollBarExtent);
	qtractorScrollView::addScrollBarWidget(m_pHzoomIn,  Qt::AlignRight);
	qtractorScrollView::addScrollBarWidget(m_pHzoomOut, Qt::AlignRight);
	m_pVzoomOut->setFixedHeight(iScrollBarExtent);
	m_pVzoomIn->setFixedHeight(iScrollBarExtent);
	qtractorScrollView::addScrollBarWidget(m_pVzoomOut, Qt::AlignBottom);
	qtractorScrollView::addScrollBarWidget(m_pVzoomIn,  Qt::AlignBottom);
#endif

	QObject::connect(m_pHzoomIn, SIGNAL(clicked()),
		m_pTracks, SLOT(horizontalZoomInSlot()));
	QObject::connect(m_pHzoomOut, SIGNAL(clicked()),
		m_pTracks, SLOT(horizontalZoomOutSlot()));
	QObject::connect(m_pVzoomIn, SIGNAL(clicked()),
		m_pTracks, SLOT(verticalZoomInSlot()));
	QObject::connect(m_pVzoomOut, SIGNAL(clicked()),
		m_pTracks, SLOT(verticalZoomOutSlot()));
	QObject::connect(m_pXzoomReset, SIGNAL(clicked()),
		m_pTracks, SLOT(viewZoomResetSlot()));

	qtractorScrollView::setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	qtractorScrollView::setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

	qtractorScrollView::viewport()->setFocusPolicy(Qt::StrongFocus);
//	qtractorScrollView::viewport()->setFocusProxy(this);
	qtractorScrollView::viewport()->setAcceptDrops(true);
//	qtractorScrollView::setDragAutoScroll(false);
	qtractorScrollView::setMouseTracking(true);

	const QFont& font = qtractorScrollView::font();
	qtractorScrollView::setFont(QFont(font.family(), font.pointSize() - 1));

//	QObject::connect(this, SIGNAL(contentsMoving(int,int)),
//		this, SLOT(updatePixmap(int,int)));

	// Trap for help/tool-tips events.
	qtractorScrollView::viewport()->installEventFilter(this);
}


// Destructor.
qtractorTrackView::~qtractorTrackView (void)
{
	clear();

	delete m_pClipSelect;
}


// Track view state reset.
void qtractorTrackView::clear (void)
{
	g_clipboard.clear();

	m_pClipSelect->clear();

	m_dropType   = qtractorTrack::None;
	m_dragState  = DragNone;
	m_dragCursor = DragNone;
	m_iDraggingX = 0;
	m_pClipDrag  = NULL;
	m_bDragTimer = false;

	m_iPlayHead  = 0;

	m_iPlayHeadX = 0;
	m_iEditHeadX = 0;
	m_iEditTailX = 0;

	m_iLastRecordX = 0;

	m_iPasteCount  = 0;
	m_iPastePeriod = 0;

	if (m_pSessionCursor)
		delete m_pSessionCursor;
	m_pSessionCursor = NULL;

	if (m_pRubberBand)
		delete m_pRubberBand;
	m_pRubberBand = NULL;

	qtractorScrollView::setContentsPos(0, 0);
}


// Update track view content height.
void qtractorTrackView::updateContentsHeight (void)
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Allways give some room to drop something at the bottom...
	int iContentsHeight = qtractorTrack::HeightBase << 1;
	// Compute total track height...
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		iContentsHeight += pTrack->zoomHeight();
		pTrack = pTrack->next();
	}

#ifdef CONFIG_DEBUG_0
	qDebug("qtractorTrackView::updateContentsHeight()");
		" => iContentsHeight=%d", iContentsHeight);
#endif

	// Do the contents resize thing...
	qtractorScrollView::resizeContents(
		qtractorScrollView::contentsWidth(), iContentsHeight);

	// Keep selection (we'll update all contents anyway)...
	updateClipSelect();
}


// Update track view content width.
void qtractorTrackView::updateContentsWidth ( int iContentsWidth )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		int iSessionWidth = pSession->pixelFromFrame(pSession->sessionLength());
		if (iContentsWidth < iSessionWidth)
			iContentsWidth = iSessionWidth;
		qtractorTimeScale::Cursor cursor(pSession->timeScale());
		qtractorTimeScale::Node *pNode = cursor.seekPixel(iContentsWidth);
		iContentsWidth += pNode->pixelFromBeat(
			pNode->beat + 2 * pNode->beatsPerBar) - pNode->pixel;
		if (iContentsWidth < qtractorScrollView::width())
			iContentsWidth += qtractorScrollView::width();
		m_iPlayHeadX = pSession->pixelFromFrame(pSession->playHead());
		m_iEditHeadX = pSession->pixelFromFrame(pSession->editHead());
		m_iEditTailX = pSession->pixelFromFrame(pSession->editTail());
	}

#ifdef CONFIG_DEBUG_0
	qDebug("qtractorTrackView::updateContentsWidth()");
		" => iContentsWidth=%d", iContentsWidth);
#endif

	// Do the contents resize thing...
	qtractorScrollView::resizeContents(
		iContentsWidth, qtractorScrollView::contentsHeight());

	// Keep selection (we'll update all contents anyway)...
	updateClipSelect();

	// Force an update on the track time line too...
	m_pTracks->trackTime()->resizeContents(
		iContentsWidth + 100, m_pTracks->trackTime()->contentsHeight());
	m_pTracks->trackTime()->updateContents();
}


// Local rectangular contents update.
void qtractorTrackView::updateContents ( const QRect& rect )
{
	updatePixmap(
		qtractorScrollView::contentsX(), qtractorScrollView::contentsY());

	qtractorScrollView::updateContents(rect);
}


// Overall contents update.
void qtractorTrackView::updateContents (void)
{
	updatePixmap(
		qtractorScrollView::contentsX(), qtractorScrollView::contentsY());

	qtractorScrollView::updateContents();
}


// Special recording visual feedback.
void qtractorTrackView::updateContentsRecord (void)
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	int cx = qtractorScrollView::contentsX();
	int x  = cx;
	int w  = qtractorScrollView::width();

	int iCurrRecordX = m_iPlayHeadX;
	if (iCurrRecordX > x + w)
		iCurrRecordX = x + w;

	if (m_iLastRecordX < iCurrRecordX) {
		if (pSession->midiRecord() < 1 &&
			x < m_iLastRecordX && m_iLastRecordX < x + w)
			x = m_iLastRecordX - 8;
		w = iCurrRecordX - x + 8;
		qtractorScrollView::viewport()->update(
			QRect(x - cx, 0, w, qtractorScrollView::height()));
	}
	else if (m_iLastRecordX > iCurrRecordX)
		qtractorScrollView::viewport()->update();

	m_iLastRecordX = iCurrRecordX;
}


// Draw the track view.
void qtractorTrackView::drawContents ( QPainter *pPainter, const QRect& rect )
{
	// Draw viewport canvas...
	pPainter->drawPixmap(rect, m_pixmap, rect);

	// Lines a-head...
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	int cx = qtractorScrollView::contentsX();
	int cy = qtractorScrollView::contentsY();
	int ch = qtractorScrollView::contentsHeight();
	int x;

	// On-the-fly recording clip drawing...
	if (pSession->isRecording() && pSession->isPlaying()) {
		unsigned long iTrackStart = pSession->frameFromPixel(cx + rect.x());
		unsigned long iTrackEnd   = pSession->playHead();
		if (iTrackStart < iTrackEnd) {
			int y1 = 0;
			int y2 = 0;
			unsigned long iFramePos = pSession->framePos();
			unsigned long iFrameOffset = iFramePos - iTrackEnd;
			qtractorTrack *pTrack = pSession->tracks().first();
			while (pTrack && y2 < ch) {
				y1  = y2;
				y2 += pTrack->zoomHeight();
				// Dispatch to paint this track...
				qtractorClip *pClipRecord = pTrack->clipRecord();
				if (pClipRecord && y2 > cy) {
					int h = y2 - y1 - 2;
					const QRect trackRect(
						rect.left() - 1, y1 - cy + 1, rect.width() + 2, h);
					unsigned long iClipStart  = pClipRecord->clipStart();
					unsigned long iClipOffset = iFrameOffset;
					if (pSession->isLooping()) {
						unsigned long iLoopStart = pSession->loopStart();
						unsigned long iLoopEnd   = pSession->loopEnd();
						if (iClipStart > iLoopStart && iClipStart < iLoopEnd
							&& iFramePos > iLoopEnd) {
							iClipOffset -= iClipStart - iLoopStart;
							iClipStart = iLoopStart;
						}
					}
					if (iClipStart < iTrackStart)
						iClipOffset += iTrackStart - iClipStart;
					x = pSession->pixelFromFrame(iClipStart) - cx;
					int w = 0;
					if (iClipStart < iTrackEnd)
						w += pSession->pixelFromFrame(iTrackEnd - iClipStart);
					const QRect& clipRect
						= QRect(x, y1 - cy + 1, w, h).intersect(trackRect);
					if (!clipRect.isEmpty()) {
					#if 0
						// Just draw a semi-transparent rectangle...
						QColor rgbPen   = pTrack->background().darker();
						QColor rgbBrush = pTrack->background();
						rgbPen.setAlpha(120);
						rgbBrush.setAlpha(120);
						pPainter->setPen(rgbPen);
						pPainter->setBrush(rgbBrush);
						pPainter->drawRect(clipRect);
					#else
						pClipRecord->drawClip(pPainter, clipRect, iClipOffset);
					#endif
					}
				}
				pTrack = pTrack->next();
			}
		}
	}

	// Draw edit-head line...
//	m_iEditHeadX = pSession->pixelFromFrame(pSession->editHead());
	x = m_iEditHeadX - cx;
	if (x >= rect.left() && x <= rect.right()) {
		pPainter->setPen(Qt::blue);
		pPainter->drawLine(x, rect.top(), x, rect.bottom());
	}

	// Draw edit-tail line...
//	m_iEditTailX = pSession->pixelFromFrame(pSession->editTail());
	x = m_iEditTailX - cx;
	if (x >= rect.left() && x <= rect.right()) {
		pPainter->setPen(Qt::blue);
		pPainter->drawLine(x, rect.top(), x, rect.bottom());
	}

	// Draw play-head line...
//	m_iPlayHeadX = pSession->pixelFromFrame(pSession->playHead());
	x = m_iPlayHeadX - cx;
	if (x >= rect.left() && x <= rect.right()) {
		pPainter->setPen(Qt::red);
		pPainter->drawLine(x, rect.top(), x, rect.bottom());
	}

	// Show/hide a moving clip fade in/out slope lines...
	if (m_dragState == DragFadeIn || m_dragState == DragFadeOut) {
		QRect rectHandle(m_rectHandle);
		// Horizontal adjust...
		rectHandle.translate(m_iDraggingX, 0);
		// Convert rectangle into view coordinates...
		rectHandle.moveTopLeft(contentsToViewport(rectHandle.topLeft()));
		// Draw envelope line...
		QPoint vpos;
		pPainter->setPen(QColor(0, 0, 255, 120));
		if (m_dragState == DragFadeIn) {
			vpos = contentsToViewport(m_rectDrag.bottomLeft());
			pPainter->drawLine(
				vpos.x(), vpos.y(), rectHandle.left(), rectHandle.top());
		} 
		else 
		if (m_dragState == DragFadeOut) {
			vpos = contentsToViewport(m_rectDrag.bottomRight());
			pPainter->drawLine(
				rectHandle.right(), rectHandle.top(), vpos.x(), vpos.y());
		}
	}
}


// Resize event handler.
void qtractorTrackView::resizeEvent ( QResizeEvent *pResizeEvent )
{
	qtractorScrollView::resizeEvent(pResizeEvent);

#if QT_VERSION >= 0x040201
	// Corner tool widget layout management...
	if (m_pXzoomReset) {
		const QSize& size = qtractorScrollView::size();
		int h = size.height();
		int w = qtractorScrollView::style()->pixelMetric(
			QStyle::PM_ScrollBarExtent);
		int x = size.width() - w - 2;
		m_pXzoomReset->setGeometry(x, h - w - 2, w, w);
	}
#else
	// Scrollbar/tools layout management.
	const QSize& size = qtractorScrollView::size();
	QScrollBar *pVScrollBar = qtractorScrollView::verticalScrollBar();
	if (pVScrollBar->isVisible()) {
		int h = size.height();
		int w = pVScrollBar->width(); 
		int x = size.width() - w - 1;
		pVScrollBar->setFixedHeight(h - w * 3 - 2);
		if (m_pVzoomIn)
			m_pVzoomIn->setGeometry(x, h - w * 3, w, w);
		if (m_pVzoomOut)
			m_pVzoomOut->setGeometry(x, h - w * 2, w, w);
		if (m_pXzoomReset)
			m_pXzoomReset->setGeometry(x, h - w - 1, w, w);
	}
	QScrollBar *pHScrollBar = qtractorScrollView::horizontalScrollBar();
	if (pHScrollBar->isVisible()) {
		int w = size.width();
		int h = pHScrollBar->height(); 
		int y = size.height() - h - 1;
		pHScrollBar->setFixedWidth(w - h * 3 - 2);
		if (m_pHzoomOut)
			m_pHzoomOut->setGeometry(w - h * 3, y, h, h);
		if (m_pHzoomIn)
			m_pHzoomIn->setGeometry(w - h * 2, y, h, h);
	}
#endif

	updateContents();

	// HACK: let our (single) thumb view get notified...
	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm && pMainForm->thumbView())
		pMainForm->thumbView()->updateThumb();
}


// (Re)create the complete track view pixmap.
void qtractorTrackView::updatePixmap ( int cx, int cy )
{
	QWidget *pViewport = qtractorScrollView::viewport();
	int w = pViewport->width();
	int h = pViewport->height();

	const QPalette& pal = qtractorScrollView::palette();

	if (w < 1 || h < 1)
		return;

	m_pixmap = QPixmap(w, h);
	m_pixmap.fill(pal.dark().color());

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	QPainter painter(&m_pixmap);
	painter.initFrom(this);

	// Update view session cursor location,
	// so that we'll start drawing clips from there...
	unsigned long iTrackStart = pSession->frameFromPixel(cx);
	unsigned long iTrackEnd   = iTrackStart + pSession->frameFromPixel(w);
	// Create cursor now if applicable...
	if (m_pSessionCursor == NULL) {
		m_pSessionCursor = pSession->createSessionCursor(iTrackStart);
	} else {
		m_pSessionCursor->seek(iTrackStart);
	}

	const QColor& rgbLight = pal.midlight().color().darker(120);
	const QColor& rgbMid   = pal.mid().color();
	const QColor& rgbDark  = rgbMid.darker(120);
	
	// Draw track and horizontal lines...
	int x, y1, y2;
	y1 = y2 = 0;
	int iTrack = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack && y2 < cy + h) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		if (y2 > cy) {
			// Dispatch to paint this track...
			if (y1 > cy) {
				painter.setPen(rgbLight);
				painter.drawLine(0, y1 - cy, w, y1 - cy);
			}
			QRect trackRect(0, y1 - cy + 1, w, y2 - y1 - 2);
			painter.fillRect(trackRect, rgbMid);
			pTrack->drawTrack(&painter, trackRect, iTrackStart, iTrackEnd,
				m_pSessionCursor->clip(iTrack));
			painter.setPen(rgbDark);
			painter.drawLine(0, y2 - cy - 1, w, y2 - cy - 1);
		}
		pTrack = pTrack->next();
		iTrack++;
	}

	// Draw vertical grid lines...
	if (cy < y2) {
		qtractorTimeScale::Cursor cursor(pSession->timeScale());
		qtractorTimeScale::Node *pNode = cursor.seekPixel(cx);
		unsigned short iPixelsPerBeat = pNode->pixelsPerBeat();
		unsigned int iBeat = pNode->beatFromPixel(cx);
		int x0 = x = pNode->pixelFromBeat(iBeat) - cx;
		while (x < w) {
			if (x >= 0) {
				bool bBeatIsBar = pNode->beatIsBar(iBeat) && (x >= x0);
				if (bBeatIsBar) {
					painter.setPen(rgbLight);
					painter.drawLine(x, 0, x, y2 - cy - 2);
					x0 = x + 16;
					if (iBeat == pNode->beat)
						iPixelsPerBeat = pNode->pixelsPerBeat();
				}
				if (bBeatIsBar || iPixelsPerBeat > 16) {
					painter.setPen(rgbDark);
					painter.drawLine(x - 1, 0, x - 1, y2 - cy - 2);
				}
			}
			pNode = cursor.seekBeat(++iBeat);
			x = pNode->pixelFromBeat(iBeat) - cx;
		}
	}

	// Fill the empty area...
	if (y2 < cy + h) {
		painter.setPen(rgbMid);
		painter.drawLine(0, y2 - cy, w, y2 - cy);
	//	painter.fillRect(0, y2 - cy + 1, w, h, pal.dark().color());
	}

	// Draw loop boundaries, if applicable...
	if (pSession->isLooping()) {
		painter.setPen(Qt::darkCyan);
		x = pSession->pixelFromFrame(pSession->loopStart()) - cx;
		if (x >= 0 && x < w)
			painter.drawLine(x, 0, x, h);
		x = pSession->pixelFromFrame(pSession->loopEnd()) - cx;
		if (x >= 0 && x < w)
			painter.drawLine(x, 0, x, h);
	}

	// Draw punch boundaries, if applicable...
	if (pSession->isPunching()) {
		painter.setPen(Qt::darkMagenta);
		x = pSession->pixelFromFrame(pSession->punchIn()) - cx;
		if (x >= 0 && x < w)
			painter.drawLine(x, 0, x, h);
		x = pSession->pixelFromFrame(pSession->punchOut()) - cx;
		if (x >= 0 && x < w)
			painter.drawLine(x, 0, x, h);
	}
}


// To have track view in v-sync with track list.
void qtractorTrackView::contentsYMovingSlot ( int /*cx*/, int cy )
{
	if (qtractorScrollView::contentsY() != cy)
		qtractorScrollView::setContentsPos(qtractorScrollView::contentsX(), cy);
}


// Get track from given contents vertical position.
qtractorTrack *qtractorTrackView::trackAt ( const QPoint& pos,
	bool bSelectTrack, qtractorTrackViewInfo *pTrackViewInfo ) const
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL || m_pSessionCursor == NULL)
		return NULL;

	int y1 = 0;
	int y2 = 0;
	int iTrack = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		if (y2 > pos.y())
			break;
		pTrack = pTrack->next();
		iTrack++;
	}

	if (pTrack && bSelectTrack)
		m_pTracks->trackList()->setCurrentTrackRow(iTrack);

	if (pTrackViewInfo) {
		int x = qtractorScrollView::contentsX();
		int w = qtractorScrollView::width();// View width, not contents.
		if (pTrack == NULL) {				// Below all tracks.
			y1 = y2;
			y2 = y1 + (qtractorTrack::HeightBase
				* pSession->verticalZoom()) / 100;
		}
		pTrackViewInfo->trackIndex = iTrack;
		pTrackViewInfo->trackStart = m_pSessionCursor->frame();
		pTrackViewInfo->trackEnd   = pTrackViewInfo->trackStart
			+ pSession->frameFromPixel(w);
		pTrackViewInfo->trackRect.setRect(x, y1 + 1, w, y2 - y1 - 2);
	}

	return pTrack;
}


// Get clip from given contents position.
qtractorClip *qtractorTrackView::clipAt ( const QPoint& pos,
	bool bSelectTrack, QRect *pClipRect ) const
{
	qtractorTrackViewInfo tvi;
	qtractorTrack *pTrack = trackAt(pos, bSelectTrack, &tvi);
	if (pTrack == NULL)
		return NULL;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return NULL;

	if (m_pSessionCursor == NULL)
		return NULL;

	qtractorClip *pClip = m_pSessionCursor->clip(tvi.trackIndex);
	if (pClip == NULL)
		pClip = pTrack->clips().first();
	if (pClip == NULL)
		return NULL;

	qtractorClip *pClipAt = NULL;
	while (pClip && pClip->clipStart() < tvi.trackEnd) {
		int x = pSession->pixelFromFrame(pClip->clipStart());
		int w = pSession->pixelFromFrame(pClip->clipLength());
		if (pos.x() >= x && x + w >= pos.x()) {
			pClipAt = pClip;
			if (pClipRect)
				pClipRect->setRect(
					x, tvi.trackRect.y(), w, tvi.trackRect.height());
		}
		pClip = pClip->next();
	}

	return pClipAt;
}


// Get contents visible rectangle from given track.
bool qtractorTrackView::trackInfo ( qtractorTrack *pTrackPtr,
	qtractorTrackViewInfo *pTrackViewInfo ) const
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL || m_pSessionCursor == NULL)
		return false;

	int y1, y2 = 0;
	int iTrack = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		if (pTrack == pTrackPtr) {
			int x = qtractorScrollView::contentsX();
			int w = qtractorScrollView::width();   	// View width, not contents.
			pTrackViewInfo->trackIndex = iTrack;
			pTrackViewInfo->trackStart = m_pSessionCursor->frame();
			pTrackViewInfo->trackEnd   = pTrackViewInfo->trackStart
				+ pSession->frameFromPixel(w);
			pTrackViewInfo->trackRect.setRect(x, y1 + 1, w, y2 - y1 - 2);
			return true;
		}
		pTrack = pTrack->next();
		iTrack++;
	}

	return false;
}


// Get contents rectangle from given clip.
bool qtractorTrackView::clipInfo ( qtractorClip *pClip,
	QRect *pClipRect ) const
{
	qtractorTrackViewInfo tvi;
	if (!trackInfo(pClip->track(), &tvi))
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		int x = pSession->pixelFromFrame(pClip->clipStart());
		int w = pSession->pixelFromFrame(pClip->clipLength());
		pClipRect->setRect(x, tvi.trackRect.y(), w, tvi.trackRect.height());
	}

	return true;
}


// Selection-dragging, following the current mouse position.
qtractorTrack *qtractorTrackView::dragMoveTrack ( const QPoint& pos,
	bool bKeyStep )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return NULL;

	// Which track we're pointing at?
	qtractorTrackViewInfo tvi;
	qtractorTrack *pTrack = trackAt(pos, true, &tvi);

	// May change vertically, if we've only one track selected,
	// and only between same track type...
	qtractorTrack *pSingleTrack = m_pClipSelect->singleTrack();
	if (pSingleTrack &&
		(pTrack == NULL || pSingleTrack->trackType() == pTrack->trackType()))
		updateSingleTrack(tvi.trackRect.y() + 1, tvi.trackRect.height() - 2);
	// Special update on keyboard vertical drag-stepping...
	if (bKeyStep)
		m_posStep.setY(m_posStep.y() - pos.y() + tvi.trackRect.y()
			+ (pTrack ? (tvi.trackRect.height() >> 1) : 0));

	// Always change horizontally wise...
	int  x = m_pClipSelect->rect().x();
	int dx = (pos.x() - m_posDrag.x());
	if (x + dx < 0)
		dx = -(x);	// Force to origin (x=0).
	m_iDraggingX = (pSession->pixelSnap(x + dx) - x);
	qtractorScrollView::ensureVisible(pos.x(), pos.y(), 24, 24);

	showClipSelect();

	// OK, we've moved it...
	return pTrack;
}


qtractorTrack *qtractorTrackView::dragDropTrack (
	const QPoint& pos, bool bKeyStep, const QMimeData *pMimeData )
{
	// It must be a valid session...
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return NULL;

	// Find the current pointer track...
	qtractorTrackViewInfo tvi;
	qtractorTrack *pTrack = trackAt(pos, true, &tvi);

	// Special update on keyboard vertical drag-stepping...
	if (bKeyStep)
		m_posStep.setY(m_posStep.y() - pos.y() + tvi.trackRect.y()
			+ (pTrack ? (tvi.trackRect.height() >> 1) : 0));

	// If we're already dragging something,
	if (!m_dropItems.isEmpty()) {
		// Adjust to target track...
		updateDropRects(tvi.trackRect.y() + 1, tvi.trackRect.height() - 2);
		// Always change horizontally wise...
		int  x = m_rectDrag.x();
		int dx = (pos.x() - m_posDrag.x());
		if (x + dx < 0)
			dx = -(x);	// Force to origin (x=0).
		m_iDraggingX = (pSession->pixelSnap(x + dx) - x);
	//	showDropRects();
		// OK, we've moved it...
		return pTrack;
	}

	// Let's start from scratch...
	qDeleteAll(m_dropItems);
	m_dropItems.clear();
	m_dropType = qtractorTrack::None;

	// Nothing more?
	if (pMimeData == NULL)
		return NULL;
	// Can it be single track channel (MIDI for sure)?
	if (qtractorFileChannelDrag::canDecode(pMimeData)) {
		// Let's see how many track-channels are there...
		const qtractorFileChannelDrag::List& items
			= qtractorFileChannelDrag::decode(pMimeData);
		QListIterator<qtractorFileChannelDrag::Item> iter(items);
		while (iter.hasNext()) {
			const qtractorFileChannelDrag::Item& item = iter.next();
			m_dropItems.append(new DropItem(item.path, item.channel));
		}
	}
	else
	// Can we decode it as Audio/MIDI files?
	if (pMimeData->hasUrls()) {
		// Let's see how many files there are...
		QList<QUrl> list = pMimeData->urls();
		QListIterator<QUrl> iter(list);
		while (iter.hasNext()) {
			const QString& sPath = iter.next().toLocalFile();
			// Close current session and try to load the new one...
			if (!sPath.isEmpty())
				m_dropItems.append(new DropItem(sPath));
		}
	}

	// Nice, now we'll try to set a preview selection rectangle set...
	m_posDrag.setX(pSession->pixelSnap(pos.x() > 8 ? pos.x() - 8 : 0));
	m_posDrag.setY(tvi.trackRect.y() + 1);
	m_rectDrag.setRect(
		m_posDrag.x(), m_posDrag.y(), 0, tvi.trackRect.height() - 2);

	// Now's time to add those rectangles...
	QMutableListIterator<DropItem *> iter(m_dropItems);
	while (iter.hasNext()) {
		DropItem *pDropItem = iter.next();
		// First test as a MIDI file...
		if (m_dropType == qtractorTrack::None
			|| m_dropType == qtractorTrack::Midi) {
			int x0 = m_posDrag.x();
			qtractorTimeScale::Cursor cursor(pSession->timeScale());
			qtractorTimeScale::Node *pNode = cursor.seekPixel(x0);
			unsigned long t1, t0 = pNode->tickFromPixel(x0);
			qtractorMidiFile file;
			if (file.open(pDropItem->path)) {
				qtractorMidiSequence seq;
				seq.setTicksPerBeat(pSession->ticksPerBeat());
				if (pDropItem->channel < 0) {
					int iTracks = (file.format() == 1 ? file.tracks() : 16);
					for (int iTrackChannel = 0;
							iTrackChannel < iTracks; iTrackChannel++) {
						if (file.readTrack(&seq, iTrackChannel)
							&& seq.duration() > 0) {
							t1 = t0 + seq.duration();
							pNode = cursor.seekTick(t1);
							m_rectDrag.setWidth(pNode->pixelFromTick(t1) - x0);
							pDropItem->rect = m_rectDrag;
							m_rectDrag.translate(0, m_rectDrag.height() + 4);
						}
					}
					if (m_dropType == qtractorTrack::None)
						m_dropType = qtractorTrack::Midi;
				} else if (file.readTrack(&seq, pDropItem->channel)
					&& seq.duration() > 0) {
					t1 = t0 + seq.duration();
					pNode = cursor.seekTick(t1);
					m_rectDrag.setWidth(pNode->pixelFromTick(t1) - x0);
					pDropItem->rect = m_rectDrag;
					m_rectDrag.translate(0, m_rectDrag.height() + 4);
					if (m_dropType == qtractorTrack::None)
						m_dropType = qtractorTrack::Midi;
				} else /*if (m_dropType == qtractorTrack::Midi)*/ {
					iter.remove();
					delete pDropItem;
				}
				file.close();
				continue;
			} else if (m_dropType == qtractorTrack::Midi) {
				iter.remove();
				delete pDropItem;
			}
		}
		// Then as an Audio file ?
		if (m_dropType == qtractorTrack::None
			|| m_dropType == qtractorTrack::Audio) {
			qtractorAudioFile *pFile
				= qtractorAudioFileFactory::createAudioFile(pDropItem->path);
			if (pFile) {
				if (pFile->open(pDropItem->path)) {
					unsigned long iFrames = pFile->frames();
					if (pFile->sampleRate() > 0
						&& pFile->sampleRate() != pSession->sampleRate()) {
						iFrames = (unsigned long) (iFrames
							* float(pSession->sampleRate())
							/ float(pFile->sampleRate()));
					}
					m_rectDrag.setWidth(pSession->pixelFromFrame(iFrames));
					pDropItem->rect = m_rectDrag;
					m_rectDrag.translate(0, m_rectDrag.height() + 4);
					if (m_dropType == qtractorTrack::None)
						m_dropType = qtractorTrack::Audio;
				} else if (m_dropType == qtractorTrack::Audio) {
					iter.remove();
					delete pDropItem;
				}
				delete pFile;
				continue;
			} else if (m_dropType == qtractorTrack::Audio) {
				iter.remove();
				delete pDropItem;
			}
		}
	}

	// Are we still here?
	if (m_dropItems.isEmpty() || m_dropType == qtractorTrack::None) {
		m_dropType = qtractorTrack::None;
		return NULL;
	}

	// Ok, sure we're into some drag state...
	m_dragState = DragDrop;
	m_iDraggingX = 0;	

	// Finally, show it to the world...
	updateDropRects(tvi.trackRect.y() + 1, tvi.trackRect.height() - 2);
//	showDropRects();

	// Done.
	return pTrack;
}


qtractorTrack *qtractorTrackView::dragDropEvent ( QDropEvent *pDropEvent )
{
	return dragDropTrack(
		viewportToContents(pDropEvent->pos()),
		false, pDropEvent->mimeData());
}


bool qtractorTrackView::canDropEvent ( QDropEvent *pDropEvent )
{
	// have one existing track on target?
	qtractorTrack *pTrack = dragDropEvent(pDropEvent);

	// Can only drop if anything...
	if (m_dropItems.isEmpty())
		return false;

	// Can only drop on same type tracks...
	if (pTrack && pTrack->trackType() != m_dropType)
		return false;

	// Special MIDI track-channel cases...
	if (m_dropType == qtractorTrack::Midi) {
		if (m_dropItems.count() == 1 && m_dropItems.first()->channel >= 0)
			return true;
		else
			return (pTrack == NULL);
	}

	// Drop in the blank...
	return (pTrack == NULL || m_dropItems.count() == 1 || m_bDropSpan);
}


// Drag enter event handler.
void qtractorTrackView::dragEnterEvent ( QDragEnterEvent *pDragEnterEvent )
{
#if 0
	if (canDropEvent(pDragEnterEvent)) {
		showDropRects();
		if (!pDragEnterEvent->isAccepted()) {
			pDragEnterEvent->setDropAction(Qt::CopyAction);
			pDragEnterEvent->accept();
			m_bDragTimer = false;
		}
	} else {
		pDragEnterEvent->ignore();
		hideDropRects();
	}
#else
	// Always accept the drag-enter event,
	// so let we deal with it during move later...
	pDragEnterEvent->accept();
	m_bDragTimer = false;
#endif
}


// Drag move event handler.
void qtractorTrackView::dragMoveEvent ( QDragMoveEvent *pDragMoveEvent )
{
	if (canDropEvent(pDragMoveEvent)) {
		showDropRects();
		if (!pDragMoveEvent->isAccepted()) {
			pDragMoveEvent->setDropAction(Qt::CopyAction);
			pDragMoveEvent->accept();
			m_bDragTimer = false;
		}
	} else {
		pDragMoveEvent->ignore();
		hideDropRects();
	}

	// Kind of auto-scroll...
	const QPoint& pos = viewportToContents(pDragMoveEvent->pos());
	qtractorScrollView::ensureVisible(pos.x(), pos.y(), 24, 24);
}


// Drag leave event handler.
void qtractorTrackView::dragLeaveEvent ( QDragLeaveEvent *pDragLeaveEvent )
{
	// Maybe we have something currently going on?
	if (pDragLeaveEvent->isAccepted()) {
		if (!m_bDragTimer) {
			m_bDragTimer = true;
			QTimer::singleShot(100, this, SLOT(dragTimeout()));
		}
	} else {
		// Nothing's being accepted...
		m_bDragTimer = false;
		resetDragState();
	}
}


// Drag timeout slot.
void qtractorTrackView::dragTimeout (void)
{
	if (m_bDragTimer) {
		resetDragState();
		m_bDragTimer = false;
	}
}


// Drop event handler.
bool qtractorTrackView::dropTrack ( const QPoint& pos, const QMimeData *pMimeData )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	// Add new clips on proper and consecutive track locations...
	unsigned long iClipStart = pSession->frameSnap(
		pSession->frameFromPixel(m_rectDrag.x() + m_iDraggingX));

	// Now check whether the drop is intra-track...
	qtractorTrack *pTrack = dragDropTrack(pos, false, pMimeData);
	// And care if we're not spanning horizontally...
	if (pTrack == NULL
		&& (!m_bDropSpan || m_dropType == qtractorTrack::Midi)) {
		// Do we have something to drop anyway?
		// if yes, this is a extra-track drop...
		int iAddTrack = 0;
		if (!m_dropItems.isEmpty()) {
			// Prepare file list for import...
			QStringList files;
			QListIterator<DropItem *> iter(m_dropItems);
			while (iter.hasNext()) {
				DropItem *pDropItem = iter.next();
				if (m_dropType == qtractorTrack::Midi
					&& pDropItem->channel >= 0) {
					m_pTracks->addMidiTrackChannel(
						pDropItem->path, pDropItem->channel, iClipStart);
					iAddTrack++;
				} else  {
					files.append(pDropItem->path);
				}
			}
			// Depending on import type...
			if (!files.isEmpty()) {
				switch (m_dropType) {
				case qtractorTrack::Audio:
					m_pTracks->addAudioTracks(files, iClipStart);
					iAddTrack++;
					break;
				case qtractorTrack::Midi:
					m_pTracks->addMidiTracks(files, iClipStart);
					iAddTrack++;
					break;
				default:
					break;
				}
			}
		}
		resetDragState();
		return (iAddTrack > 0);
	}

	// Check whether we can really drop it.
	if (pTrack && pTrack->trackType() != m_dropType) {
		resetDragState();
		return false;
	}

	// We'll build a composite command...
	qtractorClipCommand *pClipCommand
		= new qtractorClipCommand(tr("add clip"));

	// If dropping spanned we'll need a track, sure...
	int iTrackClip = 0;
	bool bAddTrack = (pTrack == NULL);
	if (bAddTrack) {
		pTrack = new qtractorTrack(pSession, m_dropType);
		// Create a new track right away...
		int iTrack = pSession->tracks().count() + 1;
		const QColor color = qtractorTrack::trackColor(iTrack);
		pTrack = new qtractorTrack(pSession, m_dropType);
		pTrack->setBackground(color);
		pTrack->setForeground(color.darker());
	//	pTrack->setTrackName(tr("Track %1").arg(iTrack));
		pClipCommand->addTrack(pTrack);
	}

	// Now's time to create the clip(s)...
	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	QListIterator<DropItem *> iter(m_dropItems);
	while (iter.hasNext()) {
		DropItem *pDropItem = iter.next();
		switch (pTrack->trackType()) {
		case qtractorTrack::Audio: {
			qtractorAudioClip *pAudioClip = new qtractorAudioClip(pTrack);
			if (pAudioClip) {
				pAudioClip->setFilename(pDropItem->path);
				pAudioClip->setClipStart(iClipStart);
				pClipCommand->addClip(pAudioClip, pTrack);
				// Don't forget to add this one to local repository.
				if (pMainForm)
					pMainForm->addAudioFile(pDropItem->path);
			}
			break;
		}
		case qtractorTrack::Midi: {
			qtractorMidiClip *pMidiClip = new qtractorMidiClip(pTrack);
			if (pMidiClip) {
				pMidiClip->setFilename(pDropItem->path);
				pMidiClip->setTrackChannel(pDropItem->channel);
				pMidiClip->setClipStart(iClipStart);
				pClipCommand->addClip(pMidiClip, pTrack);
				// Don't forget to add this one to local repository.
				if (pMainForm)
					pMainForm->addMidiFile(pDropItem->path);
			}
			break;
		}
		case qtractorTrack::None:
		default:
			break;
		}
		// If track's new it will need a name...
		if (bAddTrack && iTrackClip == 0)
			pTrack->setTrackName(QFileInfo(pDropItem->path).baseName());
		// If multiple items, just snap/concatenate them...
		iClipStart = pSession->frameSnap(iClipStart
			+ pSession->frameFromPixel(pDropItem->rect.width()));
		iTrackClip++;
	}

	// Clean up.
	resetDragState();

	// Put it in the form of an undoable command...
	return pSession->execute(pClipCommand);
}


void qtractorTrackView::dropEvent ( QDropEvent *pDropEvent )
{
	if (!dropTrack(viewportToContents(pDropEvent->pos()), pDropEvent->mimeData())) {
		qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
		if (pMainForm)
			pMainForm->dropEvent(pDropEvent);
	}
}


// Handle item selection/dragging -- mouse button press.
void qtractorTrackView::mousePressEvent ( QMouseEvent *pMouseEvent )
{
	// Which mouse state?
	const bool bModifier = (pMouseEvent->modifiers()
		& (Qt::ShiftModifier | Qt::ControlModifier));
	const QPoint& pos = viewportToContents(pMouseEvent->pos());

	// Are we already step-moving or pasting something?
	switch (m_dragState) {
	case DragStep:
		// One-click change from drag-step to drag-move...
		m_dragState = DragMove;
		m_posDrag   = m_rectDrag.center();
		m_posStep   = QPoint(0, 0);
		dragMoveTrack(pos + m_posStep);
		// Fall thru...
	case DragPaste:
	case DragDropPaste:
		qtractorScrollView::mousePressEvent(pMouseEvent);
		return;
	default:
		break;
	}

	// Force null state.
	m_pClipDrag = NULL;
	resetDragState();

	// We need a session and a location...
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		// Direct snap positioning...
		unsigned long iFrame = pSession->frameSnap(
			pSession->frameFromPixel(pos.x() > 0 ? pos.x() : 0));
		// Which button is being pressed?
		switch (pMouseEvent->button()) {
		case Qt::LeftButton:
			// Remember what and where we'll be dragging/selecting...
			m_dragState = DragStart;
			m_posDrag   = pos;
			m_pClipDrag = clipAt(m_posDrag, true, &m_rectDrag);
			// Should it be selected(toggled)?
			if (m_pClipDrag && !dragFadeResizeStart(pos)) {
				// Show that we're about to something...
				m_dragCursor = m_dragState;
				qtractorScrollView::setCursor(QCursor(Qt::PointingHandCursor));
				// Make it (un)selected, right on the file view too...
				if (m_selectMode == SelectClip)
					selectClipFile(!bModifier);
			}
			// Something got it started?...
			if (m_pClipDrag == NULL
				|| (m_pClipDrag && !m_pClipDrag->isClipSelected())) {
				// Clear any selection out there?
				if (!bModifier /* || m_selectMode != SelectClip */)
					selectAll(false);
			}
			break;
		case Qt::MidButton:
			// Mid-button positioning...
			selectAll(false);
			// Edit cursor positioning...
			setEditHead(iFrame);
			setEditTail(iFrame);
			// Not quite a selection, but some visual feedback...
			m_pTracks->selectionChangeNotify();
			break;
		case Qt::RightButton:
			// Have no sense if pointer falls over a clip...
			m_pClipDrag = clipAt(pos);
			if (m_pClipDrag == NULL) {
				// Right-button edit-tail positioning...
				setEditTail(iFrame);
				// Not quite a selection, but some visual feedback...
				m_pTracks->selectionChangeNotify();
			}
			// Fall thru...
		default:
			break;
		}
	}

	qtractorScrollView::mousePressEvent(pMouseEvent);
}


// Handle item selection/dragging -- mouse pointer move.
void qtractorTrackView::mouseMoveEvent ( QMouseEvent *pMouseEvent )
{
	// Are we already moving/dragging something?
	const QPoint& pos = viewportToContents(pMouseEvent->pos());

	switch (m_dragState) {
	case DragNone:
		// Try to catch mouse over the fade or resize handles...
		dragFadeResizeStart(pos);
		break;
	case DragMove:
	case DragPaste:
		dragMoveTrack(pos + m_posStep);
		break;
	case DragDropPaste:
		dragDropTrack(pos + m_posStep);
		showDropRects();
		break;
	case DragFadeIn:
	case DragFadeOut:
		dragFadeMove(pos);
		break;
	case DragResizeLeft:
	case DragResizeRight:
		dragResizeMove(pos);
		break;
	case DragSelect:
		m_rectDrag.setBottomRight(pos);
		moveRubberBand(&m_pRubberBand, m_rectDrag);
		qtractorScrollView::ensureVisible(pos.x(), pos.y(), 24, 24);
		selectRect(m_rectDrag, m_selectMode);
		break;
	case DragStart:
		if ((m_posDrag - pos).manhattanLength()
			> QApplication::startDragDistance()) {
			// Check if we're pointing in some fade-in/out or resize handle...
			if (dragFadeResizeStart(m_posDrag)) {
				m_dragState = m_dragCursor;
				if (m_dragState == DragFadeIn || m_dragState == DragFadeOut) {
					// DragFade...
					m_iDraggingX = (pos.x() - m_posDrag.x());
					qtractorScrollView::setCursor(QCursor(Qt::SizeHorCursor));
					moveRubberBand(&m_pRubberBand, m_rectHandle);					
				} else if (m_pClipDrag) {
					// DragResize...
					moveRubberBand(&m_pRubberBand, m_rectDrag, 3);					
				}
			} else {
				// We'll start dragging clip/regions alright...
				qtractorSession *pSession = qtractorSession::getInstance();
				qtractorClipSelect::Item *pClipItem = NULL;
				if (m_pClipDrag && pSession)
					pClipItem = m_pClipSelect->findClipItem(m_pClipDrag);
				if (pClipItem && pClipItem->rectClip.contains(pos)) {
					int x = pSession->pixelSnap(m_rectDrag.x());
					m_iDraggingX = (x - m_rectDrag.x());
					m_dragState = m_dragCursor = DragMove;
					qtractorScrollView::setCursor(QCursor(Qt::SizeAllCursor));
					showClipSelect();
				} else {
					// We'll start rubber banding...
					m_rectDrag.setTopLeft(m_posDrag);
					m_rectDrag.setBottomRight(pos);
					m_dragState = m_dragCursor = DragSelect;
					// Set a proper cursor...
					qtractorScrollView::setCursor(QCursor(
						m_selectMode == SelectRange ?
							Qt::SizeHorCursor : Qt::CrossCursor));
					// Create the rubber-band if there's none...
					moveRubberBand(&m_pRubberBand, m_rectDrag);
				}
			}
		}
		// Fall thru...
	case DragStep:
	case DragDrop:
	default:
		break;
	}

	qtractorScrollView::mouseMoveEvent(pMouseEvent);
}


// Handle item selection/dragging -- mouse button release.
void qtractorTrackView::mouseReleaseEvent ( QMouseEvent *pMouseEvent )
{
	qtractorScrollView::mouseReleaseEvent(pMouseEvent);

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		// Direct snap positioning...
		const QPoint& pos = viewportToContents(pMouseEvent->pos());
		unsigned long iFrame = pSession->frameSnap(
			pSession->frameFromPixel(m_posDrag.x() > 0 ? m_posDrag.x() : 0));
		// Which mouse state?
		const bool bModifier = (pMouseEvent->modifiers()
			& (Qt::ShiftModifier | Qt::ControlModifier));
		switch (m_dragState) {
		case DragSelect:
			// Here we're mainly supposed to select a few bunch
			// of clips (all that fall inside the rubber-band...
			selectRect(m_rectDrag, m_selectMode);
			// For immediate visual feedback...
			m_pTracks->selectionChangeNotify();
			break;
		case DragMove:
			// Let's move them...
			moveClipSelect(dragMoveTrack(pos + m_posStep));
			break;
		case DragPaste:
			// Let's paste them...
			pasteClipSelect(dragMoveTrack(pos + m_posStep));
			break;
		case DragDropPaste:
			// Let's drop-paste them...
			dropTrack(pos + m_posStep);
			break;
		case DragFadeIn:
		case DragFadeOut:
			dragFadeDrop(pos);
			break;
		case DragResizeLeft:
		case DragResizeRight:
			dragResizeDrop(pos, bModifier);
			break;
		case DragStart:
			// Deferred left-button positioning...
			if (m_pClipDrag) {
				// Make it right on the file view now...
				if (m_selectMode != SelectClip)
					selectClipFile(!bModifier);
				// Nothing more has been deferred...
			} else {
				// Direct play-head positioning...
				if (bModifier) {
					// First, set actual engine position...
					pSession->setPlayHead(iFrame);
					// Play-head positioning...
					setPlayHead(iFrame);
					// Done with (deferred) play-head positioning.
				} else {
					// Deferred left-button edit-head positioning...
					setEditHead(iFrame);
				}
				// Not quite a selection, but for
				// immediate visual feedback...
				m_pTracks->selectionChangeNotify();
			}
			// Fall thru...
		case DragStep:
		case DragDrop:
		case DragNone:
		default:
			break;
		}
	}

	// Force null state.
	resetDragState();
}


// Handle item/clip editing from mouse.
void qtractorTrackView::mouseDoubleClickEvent ( QMouseEvent *pMouseEvent )
{
	qtractorScrollView::mouseDoubleClickEvent(pMouseEvent);

	// By this time we should have something under...
	if (m_pClipDrag)
		m_pTracks->editClip(m_pClipDrag);
	else
		m_pTracks->selectCurrentTrack();
}


// Focus lost event.
void qtractorTrackView::focusOutEvent ( QFocusEvent *pFocusEvent )
{
	if (m_dragState == DragStep || m_dragState == DragPaste || m_dragState == DragDropPaste)
		resetDragState();

	qtractorScrollView::focusOutEvent(pFocusEvent);
}


// Trap for help/tool-tip events.
bool qtractorTrackView::eventFilter ( QObject *pObject, QEvent *pEvent )
{
	QWidget *pViewport = qtractorScrollView::viewport();
	if (static_cast<QWidget *> (pObject) == pViewport) {
		if (pEvent->type() == QEvent::ToolTip) {
			QHelpEvent *pHelpEvent = static_cast<QHelpEvent *> (pEvent);
			if (pHelpEvent) {
				const QPoint& pos
					= qtractorScrollView::viewportToContents(pHelpEvent->pos());
				qtractorClip *pClip = clipAt(pos);
				if (pClip) {
					QToolTip::showText(
						pHelpEvent->globalPos(), pClip->toolTip(), pViewport);
					return true;
				}
			}
		}
		else
		if (pEvent->type() == QEvent::Leave	&&
			m_dragState != DragDropPaste &&
			m_dragState != DragPaste &&
			m_dragState != DragStep) {
			m_dragCursor = DragNone;
			qtractorScrollView::unsetCursor();
			return true;
		}
	}

	// Not handled here.
	return qtractorScrollView::eventFilter(pObject, pEvent);
}


// Clip file(item) selection convenience method.
void qtractorTrackView::selectClipFile ( bool bReset )
{
	if (m_pClipDrag == NULL)
		return;

	// Do the selection dance, first...
	qtractorClipSelect::Item *pClipItem = m_pClipSelect->findClipItem(m_pClipDrag);
	bool bSelect = !(pClipItem && pClipItem->rectClip.contains(m_posDrag));
	if (!bReset) {
		m_pClipSelect->selectClip(m_pClipDrag, m_rectDrag, bSelect);
		updateContents(m_rectDrag);
		m_pTracks->selectionChangeNotify();
	} else if (bSelect || m_selectMode != SelectClip) {
		m_pClipSelect->clear();
		if (bSelect)
			m_pClipSelect->selectClip(m_pClipDrag, m_rectDrag, true);
		updateContents();
		m_pTracks->selectionChangeNotify();
	}

	// Do the file view selection then...
	qtractorTrack *pTrack = m_pClipDrag->track();
	if (pTrack == NULL)
		return;

	qtractorMainForm *pMainForm = qtractorMainForm::getInstance();
	if (pMainForm == NULL)
		return;

	qtractorFiles *pFiles = pMainForm->files();
	if (pFiles == NULL)
		return;

	switch (pTrack->trackType()) {
	case qtractorTrack::Audio: {
		qtractorAudioClip *pAudioClip
			= static_cast<qtractorAudioClip *> (m_pClipDrag);
		if (pAudioClip)
			pFiles->selectAudioFile(pAudioClip->filename());
		break;
	}
	case qtractorTrack::Midi: {
		qtractorMidiClip *pMidiClip
			= static_cast<qtractorMidiClip *> (m_pClipDrag);
		if (pMidiClip)
			pFiles->selectMidiFile(
				pMidiClip->filename(), pMidiClip->trackChannel());
		break;
	}
	default:
		break;
	}
}


// Select everything under a given (rubber-band) rectangle.
void qtractorTrackView::selectRect ( const QRect& rectDrag,
	qtractorTrackView::SelectMode selectMode,
	qtractorTrackView::SelectEdit selectEdit )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	QRect rect(rectDrag.normalized());

	// The precise (snapped) selection frame points...
	unsigned long iSelectStart
		= pSession->frameSnap(pSession->frameFromPixel(rect.left()));
	unsigned long iSelectEnd
		= pSession->frameSnap(pSession->frameFromPixel(rect.right()));

	// Special whole-vertical range case...
	QRect rectRange(0, 0, 0, qtractorScrollView::contentsHeight());
	rectRange.setLeft(pSession->pixelFromFrame(iSelectStart));
	rectRange.setRight(pSession->pixelFromFrame(iSelectEnd));
	if (selectMode == SelectRange) {
		rect.setTop(rectRange.top());
		rect.setBottom(rectRange.height());
	}

	// Let's start invalidading things...
	QRect rectUpdate = m_pClipSelect->rect();
	// Reset all selected clips...
	m_pClipSelect->clear();

	// Now find all the clips/regions that fall
	// in the given rectangular region...
	int y1, y2 = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		if (rect.bottom() < y1)
			break;
		if (y2 >= rect.top()) {
			int y = y1 + 1;
			int h = y2 - y1 - 2;
			for (qtractorClip *pClip = pTrack->clips().first();
					pClip; pClip = pClip->next()) {
				int x = pSession->pixelFromFrame(pClip->clipStart());
				int w = pSession->pixelFromFrame(pClip->clipLength());
				if (x > rect.right())
					break;
				// Test whether the whole clip rectangle
				// intersects the rubber-band range one...
				QRect rectClip(x, y, w, h);
				if (rect.intersects(rectClip)) {
					if (selectMode != SelectClip)
						rectClip = rectRange.intersect(rectClip);
					m_pClipSelect->selectClip(pClip, rectClip, true);
					if (selectMode != SelectClip)
						pClip->setClipSelect(iSelectStart, iSelectEnd);
					rectUpdate = rectUpdate.unite(rectClip);
				}
			}
		}
		pTrack = pTrack->next();
	}

	// Update the screen real estate...
	if (!rectUpdate.isEmpty())
		updateContents(rectUpdate);

	// That's all very nice but we'll also set edit-range positioning...
	if (selectEdit & EditHead)
		setEditHead(iSelectStart);
	if (selectEdit & EditTail)
		setEditTail(iSelectEnd);
}


// Clip selection mode accessors.
void qtractorTrackView::setSelectMode (
	qtractorTrackView::SelectMode selectMode )
{
	m_selectMode = selectMode;
}

qtractorTrackView::SelectMode qtractorTrackView::selectMode (void) const
{
	return m_selectMode;
}


// Select every clip of a given track.
void qtractorTrackView::selectTrack ( qtractorTrack *pTrackPtr, bool bReset )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	int iUpdate = 0;
	if (bReset && m_pClipSelect->singleTrack() != pTrackPtr) {
		m_pClipSelect->clear();
		iUpdate++;
	}

	QRect rectUpdate;
	int y1, y2 = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		if (pTrack == pTrackPtr) {
			int y = y1 + 1;
			int h = y2 - y1 - 2;
			for (qtractorClip *pClip = pTrack->clips().first();
					pClip; pClip = pClip->next()) {
				int x = pSession->pixelFromFrame(pClip->clipStart());
				int w = pSession->pixelFromFrame(pClip->clipLength());
				const QRect rectClip(x, y, w, h);
				m_pClipSelect->selectClip(pClip, rectClip,
					!pClip->isClipSelected());
				rectUpdate = rectUpdate.unite(rectClip);
				iUpdate++;
			}
			break;
		}
		pTrack = pTrack->next();
	}

	if (iUpdate > 0) {
		if (bReset || rectUpdate.isEmpty())
			updateContents();
		else
			updateContents(rectUpdate);
		m_pTracks->selectionChangeNotify();
	}
}


// Select range interval between edit head and tail.
void qtractorTrackView::selectEditRange (void)
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Get and select the whole rectangular area
	// between the edit head and tail points...
	QRect rect(0, 0, 0, qtractorScrollView::contentsHeight());
	rect.setLeft(pSession->pixelFromFrame(pSession->editHead()));
	rect.setRight(pSession->pixelFromFrame(pSession->editTail()));

	// HACK: Make sure the snap goes straight...
	unsigned short iSnapPerBeat4 = (pSession->snapPerBeat() << 2);
	if (iSnapPerBeat4 > 0)
		rect.translate(pSession->pixelsPerBeat() / iSnapPerBeat4, 0);

	// Make the selection, but don't change edit head nor tail...
	selectRect(rect,
		qtractorTrackView::SelectRange,
		qtractorTrackView::EditNone);

	// Make its due...
	m_pTracks->selectionChangeNotify();
}


// Select all contents (or not).
void qtractorTrackView::selectAll ( bool bSelect )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	if (bSelect) {
		// Select all clips on all tracks...
		int iUpdate = 0;
		int y1, y2 = 0;
		qtractorTrack *pTrack = pSession->tracks().first();
		while (pTrack) {
			y1  = y2;
			y2 += pTrack->zoomHeight();
			int y = y1 + 1;
			int h = y2 - y1 - 2;
			for (qtractorClip *pClip = pTrack->clips().first();
					pClip; pClip = pClip->next()) {
				int x = pSession->pixelFromFrame(pClip->clipStart());
				int w = pSession->pixelFromFrame(pClip->clipLength());
				const QRect rectClip(x, y, w, h);
				m_pClipSelect->selectClip(pClip, rectClip, bSelect);
				iUpdate++;
			}
			pTrack = pTrack->next();
		}
		// This is most probably an overall update...
		if (iUpdate > 0) {
			updateContents(m_pClipSelect->rect());
			m_pTracks->selectionChangeNotify();
		}
	}
	else
	// Clear all selections...
	if (m_pClipSelect->items().count() > 0) {
		QRect rectUpdate = m_pClipSelect->rect();
		m_pClipSelect->clear();
		if (!rectUpdate.isEmpty())
			updateContents(rectUpdate);
		m_pTracks->selectionChangeNotify();
	}
}


// Whether there's any clip currently editable.
qtractorClip *qtractorTrackView::currentClip (void) const
{
	qtractorClip *pClip = m_pClipDrag;

	if (pClip == NULL && isClipSelected())
		pClip = m_pClipSelect->items().first()->clip;

	return pClip;
}


// Clip selection accessor.
qtractorClipSelect *qtractorTrackView::clipSelect (void) const
{
	return m_pClipSelect;
}


// Update whole clip selection.
void qtractorTrackView::updateClipSelect (void)
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Reset all selected clips, but don't
	// clear their own selection state...
	m_pClipSelect->reset();

	// Now find all the clips/regions
	// that were currently selected...
	int y1, y2 = 0;
	qtractorTrack *pTrack = pSession->tracks().first();
	while (pTrack) {
		y1  = y2;
		y2 += pTrack->zoomHeight();
		int y = y1 + 1;
		int h = y2 - y1 - 2;
		for (qtractorClip *pClip = pTrack->clips().first();
				pClip; pClip = pClip->next()) {
			if (pClip->isClipSelected()) {
				int x = pSession->pixelFromFrame(pClip->clipSelectStart());
				int w = pSession->pixelFromFrame(pClip->clipSelectEnd()) - x;
				m_pClipSelect->addClip(pClip, QRect(x, y, w, h));
			}
		}
		pTrack = pTrack->next();
	}
}


// Draw/hide the whole current clip selection.
void qtractorTrackView::showClipSelect (void) const
{
	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		qtractorClipSelect::Item *pClipItem = iter.next();
		moveRubberBand(&(pClipItem->rubberBand), pClipItem->rectClip, 3);
	}
}

void qtractorTrackView::hideClipSelect (void) const
{
	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		qtractorRubberBand *pRubberBand = iter.next()->rubberBand;
		if (pRubberBand && pRubberBand->isVisible())
			pRubberBand->hide();
	}
}


// Update single track clip selection.
void qtractorTrackView::updateSingleTrack ( int y, int h ) const
{
	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		qtractorClipSelect::Item *pClipItem	= iter.next();
		pClipItem->rectClip.setY(y);
		pClipItem->rectClip.setHeight(h);
	}
}


// Draw/hide the whole drop rectagle list
void qtractorTrackView::updateDropRects ( int y, int h ) const
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	int x = 0;
	bool bDropSpan = (m_bDropSpan && m_dropType != qtractorTrack::Midi);
	QListIterator<DropItem *> iter(m_dropItems);
	while (iter.hasNext()) {
		DropItem *pDropItem = iter.next();
		pDropItem->rect.setY(y);
		pDropItem->rect.setHeight(h);
		if (bDropSpan) {
			if (x > 0)
				pDropItem->rect.moveLeft(x);
			else
				x = pDropItem->rect.x();
			x = pSession->pixelSnap(x + pDropItem->rect.width());
		} else {
			y += h + 4;
		}
	}
}

void qtractorTrackView::showDropRects (void) const
{
	QListIterator<DropItem *> iter(m_dropItems);
	while (iter.hasNext()) {
		DropItem *pDropItem = iter.next();
		moveRubberBand(&(pDropItem->rubberBand), pDropItem->rect, 3);
	}
}

void qtractorTrackView::hideDropRects (void) const
{
	QListIterator<DropItem *> iter(m_dropItems);
	while (iter.hasNext()) {
		qtractorRubberBand *pRubberBand = iter.next()->rubberBand;
		if (pRubberBand && pRubberBand->isVisible())
			pRubberBand->hide();
	}
}



// Show and move rubber-band item.
void qtractorTrackView::moveRubberBand ( qtractorRubberBand **ppRubberBand,
	const QRect& rectDrag, int thick ) const
{
	QRect rect(rectDrag.normalized());

	// Horizontal adjust...
	rect.translate(m_iDraggingX, 0);
	// Convert rectangle into view coordinates...
	rect.moveTopLeft(qtractorScrollView::contentsToViewport(rect.topLeft()));
	// Make sure the rectangle doesn't get too off view,
	// which it would make it sluggish :)
	if (rect.left() < 0)
		rect.setLeft(-8);
	if (rect.right() > qtractorScrollView::width())
		rect.setRight(qtractorScrollView::width() + 8);

	// Create the rubber-band if there's none...
	qtractorRubberBand *pRubberBand = *ppRubberBand;
	if (pRubberBand == NULL) {
		pRubberBand = new qtractorRubberBand(
			QRubberBand::Rectangle, qtractorScrollView::viewport(), thick);
	//	QPalette pal(pRubberBand->palette());
	//	pal.setColor(pRubberBand->foregroundRole(), Qt::blue);
	//	pRubberBand->setPalette(pal);
	//	pRubberBand->setBackgroundRole(QPalette::NoRole);
		// Do not ever forget to set it back...
		*ppRubberBand = pRubberBand;
	}
	
	// Just move it
	pRubberBand->setGeometry(rect);

	// Ah, and make it visible, of course...
	if (!pRubberBand->isVisible())
		pRubberBand->show();
}


// Check whether we're up to drag a clip fade-in/out or resize handle.
bool qtractorTrackView::dragFadeResizeStart ( const QPoint& pos )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	QRect rectClip;
	qtractorClip *pClip = clipAt(pos, false, &rectClip);
	if (pClip) {
		// Fade-in handle check...
		m_rectHandle.setRect(rectClip.left() + 1
			+ pSession->pixelFromFrame(pClip->fadeInLength()),
				rectClip.top() + 1, 8, 8);
		if (m_rectHandle.contains(pos)) {
			m_dragCursor = DragFadeIn;
			qtractorScrollView::setCursor(QCursor(Qt::PointingHandCursor));
			return true;
		}
		// Fade-out handle check...
		m_rectHandle.setRect(rectClip.right() - 8
			- pSession->pixelFromFrame(pClip->fadeOutLength()),
				rectClip.top() + 1, 8, 8);
		if (m_rectHandle.contains(pos)) {
			m_dragCursor = DragFadeOut;
			qtractorScrollView::setCursor(QCursor(Qt::PointingHandCursor));
			return true;
		}
		// Resize-right check...
		if (pos.x() >= rectClip.right() - 4) {
			m_dragCursor = DragResizeRight;
			qtractorScrollView::setCursor(QCursor(Qt::SizeHorCursor));
			return true;
		}
		// Resize-left check...
		if (pos.x() < rectClip.left() + 4) {
			m_dragCursor = DragResizeLeft;
			qtractorScrollView::setCursor(QCursor(Qt::SizeHorCursor));
			return true;
		}
	}

	// Reset cursor if any persist around.
	if (m_dragCursor != DragNone) {
		qtractorScrollView::unsetCursor();
		m_dragCursor  = DragNone;
	}

	return false;
}


// Clip fade-in/out handle drag-moving parts.
void qtractorTrackView::dragFadeMove ( const QPoint& pos )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Always change horizontally wise...
	int dx = (pos.x() - m_posDrag.x());
	if (m_rectHandle.left() + dx < m_rectDrag.left())
		dx = m_rectDrag.left() - m_rectHandle.left();
	else if (m_rectHandle.right() + dx > m_rectDrag.right())
		dx = m_rectDrag.right() - m_rectHandle.right();
	m_iDraggingX = dx;
	moveRubberBand(&m_pRubberBand, m_rectHandle);
	qtractorScrollView::ensureVisible(pos.x(), pos.y(), 24, 24);

	// Prepare to update the whole view area...
	qtractorScrollView::viewport()->update(
		QRect(contentsToViewport(m_rectDrag.topLeft()), m_rectDrag.size()));
}


// Clip fade-in/out handle settler.
void qtractorTrackView::dragFadeDrop ( const QPoint& pos )
{
	if (m_pClipDrag == NULL)
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	dragFadeMove(pos);

	// We'll build a command...
	qtractorClipCommand *pClipCommand
		= new qtractorClipCommand(tr("clip %1").arg(
			m_dragState == DragFadeIn ? tr("fade-in") : tr("fade-out")));

	if (m_dragState == DragFadeIn) {
		pClipCommand->fadeInClip(m_pClipDrag,
			pSession->frameFromPixel(
				m_rectHandle.left() + m_iDraggingX - m_rectDrag.left()),
				m_pClipDrag->fadeInType());
	} 
	else
	if (m_dragState == DragFadeOut) {
		pClipCommand->fadeOutClip(m_pClipDrag,
			pSession->frameFromPixel(
				m_rectDrag.right() - m_iDraggingX - m_rectHandle.right()),
				m_pClipDrag->fadeOutType());
	}

	// Reset state for proper redrawing...
	m_dragState = DragNone;

	// Put it in the form of an undoable command...
	pSession->execute(pClipCommand);
}


// Clip resize handle drag-moving parts.
void qtractorTrackView::dragResizeMove ( const QPoint& pos )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Always change horizontally wise...
	int  x = 0;
	int dx = (pos.x() - m_posDrag.x());
	QRect rect(m_rectDrag);
	if (m_dragState == DragResizeLeft) {
		if (rect.left() > -(dx))
			x = pSession->pixelSnap(rect.left() + dx);
		if (x < 0)
			x = 0;
		else
		if (x > rect.right() - 8)
			x = rect.right() - 8;
		rect.setLeft(x);
	}
	else
	if (m_dragState == DragResizeRight) {
		if (rect.right() > -(dx))
			x = pSession->pixelSnap(rect.right() + dx);
		if (x < rect.left() + 8)
			x = rect.left() + 8;
		rect.setRight(x);
	}

	moveRubberBand(&m_pRubberBand, rect, 3);
	qtractorScrollView::ensureVisible(pos.x(), pos.y(), 24, 24);
}


// Clip resize handle settler.
void qtractorTrackView::dragResizeDrop ( const QPoint& pos, bool bTimeStretch )
{
	if (m_pClipDrag == NULL)
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// We'll build a command...
	qtractorClipCommand *pClipCommand = new qtractorClipCommand(
		bTimeStretch ? tr("clip stretch") : tr("clip resize"));

	unsigned long iClipStart  = m_pClipDrag->clipStart();
	unsigned long iClipOffset = m_pClipDrag->clipOffset();
	unsigned long iClipLength = m_pClipDrag->clipLength();

	// Always change horizontally wise...
	int  x = 0;
	int dx = (pos.x() - m_posDrag.x());
	if (m_dragState == DragResizeLeft) {
		unsigned long iClipDelta;
		x = m_rectDrag.left() + dx;
		if (x < 0)
			x = 0;
		else
		if (x > m_rectDrag.right() - 8)
			x = m_rectDrag.right() - 8;
		iClipStart = pSession->frameSnap(pSession->frameFromPixel(x));
		if (m_pClipDrag->clipStart() > iClipStart) {
			iClipDelta = (m_pClipDrag->clipStart() - iClipStart);
			if (!bTimeStretch) {
				if (iClipOffset  > iClipDelta)
					iClipOffset -= iClipDelta;
				else
					iClipOffset = 0;
			}
			iClipLength += iClipDelta;
		} else {
			iClipDelta = (iClipStart - m_pClipDrag->clipStart());
			if (!bTimeStretch)
				iClipOffset += iClipDelta;
			iClipLength -= iClipDelta;
		}
	}
	else
	if (m_dragState == DragResizeRight) {
		x = m_rectDrag.right() + dx;
		if (x < m_rectDrag.left() + 8)
			x = m_rectDrag.left() + 8;
		iClipLength = pSession->frameSnap(pSession->frameFromPixel(x))
			- iClipStart;
	}

	// Time stretching...
	float fTimeStretch = 0.0f;
	if (bTimeStretch && m_pClipDrag->track()
		&& (m_pClipDrag->track())->trackType() == qtractorTrack::Audio) {
		qtractorAudioClip *pAudioClip
			= static_cast<qtractorAudioClip *> (m_pClipDrag);
		if (pAudioClip) {
			fTimeStretch = (float(iClipLength) * pAudioClip->timeStretch())
				/ float(m_pClipDrag->clipLength());
		}
	}

	// Declare the clip resize parcel...
	pClipCommand->resizeClip(m_pClipDrag,
		iClipStart, iClipOffset, iClipLength, fTimeStretch);

	// Put it in the form of an undoable command...
	pSession->execute(pClipCommand);
}


// Reset drag/select/move state.
void qtractorTrackView::resetDragState (void)
{
	// To remember what we were doing...
	DragState dragState = m_dragState;

	// Should fallback mouse cursor...
	if (m_dragCursor != DragNone)
		qtractorScrollView::unsetCursor();
	if (m_dragState == DragMove        ||
		m_dragState == DragResizeLeft  ||
		m_dragState == DragResizeRight ||
		m_dragState == DragDropPaste   ||
		m_dragState == DragPaste       ||
		m_dragState == DragStep) {
		m_pClipSelect->clear();
		updateContents();
	}

	// Force null state, now.
	m_dragState  = DragNone;
	m_dragCursor = DragNone;
	m_iDraggingX = 0;
//	m_pClipDrag  = NULL;

	m_posStep = QPoint(0, 0);

	// No pasting nomore.
	m_iPasteCount  = 0;
	m_iPastePeriod = 0;

	// If we were moving clips around,
	// just hide selection, of course.
	hideClipSelect();

	// Just hide the rubber-band...
	if (m_pRubberBand) {
		m_pRubberBand->hide();
		delete m_pRubberBand;
		m_pRubberBand = NULL;
	}

	// If we were dragging fade-slope lines, refresh...
	if (dragState == DragFadeIn || dragState == DragFadeOut) {
		qtractorScrollView::viewport()->update(
			QRect(contentsToViewport(m_rectDrag.topLeft()), m_rectDrag.size()));
	}

	// No dropping files, whatsoever.
	qDeleteAll(m_dropItems);
	m_dropItems.clear();
	m_dropType = qtractorTrack::None;
}


// Keyboard event handler.
void qtractorTrackView::keyPressEvent ( QKeyEvent *pKeyEvent )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorTrackView::keyPressEvent(%d)", pKeyEvent->key());
#endif
	int iKey = pKeyEvent->key();
	switch (iKey) {
	case Qt::Key_Insert: // Aha, joking :)
	case Qt::Key_Return:
		if (m_dragState == DragStep) {
			moveClipSelect(dragMoveTrack(m_posDrag + m_posStep));
		} else {
			const QPoint& pos = qtractorScrollView::viewportToContents(
				qtractorScrollView::viewport()->mapFromGlobal(QCursor::pos()));
			if (m_dragState == DragMove)
				moveClipSelect(dragMoveTrack(pos + m_posStep));
			else if (m_dragState == DragPaste)
				pasteClipSelect(dragMoveTrack(pos + m_posStep));
			else if (m_dragState == DragDropPaste)
				dropTrack(pos + m_posStep);
		}
		// Fall thru...
	case Qt::Key_Escape:
		m_dragState = DragStep; // HACK: Force selection clearance!
		resetDragState();
		break;
	case Qt::Key_Home:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(0, 0);
		} else {
			qtractorScrollView::setContentsPos(
				0, qtractorScrollView::contentsY());
		}
		break;
	case Qt::Key_End:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsWidth(),
				qtractorScrollView::contentsHeight());
		} else {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsWidth(),
				qtractorScrollView::contentsY());
		}
		break;
	case Qt::Key_Left:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX() - qtractorScrollView::width(),
				qtractorScrollView::contentsY());
		} else if (!keyStep(iKey)) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX() - 16,
				qtractorScrollView::contentsY());
		}
		break;
	case Qt::Key_Right:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX() + qtractorScrollView::width(),
				qtractorScrollView::contentsY());
		} else if (!keyStep(iKey)) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX() + 16,
				qtractorScrollView::contentsY());
		}
		break;
	case Qt::Key_Up:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() - qtractorScrollView::height());
		} else if (!keyStep(iKey)) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() - 16);
		}
		break;
	case Qt::Key_Down:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() + qtractorScrollView::height());
		} else if (!keyStep(iKey)) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() + 16);
		}
		break;
	case Qt::Key_PageUp:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(), 16);
		} else {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() - qtractorScrollView::height());
		}
		break;
	case Qt::Key_PageDown:
		if (pKeyEvent->modifiers() & Qt::ControlModifier) {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsHeight());
		} else {
			qtractorScrollView::setContentsPos(
				qtractorScrollView::contentsX(),
				qtractorScrollView::contentsY() + qtractorScrollView::height());
		}
		break;
	default:
		qtractorScrollView::keyPressEvent(pKeyEvent);
		break;
	}
}


// Keyboard step handler.
bool qtractorTrackView::keyStep ( int iKey )
{
	// Only applicable if something is selected...
	if (!isClipSelected() && m_dropItems.isEmpty())
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	// Set initial bound conditions...
	if (m_dragState == DragNone) {
		m_dragState  = m_dragCursor = DragStep;
		m_rectDrag   = m_pClipSelect->rect();
		m_posDrag    = m_rectDrag.topLeft();
		m_posStep    = QPoint(0, 0);
		m_iDraggingX = (pSession->pixelSnap(m_rectDrag.x()) - m_rectDrag.x());
		qtractorScrollView::setCursor(QCursor(Qt::SizeAllCursor));
	//	showClipSelect();
	}

	// Now to say the truth...
	if (m_dragState != DragMove &&
		m_dragState != DragStep &&
		m_dragState != DragPaste &&
		m_dragState != DragDropPaste)
		return false;

	// Determine vertical step...
	if (iKey == Qt::Key_Up || iKey == Qt::Key_Down)  {
		int iVerticalStep = qtractorTrack::HeightMin;
		qtractorTrack *pTrack = m_pTracks->currentTrack();
		if (pTrack)
			iVerticalStep += (pTrack->zoomHeight() >> 1);
		int y0 = m_posDrag.y();
		int y1 = y0 + m_posStep.y();
		if (iKey == Qt::Key_Up)
			y1 -= iVerticalStep;
		else
			y1 += iVerticalStep;
		m_posStep.setY((y1 < 0 ? 0 : y1) - y0);
	}
	else
	// Determine horizontal step...
	if (iKey == Qt::Key_Left || iKey == Qt::Key_Right)  {
		unsigned short iSnapPerBeat = pSession->snapPerBeat();
		if (iSnapPerBeat < 1)
			iSnapPerBeat = 1;
		int x0 = m_posDrag.x();
		int x1 = x0 + m_posStep.x();
		qtractorTimeScale::Cursor cursor(pSession->timeScale());
		qtractorTimeScale::Node *pNode = cursor.seekPixel(x1);
		int iHorizontalStep = pNode->pixelsPerBeat() / iSnapPerBeat;
		if (iKey == Qt::Key_Left)
			x1 -= iHorizontalStep;
		else
			x1 += iHorizontalStep;
		m_posStep.setX(pSession->pixelSnap(x1 < 0 ? 0 : x1) - x0);
	}

	// Early sanity check...
	const QRect& rect
		= (m_dragState == DragDropPaste ? m_rectDrag : m_pClipSelect->rect());
	QPoint pos = m_posDrag;
	if (m_dragState != DragStep) {
		pos = qtractorScrollView::viewportToContents(
			qtractorScrollView::viewport()->mapFromGlobal(QCursor::pos()));
	}

	int x2 = - pos.x();
	int y2 = - pos.y();
	if (m_dragState != DragStep) {
		x2 += (m_posDrag.x() - rect.x());
		y2 += (m_posDrag.y() - rect.y());
	}

	if (m_posStep.x() < x2) {
		m_posStep.setX (x2);
	} else {
		x2 += qtractorScrollView::contentsWidth() - (rect.width() >> 1);
		if (m_posStep.x() > x2)
			m_posStep.setX (x2);
	}

	if (m_posStep.y() < y2) {
		m_posStep.setY (y2);
	} else {
		y2 += qtractorScrollView::contentsHeight() - (rect.height() >> 1);
		if (m_posStep.y() > y2)
			m_posStep.setY (y2);
	}

	// Do our deeds (flag we're key-steppin')...
	if (m_dragState == DragDropPaste) {
		dragDropTrack(pos + m_posStep, true);
		showDropRects();
	} else {
		dragMoveTrack(pos + m_posStep, true);
	}

	return true;
}


// Make given frame position visible in view.
void qtractorTrackView::ensureVisibleFrame ( unsigned long iFrame )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		int x0 = qtractorScrollView::contentsX();
		int y  = qtractorScrollView::contentsY();
		int x  = pSession->pixelFromFrame(iFrame);
		int w  = m_pixmap.width();
		int w3 = w - (w >> 3);
		if (x < x0)
			x -= w3;
		else if (x > x0 + w3)
			x += w3;
		qtractorScrollView::ensureVisible(x, y, 0, 0);
	//	qtractorScrollView::setFocus();
	}
}


// Session cursor accessor.
qtractorSessionCursor *qtractorTrackView::sessionCursor (void) const
{
	return m_pSessionCursor;
}


// Vertical line positioning.
void qtractorTrackView::drawPositionX ( int& iPositionX, int x, bool bSyncView )
{
	// Update track-view position...
	int x0 = qtractorScrollView::contentsX();
	int x1 = iPositionX - x0;
	int w  = qtractorScrollView::width();
	int h  = qtractorScrollView::height();
	int wm = (w >> 3);

	// Time-line header extents...
	int h2 = m_pTracks->trackTime()->height();
	int d2 = (h2 >> 1);

	// Restore old position...
	if (iPositionX != x && x1 >= 0 && x1 < w + d2) {
		// Override old view line...
		qtractorScrollView::viewport()->update(QRect(x1, 0, 1, h));
		((m_pTracks->trackTime())->viewport())->update(
			QRect(x1 - d2, d2, h2, d2));
	}

	// New position is in...
	iPositionX = x;

	// Force position to be in view?
	if (bSyncView && (x < x0 || x > x0 + w - wm) && m_dragState == DragNone) {
		 // Maybe we'll need some head-room...
		if (x0 < qtractorScrollView::contentsWidth() - w) {
			qtractorScrollView::setContentsPos(
				x - wm, qtractorScrollView::contentsY());
		}
		else updateContentsWidth(x0 + w);
	}
	else {
		// Draw the line, by updating the new region...
		x1 = x - x0;
		if (x1 >= 0 && x1 < w + d2) {
			qtractorScrollView::viewport()->update(QRect(x1, 0, 1, h));
			((m_pTracks->trackTime())->viewport())->update(
				QRect(x1 - d2, d2, h2, d2));
		}
	}
}


// Playhead positioning.
void qtractorTrackView::setPlayHead ( unsigned long iPlayHead, bool bSyncView )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		m_iPlayHead = iPlayHead;
		int iPlayHeadX = pSession->pixelFromFrame(iPlayHead);
		drawPositionX(m_iPlayHeadX, iPlayHeadX, bSyncView);
	}
}

unsigned long qtractorTrackView::playHead (void) const
{
	return m_iPlayHead;
}

int qtractorTrackView::playHeadX (void) const
{
	return m_iPlayHeadX;
}


// Edit-head positioning
void qtractorTrackView::setEditHead ( unsigned long iEditHead )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		if (iEditHead > pSession->editTail())
			setEditTail(iEditHead);
		pSession->setEditHead(iEditHead);
		int iEditHeadX = pSession->pixelFromFrame(iEditHead);
		drawPositionX(m_iEditHeadX, iEditHeadX);
	}
}

int qtractorTrackView::editHeadX (void) const
{
	return m_iEditHeadX;
}


// Edit-tail positioning
void qtractorTrackView::setEditTail ( unsigned long iEditTail )
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession) {
		if (iEditTail < pSession->editHead())
			setEditHead(iEditTail);
		pSession->setEditTail(iEditTail);
		int iEditTailX = pSession->pixelFromFrame(iEditTail);
		drawPositionX(m_iEditTailX, iEditTailX);
	}
}

int qtractorTrackView::editTailX (void) const
{
	return m_iEditTailX;
}


// Clear current selection (no notify).
void qtractorTrackView::clearClipSelect (void)
{
	m_pClipSelect->clear();
}


// Whether there's any clip currently selected.
bool qtractorTrackView::isClipSelected (void) const
{
	return (m_pClipSelect->items().count() > 0);
}


// Whether there's a single track selection.
qtractorTrack *qtractorTrackView::singleTrackSelected (void)
{
	return m_pClipSelect->singleTrack();
}


// Whether there's any clip on clipboard. (static)
bool qtractorTrackView::isClipboard (void)
{
	return (g_clipboard.items.count() > 0);
}


// Clipboard stuffer method.
void qtractorTrackView::ClipBoard::addItem ( qtractorClip *pClip,
	const QRect& clipRect, unsigned long iClipStart,
	unsigned long iClipOffset, unsigned long iClipLength )
{
	ClipItem *pClipItem
		= new ClipItem(pClip, clipRect, iClipStart, iClipOffset, iClipLength);
	if (iClipOffset == pClip->clipOffset())
		pClipItem->fadeInLength = pClip->fadeInLength();
	if (iClipOffset + iClipLength == pClip->clipOffset() + pClip->clipLength())
		pClipItem->fadeOutLength = pClip->fadeOutLength();
	items.append(pClipItem);
}


// Clipboard reset method.
void qtractorTrackView::ClipBoard::clear (void)
{
	qDeleteAll(items);
	items.clear();

	singleTrack = NULL;
}


// Clip selection sanity check method.
bool qtractorTrackView::queryClipSelect (void)
{
	// Check if anything is really selected...
	if (m_pClipSelect->items().count() < 1)
		return false;

	// Just ask whether any target clips have pending editors...
	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		if (!(iter.next()->clip)->queryEditor())
			return false;
	}

	// If it reaches here, we can do what we will to...
	return true;
}


// Clip selection executive method.
void qtractorTrackView::executeClipSelect ( qtractorTrackView::Command cmd )
{
	// Check if anything is really selected and sane...
	if (!queryClipSelect())
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Reset clipboard...
	bool bClipboard = (cmd == Cut || cmd == Copy);
	if (bClipboard) {
		g_clipboard.clear();
		g_clipboard.singleTrack = m_pClipSelect->singleTrack();
		g_clipboard.rect = m_pClipSelect->rect();
		QApplication::clipboard()->clear();
	}

	// We'll build a composite command...
	qtractorClipCommand *pClipCommand = NULL;
	if (cmd == Cut || cmd == Delete) {
		pClipCommand = new qtractorClipCommand(
			tr("%1 clip").arg(cmd == Cut ? tr("cut") : tr("delete")));
	}

	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		qtractorClipSelect::Item *pClipItem	= iter.next();
		qtractorClip  *pClip  = pClipItem->clip;
		qtractorTrack *pTrack = pClip->track();
		// Clip parameters.
		unsigned long iClipStart    = pClip->clipStart();
		unsigned long iClipOffset   = pClip->clipOffset();
		unsigned long iClipLength   = pClip->clipLength();
		unsigned long iClipEnd      = iClipStart + iClipLength;
		// Clip selection points.
		unsigned long iSelectStart  = pClip->clipSelectStart();
		unsigned long iSelectEnd    = pClip->clipSelectEnd();
		unsigned long iSelectOffset = iSelectStart - iClipStart;
		unsigned long iSelectLength = iSelectEnd - iSelectStart;
		// Determine and dispatch selected clip regions...
		if (iSelectStart > iClipStart) {
			if (iSelectEnd < iClipEnd) {
				// -- Middle region...
				if (bClipboard) {
					g_clipboard.addItem(pClip,
						pClipItem->rectClip,
						iSelectStart,
						iClipOffset + iSelectOffset,
						iSelectLength);
				}
				if (pClipCommand) {
					// Left-clip...
					pClipCommand->resizeClip(pClip,
						iClipStart,
						iClipOffset,
						iSelectOffset);
					// Right-clip...
					qtractorClip *pClipEx = cloneClip(pClip);
					if (pClipEx) {
						pClipEx->setClipStart(iSelectEnd);
						pClipEx->setClipOffset(iClipOffset
							+ iSelectOffset + iSelectLength);
						pClipEx->setClipLength(iClipEnd - iSelectEnd);
						pClipEx->setFadeOutLength(pClip->fadeOutLength());
						pClipCommand->addClip(pClipEx, pTrack);
					}
				}
				// Done, middle region.
			} else {
				// -- Right region...
				if (bClipboard) {
					g_clipboard.addItem(pClip,
						pClipItem->rectClip,
						iSelectStart,
						iClipOffset + iSelectOffset,
						iSelectLength);
				}
				if (pClipCommand) {
					pClipCommand->resizeClip(pClip,
						iClipStart,
						iClipOffset,
						iSelectOffset);
				}
				// Done, right region.
			}
		}
		else
		if (iSelectEnd < iClipEnd) {
			// -- Left region...
			if (bClipboard) {
				g_clipboard.addItem(pClip,
					pClipItem->rectClip,
					iClipStart,
					iClipOffset,
					iSelectLength);
			}
			if (pClipCommand) {
				pClipCommand->resizeClip(pClip,
					iSelectEnd,
					iClipOffset + iSelectLength,
					iClipLength - iSelectLength);
			}
			// Done, left region.
		} else {
			// -- Whole clip...
			if (bClipboard) {
				g_clipboard.addItem(pClip,
					pClipItem->rectClip,
					iClipStart,
					iClipOffset,
					iClipLength);
			}
			if (pClipCommand) {
				pClipCommand->removeClip(pClip);
			}
			// Done, whole clip.
		}
	}

	// Reset selection on cut or delete;
	// put it in the form of an undoable command...
	if (pClipCommand) {
		m_pClipSelect->clear();
		pSession->execute(pClipCommand);
	}
}


// Retrieve current paste period.
// (as from current clipboard width)
unsigned long qtractorTrackView::pastePeriod (void) const
{
	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return 0;

	if (g_clipboard.items.count() < 1)
		return 0;

	return pSession->frameSnap(
		pSession->frameFromPixel(g_clipboard.rect.width()));
}


// Paste from clipboard (start).
void qtractorTrackView::pasteClipboard (
	unsigned short iPasteCount, unsigned long iPastePeriod )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorTrackView::pasteClipboard(%u, %lu)",
		iPasteCount, iPastePeriod);
#endif

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Make sure the mouse pointer is properly located...
	const QPoint& pos = qtractorScrollView::viewportToContents(
		qtractorScrollView::viewport()->mapFromGlobal(QCursor::pos()));

	// Check if anythings really on clipboard...
	if (g_clipboard.items.count() < 1) {
		// System clipboard?
		QClipboard *pClipboard = QApplication::clipboard();
		if (pClipboard && (pClipboard->mimeData())->hasUrls()) {
			dragDropTrack(pos, false, pClipboard->mimeData());
			// Make a proper out of this (new) state?
			if (!m_dropItems.isEmpty()) {
				m_dragState = m_dragCursor = DragDropPaste;
				// It doesn't matter which one, both pasteable views are due...
				qtractorScrollView::setFocus();
				qtractorScrollView::setCursor(
					QCursor(QPixmap(":/icons/editPaste.png"), 12, 12));
				// Update the pasted stuff
				showDropRects();
			}
		}
		// Woot!
		return;
	}

	// Reset any current selection, whatsoever...
	m_pClipSelect->clear();
	resetDragState();

	// Set paste parameters...
	m_iPasteCount  = iPasteCount;
	m_iPastePeriod = iPastePeriod;

	int x0 = 0;
	int dx = (m_iPastePeriod > 0
		? pSession->pixelFromFrame(m_iPastePeriod)
		: g_clipboard.rect.width());

	// Copy clipboard items to floating selection;
	// adjust clip widths/lengths just in case time
	// scale (horizontal zoom) has been changed... 
	//
	QListIterator<ClipItem *> iter(g_clipboard.items);
	for (unsigned short i = 0; i < m_iPasteCount; ++i) {
		iter.toFront();
		while (iter.hasNext()) {
			ClipItem *pClipItem = iter.next();
			QRect rect(pClipItem->rect);
			rect.setX(rect.x() + x0);
			rect.setWidth(pSession->pixelFromFrame(pClipItem->clipLength));
			m_pClipSelect->addClip(pClipItem->clip, rect);
		}
		x0 += dx;
	}

	// We'll start a brand new floating state...
	m_dragState = m_dragCursor = DragPaste;
	m_rectDrag  = m_pClipSelect->rect();
	m_posDrag   = m_rectDrag.topLeft();
	m_posStep   = QPoint(0, 0);

	// It doesn't matter which one, both pasteable views are due...
	qtractorScrollView::setFocus();
	qtractorScrollView::setCursor(
		QCursor(QPixmap(":/icons/editPaste.png"), 12, 12));

	// Let's-a go...
	dragMoveTrack(pos + m_posStep);
}



// Intra-drag-n-drop clip move method.
void qtractorTrackView::moveClipSelect ( qtractorTrack *pTrack )
{
	// Check if anything is really selected and sane...
	if (!queryClipSelect())
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// We'll need this...
	qtractorClipCommand *pClipCommand
		= new qtractorClipCommand(tr("move clip"));

	// We can only move clips between tracks of the same type...
	int  iTrackClip = 0;
	long iClipDelta = 0;
	bool bAddTrack = (pTrack == NULL);
	qtractorTrack *pSingleTrack = m_pClipSelect->singleTrack();
	if (pSingleTrack) {
		if (bAddTrack) {
			int iTrack = pSession->tracks().count() + 1;
			const QColor color = qtractorTrack::trackColor(iTrack);
			pTrack = new qtractorTrack(pSession, pSingleTrack->trackType());
		//	pTrack->setTrackName(tr("Track %1").arg(iTrack));
			pTrack->setBackground(color);
			pTrack->setForeground(color.darker());
			if (pSingleTrack->trackType() == qtractorTrack::Midi) {
				pTrack->setMidiChannel(pSingleTrack->midiChannel());
				pTrack->setMidiBank(pSingleTrack->midiBank());
				pTrack->setMidiProgram(pSingleTrack->midiProgram());
			}
			pClipCommand->addTrack(pTrack);
		}
		else
		if (pSingleTrack->trackType() != pTrack->trackType())
			return;
	}

	// We'll build a composite command...

	QListIterator<qtractorClipSelect::Item *> iter(m_pClipSelect->items());
	while (iter.hasNext()) {
		qtractorClipSelect::Item *pClipItem	= iter.next();
		qtractorClip *pClip = pClipItem->clip;
		if (pSingleTrack == NULL)
			pTrack = pClip->track();
		// Clip parameters.
		unsigned long iClipStart    = pClip->clipStart();
		unsigned long iClipOffset   = pClip->clipOffset();
		unsigned long iClipLength   = pClip->clipLength();
		unsigned long iClipEnd      = iClipStart + iClipLength;
		// Clip selection points.
		unsigned long iSelectStart  = pClip->clipSelectStart();
		unsigned long iSelectEnd    = pClip->clipSelectEnd();
		unsigned long iSelectOffset = iSelectStart - iClipStart;
		unsigned long iSelectLength = iSelectEnd - iSelectStart;
		// Determine and keep clip regions...
		if (iSelectStart > iClipStart) {
			// -- Left clip...
			qtractorClip *pClipLeft = cloneClip(pClip);
			pClipLeft->setClipStart(iClipStart);
			pClipLeft->setClipOffset(iClipOffset);
			pClipLeft->setClipLength(iSelectOffset);
			pClipLeft->setFadeInLength(pClip->fadeInLength());
			pClipCommand->addClip(pClipLeft, pClipLeft->track());
			// Done, left clip.
		}
		if (iSelectEnd < iClipEnd) {
			// -- Right clip...
			qtractorClip *pClipRight = cloneClip(pClip);
			pClipRight->setClipStart(iSelectEnd);
			pClipRight->setClipOffset(iClipOffset
				+ iSelectOffset + iSelectLength);
			pClipRight->setClipLength(iClipEnd - iSelectEnd);
			pClipRight->setFadeOutLength(pClip->fadeOutLength());
			pClipCommand->addClip(pClipRight, pClipRight->track());
			// Done, right clip.
		}
		// Convert to precise frame positioning,
		// but only the first clip gets snapped...
		iClipStart = iSelectStart;
		if (iTrackClip == 0) {
			int x = (pClipItem->rectClip.x() + m_iDraggingX);
			unsigned long iFrameStart = pSession->frameSnap(
				pSession->frameFromPixel(x > 0 ? x : 0));
			iClipDelta = long(iFrameStart) - long(iClipStart);
			iClipStart = iFrameStart;
		} else if (long(iClipStart) + iClipDelta > 0) {
			iClipStart += iClipDelta;
		} else {
			iClipStart = 0;
		}
		// -- Moved clip...
		pClipCommand->moveClip(pClip, pTrack,
			iClipStart,
			iClipOffset + iSelectOffset,
			iSelectLength);
		// If track's new it will need a name...
		if (bAddTrack && iTrackClip == 0)
			pTrack->setTrackName(pClip->clipName());
		iTrackClip++;
	}

	// May reset selection, yep.
	m_pClipSelect->clear();

	// Put it in the form of an undoable command...
	pSession->execute(pClipCommand);
}


// Paste from clipboard (execute).
void qtractorTrackView::pasteClipSelect ( qtractorTrack *pTrack )
{
	// Check if anything is really selected and sane...
	if (!queryClipSelect())
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Check if there's anything really on clipboard...
	if (g_clipboard.items.count() < 1)
		return;

	// We'll need this...
	qtractorClipCommand *pClipCommand
		= new qtractorClipCommand(tr("paste clip"));

	// We can only move clips between tracks of the same type...
	bool bAddTrack = (pTrack == NULL);
	qtractorTrack *pSingleTrack = m_pClipSelect->singleTrack();
	if (pSingleTrack) {
		if (bAddTrack) {
			int iTrack = pSession->tracks().count() + 1;
			const QColor color = qtractorTrack::trackColor(iTrack);
			pTrack = new qtractorTrack(pSession, pSingleTrack->trackType());
		//	pTrack->setTrackName(tr("Track %1").arg(iTrack));
			pTrack->setBackground(color);
			pTrack->setForeground(color.darker());
			pClipCommand->addTrack(pTrack);
		}
		else
		if (pSingleTrack->trackType() != pTrack->trackType())
			return;
	}

	int dx = (m_iPastePeriod > 0
		? pSession->pixelFromFrame(m_iPastePeriod)
		: g_clipboard.rect.width());

	// We'll build a composite command...
	QListIterator<ClipItem *> iter(g_clipboard.items);
	for (unsigned short i = 0; i < m_iPasteCount; ++i) {
		// Paste iteration...
		int  iTrackClip = 0;
		long iClipDelta = 0;
		iter.toFront();
		while (iter.hasNext()) {
			ClipItem *pClipItem = iter.next();
			qtractorClip *pClip = pClipItem->clip;
			if (pSingleTrack == NULL)
				pTrack = pClip->track();
			// Convert to precise frame positioning,
			// but only the first clip gets snapped...
			unsigned long iClipStart = pClipItem->clipStart;
			if (iTrackClip == 0) {
				int x = (pClipItem->rect.x() + m_iDraggingX);
				unsigned long iFrameStart // = pSession->frameSnap(...);
					= pSession->frameFromPixel(x > 0 ? x : 0);
				iClipDelta = long(iFrameStart) - long(iClipStart);
				iClipStart = iFrameStart;
			} else if (long(iClipStart) + iClipDelta > 0) {
				iClipStart += iClipDelta;
			} else {
				iClipStart = 0;
			}
			// Now, its imperative to make a proper copy of those clips...
			qtractorClip *pNewClip = NULL;
			switch (pTrack->trackType()) {
			case qtractorTrack::Audio: {
				qtractorAudioClip *pAudioClip
					= static_cast<qtractorAudioClip *> (pClip);
				if (pAudioClip)
					pNewClip = new qtractorAudioClip(*pAudioClip);
				break;
			}
			case qtractorTrack::Midi: {
				qtractorMidiClip *pMidiClip
					= static_cast<qtractorMidiClip *> (pClip);
				if (pMidiClip)
					pNewClip = new qtractorMidiClip(*pMidiClip);
				break;
			}
			default:
				break;
			}
			// Add the new pasted clip...
			if (pNewClip) {
				pNewClip->setClipStart(iClipStart);
				pNewClip->setClipOffset(pClipItem->clipOffset);
				pNewClip->setClipLength(pClipItem->clipLength);
				pNewClip->setFadeInLength(pClipItem->fadeInLength);
				pNewClip->setFadeOutLength(pClipItem->fadeOutLength);
				pClipCommand->addClip(pNewClip, pTrack);
				// If track's new it will need a name...
				if (bAddTrack && iTrackClip == 0)
					pTrack->setTrackName(pClip->clipName());
				iTrackClip++;
			}
		}
		// Set to repeat...
		m_iDraggingX += dx;
	}

	// May reset selection, yep.
	m_pClipSelect->clear();

	// Put it in the form of an undoable command...
	pSession->execute(pClipCommand);
}


// Clip cloner helper.
qtractorClip *qtractorTrackView::cloneClip ( qtractorClip *pClip )
{
	if (pClip == NULL)
		return NULL;

	qtractorTrack *pTrack = pClip->track();
	if (pTrack == NULL)
		return NULL;

	qtractorClip *pNewClip = NULL;
	switch (pTrack->trackType()) {
		case qtractorTrack::Audio: {
			qtractorAudioClip *pAudioClip
				= static_cast<qtractorAudioClip *> (pClip);
			if (pAudioClip)
				pNewClip = new qtractorAudioClip(*pAudioClip);
			break;
		}
		case qtractorTrack::Midi: {
			qtractorMidiClip *pMidiClip
				= static_cast<qtractorMidiClip *> (pClip);
			if (pMidiClip)
				pNewClip = new qtractorMidiClip(*pMidiClip);
			break;
		}
		case qtractorTrack::None:
		default:
			break;
	}

	return pNewClip;
}


// Multi-item drop mode (whether to span clips horixontally).
void qtractorTrackView::setDropSpan ( bool bDropSpan )
{
	m_bDropSpan = bDropSpan;
}

bool qtractorTrackView::isDropSpan (void) const
{
	return m_bDropSpan;
}


// end of qtractorTrackView.cpp
