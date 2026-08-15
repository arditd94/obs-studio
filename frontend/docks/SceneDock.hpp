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

class OBSQTDisplay;

/* A dock showing the live preview of one scene. Clicking the preview takes the
 * scene to air, which turns a row of these into a switcher.
 *
 * Being a real QDockWidget, it can be docked alongside the built-in panels and
 * its placement is saved with the rest of the window state. */
class SceneDock : public OBSDock {
	Q_OBJECT

	OBSWeakSource weakScene;

	QPointer<OBSQTDisplay> preview;

	std::vector<OBSSignal> sigs;

	static void DrawPreview(void *data, uint32_t cx, uint32_t cy);
	static void SceneRenamed(void *param, calldata_t *data);
	static void SceneRemoved(void *param, calldata_t *data);

private slots:
	void HandleRename(const QString &name);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

public:
	SceneDock(OBSSource scene, QWidget *parent = nullptr);
	~SceneDock();

	/* Empty once the scene is gone, which is how the owner knows the dock
	 * should be dropped. */
	OBSSource GetScene() const;
};
