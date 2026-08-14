/******************************************************************************
    Copyright (C) 2026 by the OBS Studio contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "LayoutView.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

/* Thickness of the grab band along a slot edge, in device independent pixels. */
constexpr qreal kHandleSize = 7.0;

/* Margin kept around the canvas so the selection outline is never clipped. */
constexpr qreal kCanvasMargin = 3.0;

LayoutView::LayoutView(QWidget *parent, Mode mode_) : QWidget(parent), mode(mode_)
{
	setMouseTracking(mode == Mode::Editor);

	if (mode == Mode::Editor)
		setFocusPolicy(Qt::StrongFocus);
	else
		setCursor(Qt::PointingHandCursor);
}

void LayoutView::SetLayout(const SceneLayout &newLayout)
{
	layout = newLayout;

	if (selectedSlot >= (int)layout.slotList.size())
		selectedSlot = -1;

	update();
}

void LayoutView::SetAspect(double newAspect)
{
	if (newAspect > 0.0) {
		aspect = newAspect;
		update();
	}
}

void LayoutView::SetSelected(bool value)
{
	if (selected != value) {
		selected = value;
		update();
	}
}

void LayoutView::SetSelectedSlot(int index)
{
	const int clamped = (index >= 0 && index < (int)layout.slotList.size()) ? index : -1;

	if (selectedSlot != clamped) {
		selectedSlot = clamped;
		update();
		emit SlotSelected(selectedSlot);
	}
}

QSize LayoutView::sizeHint() const
{
	return mode == Mode::Editor ? QSize(480, 270) : QSize(160, 90);
}

QRectF LayoutView::CanvasRect() const
{
	QRectF available = QRectF(rect()).adjusted(kCanvasMargin, kCanvasMargin, -kCanvasMargin, -kCanvasMargin);

	if (available.width() <= 0.0 || available.height() <= 0.0)
		return QRectF();

	qreal w = available.width();
	qreal h = w / aspect;

	if (h > available.height()) {
		h = available.height();
		w = h * aspect;
	}

	const qreal x = available.left() + (available.width() - w) / 2.0;
	const qreal y = available.top() + (available.height() - h) / 2.0;

	return QRectF(x, y, w, h);
}

QRectF LayoutView::SlotRect(const LayoutSlot &slot) const
{
	const QRectF canvas = CanvasRect();

	return QRectF(canvas.left() + slot.x * canvas.width(), canvas.top() + slot.y * canvas.height(),
		      slot.cx * canvas.width(), slot.cy * canvas.height());
}

int LayoutView::SlotAt(const QPointF &pos) const
{
	/* Walk backwards so the slot drawn on top is the one that gets picked. */
	for (int i = (int)layout.slotList.size() - 1; i >= 0; i--) {
		QRectF r = SlotRect(layout.slotList[i]);
		r.adjust(-kHandleSize / 2.0, -kHandleSize / 2.0, kHandleSize / 2.0, kHandleSize / 2.0);

		if (r.contains(pos))
			return i;
	}

	return -1;
}

LayoutView::DragMode LayoutView::DragModeAt(const QPointF &pos, int slotIndex) const
{
	if (slotIndex < 0 || slotIndex >= (int)layout.slotList.size())
		return DragMode::None;

	const QRectF r = SlotRect(layout.slotList[slotIndex]);

	const bool left = std::fabs(pos.x() - r.left()) <= kHandleSize;
	const bool right = std::fabs(pos.x() - r.right()) <= kHandleSize;
	const bool top = std::fabs(pos.y() - r.top()) <= kHandleSize;
	const bool bottom = std::fabs(pos.y() - r.bottom()) <= kHandleSize;

	if (top && left)
		return DragMode::ResizeTopLeft;
	if (top && right)
		return DragMode::ResizeTopRight;
	if (bottom && left)
		return DragMode::ResizeBottomLeft;
	if (bottom && right)
		return DragMode::ResizeBottomRight;
	if (left)
		return DragMode::ResizeLeft;
	if (right)
		return DragMode::ResizeRight;
	if (top)
		return DragMode::ResizeTop;
	if (bottom)
		return DragMode::ResizeBottom;

	return r.contains(pos) ? DragMode::Move : DragMode::None;
}

void LayoutView::UpdateCursor(const QPointF &pos)
{
	if (mode != Mode::Editor)
		return;

	switch (DragModeAt(pos, SlotAt(pos))) {
	case DragMode::Move:
		setCursor(Qt::SizeAllCursor);
		break;
	case DragMode::ResizeLeft:
	case DragMode::ResizeRight:
		setCursor(Qt::SizeHorCursor);
		break;
	case DragMode::ResizeTop:
	case DragMode::ResizeBottom:
		setCursor(Qt::SizeVerCursor);
		break;
	case DragMode::ResizeTopLeft:
	case DragMode::ResizeBottomRight:
		setCursor(Qt::SizeFDiagCursor);
		break;
	case DragMode::ResizeTopRight:
	case DragMode::ResizeBottomLeft:
		setCursor(Qt::SizeBDiagCursor);
		break;
	default:
		setCursor(Qt::ArrowCursor);
		break;
	}
}

void LayoutView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF canvas = CanvasRect();
	if (canvas.isEmpty())
		return;

	const QPalette &pal = palette();

	/* The canvas stands in for the program output, so it is painted as a
	 * neutral dark surface regardless of the active theme. */
	painter.fillRect(canvas, QColor(24, 24, 24));

	const QColor accent = pal.color(QPalette::Highlight);

	for (size_t i = 0; i < layout.slotList.size(); i++) {
		const QRectF r = SlotRect(layout.slotList[i]);
		const bool isSelected = mode == Mode::Editor && (int)i == selectedSlot;
		const bool isHovered = mode == Mode::Editor && (int)i == hoveredSlot;

		QColor fill = accent;
		/* Fade successive slots so overlapping boxes stay readable. */
		fill.setAlpha(isSelected ? 220 : (isHovered ? 190 : 150 - std::min<int>(60, (int)i * 10)));

		painter.setPen(QPen(isSelected ? pal.color(QPalette::BrightText) : accent.darker(140),
				    isSelected ? 2.0 : 1.0));
		painter.setBrush(fill);
		painter.drawRect(r);

		if (r.width() < 12.0 || r.height() < 12.0)
			continue;

		QFont font = painter.font();
		font.setBold(true);
		font.setPointSizeF(std::clamp(r.height() / 4.0, 7.0, 22.0));
		painter.setFont(font);
		painter.setPen(pal.color(QPalette::HighlightedText));
		painter.drawText(r, Qt::AlignCenter, QString::number(i + 1));
	}

	/* Outline: selection state in the preset grid, plain border elsewhere. */
	QPen outline(selected ? accent : pal.color(QPalette::Mid), selected ? 2.5 : 1.0);
	painter.setPen(outline);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(canvas);
}

void LayoutView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}

	if (mode == Mode::Preview) {
		emit Clicked();
		return;
	}

	const QPointF pos = event->position();
	const int index = SlotAt(pos);

	SetSelectedSlot(index);

	if (index >= 0) {
		dragMode = DragModeAt(pos, index);
		dragOrigin = pos;
		dragStartSlot = layout.slotList[index];
	} else {
		dragMode = DragMode::None;
	}
}

void LayoutView::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF pos = event->position();

	if (mode != Mode::Editor) {
		QWidget::mouseMoveEvent(event);
		return;
	}

	if (dragMode == DragMode::None) {
		const int hovered = SlotAt(pos);

		if (hovered != hoveredSlot) {
			hoveredSlot = hovered;
			update();
		}

		UpdateCursor(pos);
		return;
	}

	if (selectedSlot < 0 || selectedSlot >= (int)layout.slotList.size())
		return;

	const QRectF canvas = CanvasRect();
	if (canvas.isEmpty())
		return;

	const float dx = (float)((pos.x() - dragOrigin.x()) / canvas.width());
	const float dy = (float)((pos.y() - dragOrigin.y()) / canvas.height());

	LayoutSlot slot = dragStartSlot;

	/* Resizing adjusts the moving edge while pinning the opposite one, so a
	 * drag past the far edge collapses to the minimum size instead of
	 * flipping the rectangle inside out. */
	auto resizeLeft = [&]() {
		const float right = dragStartSlot.x + dragStartSlot.cx;
		slot.x = std::clamp(dragStartSlot.x + dx, 0.0f, right - kMinLayoutSlotSize);
		slot.cx = right - slot.x;
	};
	auto resizeRight = [&]() {
		slot.cx = std::clamp(dragStartSlot.cx + dx, kMinLayoutSlotSize, 1.0f - dragStartSlot.x);
	};
	auto resizeTop = [&]() {
		const float bottom = dragStartSlot.y + dragStartSlot.cy;
		slot.y = std::clamp(dragStartSlot.y + dy, 0.0f, bottom - kMinLayoutSlotSize);
		slot.cy = bottom - slot.y;
	};
	auto resizeBottom = [&]() {
		slot.cy = std::clamp(dragStartSlot.cy + dy, kMinLayoutSlotSize, 1.0f - dragStartSlot.y);
	};

	switch (dragMode) {
	case DragMode::Move:
		slot.x = dragStartSlot.x + dx;
		slot.y = dragStartSlot.y + dy;
		break;
	case DragMode::ResizeLeft:
		resizeLeft();
		break;
	case DragMode::ResizeRight:
		resizeRight();
		break;
	case DragMode::ResizeTop:
		resizeTop();
		break;
	case DragMode::ResizeBottom:
		resizeBottom();
		break;
	case DragMode::ResizeTopLeft:
		resizeLeft();
		resizeTop();
		break;
	case DragMode::ResizeTopRight:
		resizeRight();
		resizeTop();
		break;
	case DragMode::ResizeBottomLeft:
		resizeLeft();
		resizeBottom();
		break;
	case DragMode::ResizeBottomRight:
		resizeRight();
		resizeBottom();
		break;
	case DragMode::None:
		return;
	}

	slot.Normalize();

	if (!(slot == layout.slotList[selectedSlot])) {
		layout.slotList[selectedSlot] = slot;
		update();
		emit LayoutChanged();
	}
}

void LayoutView::mouseReleaseEvent(QMouseEvent *event)
{
	if (mode == Mode::Editor && dragMode != DragMode::None) {
		dragMode = DragMode::None;
		UpdateCursor(event->position());
	}

	QWidget::mouseReleaseEvent(event);
}

void LayoutView::leaveEvent(QEvent *event)
{
	if (hoveredSlot != -1) {
		hoveredSlot = -1;
		update();
	}

	QWidget::leaveEvent(event);
}
