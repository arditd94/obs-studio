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

#include "SceneDock.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <utility/display-helpers.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <QMouseEvent>
#include <QVBoxLayout>

#include <algorithm>

SceneDock::SceneDock(OBSSource scene, QWidget *parent) : OBSDock(parent)
{
	weakScene = OBSGetWeakRef(scene);

	setObjectName(QString::fromUtf8(obs_source_get_uuid(scene)) + "_SceneDock");
	setWindowTitle(QString::fromUtf8(obs_source_get_name(scene)));
	setAllowedAreas(Qt::AllDockWidgetAreas);
	setMinimumSize(120, 100);

	preview = new OBSQTDisplay(this);
	preview->setMinimumSize(64, 36);
	preview->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

	auto addDrawCallback = [this]() {
		obs_display_add_draw_callback(preview->GetDisplay(), SceneDock::DrawPreview, this);
	};

	connect(preview.data(), &OBSQTDisplay::DisplayCreated, this, addDrawCallback);

	preview->setCursor(Qt::PointingHandCursor);
	preview->setToolTip(QTStr("Basic.SceneDock.ClickToSwitch"));
	preview->installEventFilter(this);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->setContentsMargins(2, 2, 2, 2);
	layout->addWidget(preview, 1);

	QWidget *content = new QWidget(this);
	content->setLayout(layout);
	setWidget(content);

	signal_handler_t *handler = obs_source_get_signal_handler(scene);
	sigs.emplace_back(handler, "rename", SceneDock::SceneRenamed, this);
	sigs.emplace_back(handler, "remove", SceneDock::SceneRemoved, this);
	sigs.emplace_back(handler, "destroy", SceneDock::SceneRemoved, this);
}

SceneDock::~SceneDock()
{
	if (preview && preview->GetDisplay()) {
		obs_display_remove_draw_callback(preview->GetDisplay(), SceneDock::DrawPreview, this);
	}
}

OBSSource SceneDock::GetScene() const
{
	return OBSGetStrongRef(weakScene);
}

void SceneDock::DrawPreview(void *data, uint32_t cx, uint32_t cy)
{
	SceneDock *window = static_cast<SceneDock *>(data);

	OBSSource scene = window->GetScene();
	if (!scene) {
		return;
	}

	const uint32_t sceneCX = std::max(obs_source_get_width(scene), 1u);
	const uint32_t sceneCY = std::max(obs_source_get_height(scene), 1u);

	int x;
	int y;
	float scale;

	GetScaleAndCenterPos(sceneCX, sceneCY, cx, cy, x, y, scale);

	const int newCX = int(scale * float(sceneCX));
	const int newCY = int(scale * float(sceneCY));

	gs_viewport_push();
	gs_projection_push();
	const bool previous = gs_set_linear_srgb(true);

	gs_ortho(0.0f, float(sceneCX), 0.0f, float(sceneCY), -100.0f, 100.0f);
	gs_set_viewport(x, y, newCX, newCY);
	obs_source_video_render(scene);

	gs_set_linear_srgb(previous);
	gs_projection_pop();
	gs_viewport_pop();
}

void SceneDock::SceneRenamed(void *param, calldata_t *data)
{
	SceneDock *window = static_cast<SceneDock *>(param);
	const char *name = calldata_string(data, "new_name");

	if (name) {
		QMetaObject::invokeMethod(window, "HandleRename", Q_ARG(QString, QString::fromUtf8(name)));
	}
}

void SceneDock::SceneRemoved(void *param, calldata_t *)
{
	SceneDock *window = static_cast<SceneDock *>(param);

	/* Deleting from the Qt thread keeps the draw callback teardown on the
	 * thread that created the display. */
	QMetaObject::invokeMethod(window, "deleteLater");
}

void SceneDock::HandleRename(const QString &name)
{
	setWindowTitle(name);
}

bool SceneDock::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == preview && event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *mouse = static_cast<QMouseEvent *>(event);

		if (mouse->button() == Qt::LeftButton) {
			OBSSource scene = GetScene();

			if (scene) {
				/* Same path as the scene list, so studio mode
				 * lands on preview instead of cutting to air. */
				OBSBasic::Get()->SetCurrentScene(scene, false);
			}

			return true;
		}
	}

	return OBSDock::eventFilter(watched, event);
}
