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

#pragma once

#include <components/SceneLayout.hpp>

#include <QWidget>

/* Draws a layout as a canvas-proportioned rectangle with one numbered box per
 * slot. In Preview mode it behaves as a selectable thumbnail for the preset
 * grid; in Editor mode slots can be moved and resized with the mouse. */
class LayoutView : public QWidget {
	Q_OBJECT

public:
	enum class Mode {
		Preview,
		Editor,
	};

private:
	SceneLayout layout;
	Mode mode = Mode::Preview;

	bool selected = false;
	int selectedSlot = -1;
	int hoveredSlot = -1;

	/* Canvas aspect ratio, used so the preview matches what the layout will
	 * actually look like on the output. */
	double aspect = 16.0 / 9.0;

	enum class DragMode {
		None,
		Move,
		ResizeLeft,
		ResizeRight,
		ResizeTop,
		ResizeBottom,
		ResizeTopLeft,
		ResizeTopRight,
		ResizeBottomLeft,
		ResizeBottomRight,
	};

	DragMode dragMode = DragMode::None;
	QPointF dragOrigin;
	LayoutSlot dragStartSlot;

	/* Set while a drag is snapped, so the guide can be drawn. Coordinates
	 * are normalized like the slots themselves. */
	bool snappedX = false;
	bool snappedY = false;
	float snapLineX = 0.0f;
	float snapLineY = 0.0f;

	/* Nudges the slot onto nearby canvas or sibling edges. Honours the
	 * snapping preferences the user already set for the preview. */
	void ApplySnapping(LayoutSlot &slot, DragMode drag, bool disabled);

	/* Rectangle inside the widget the canvas is drawn into. */
	QRectF CanvasRect() const;

	QRectF SlotRect(const LayoutSlot &slot) const;

	int SlotAt(const QPointF &pos) const;
	DragMode DragModeAt(const QPointF &pos, int slotIndex) const;

	void UpdateCursor(const QPointF &pos);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEvent(QEvent *event) override;

public:
	explicit LayoutView(QWidget *parent = nullptr, Mode mode = Mode::Preview);

	void SetLayout(const SceneLayout &newLayout);
	const SceneLayout &Layout() const { return layout; }

	void SetAspect(double newAspect);

	void SetSelected(bool value);
	bool Selected() const { return selected; }

	int SelectedSlot() const { return selectedSlot; }
	void SetSelectedSlot(int index);

	enum class AlignAction {
		Left,
		HCenter,
		Right,
		Top,
		VCenter,
		Bottom,
		FillCanvas,
	};

	/* Moves the selected slot against a canvas edge, its centre, or expands
	 * it to the whole canvas. No-op when nothing is selected. */
	void AlignSelectedSlot(AlignAction action);

	QSize sizeHint() const override;

signals:
	void Clicked();
	void LayoutChanged();
	void SlotSelected(int index);
};
