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

#include "OBSDock.hpp"

#include <obs.hpp>

#include <QPointer>

#include <vector>

class MediaControls;
class OBSQTDisplay;
class QPushButton;

/* A dock showing a single source: a live preview, media transport when the
 * source supports it, and shortcuts to its properties and interaction window.
 *
 * Unlike a floating utility window, this is a real QDockWidget, so it can be
 * docked alongside the built-in panels and its position is saved with the rest
 * of the window state. */
class SourceDock : public OBSDock {
	Q_OBJECT

	OBSWeakSource weakSource;

	QPointer<OBSQTDisplay> preview;
	QPointer<MediaControls> mediaControls;
	QPointer<QPushButton> propertiesButton;
	QPointer<QPushButton> interactButton;

	std::vector<OBSSignal> sigs;

	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	static void SourceRenamed(void *param, calldata_t *data);
	static void SourceRemoved(void *param, calldata_t *data);

	void UpdateButtons(obs_source_t *source);

	/* First scene in scene-list order that contains this source, searched
	 * recursively so a source inside a group still resolves. */
	OBSSource FindContainingScene() const;

	void SwitchToContainingScene();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void OpenProperties();
	void OpenInteract();
	void HandleRename(const QString &name);

public:
	SourceDock(OBSSource source, QWidget *parent = nullptr);
	~SourceDock();

	/* Empty once the source is gone, which is how the owner knows the dock
	 * should be dropped. */
	OBSSource GetSource() const;
};
