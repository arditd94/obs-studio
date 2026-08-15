/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

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

#include "OBSBasic.hpp"
#include "OBSProjector.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <QMenu>
#include <QScreen>
#include <QTimer>

obs_data_array_t *OBSBasic::SaveProjectors()
{
	obs_data_array_t *savedProjectors = obs_data_array_create();

	auto saveProjector = [savedProjectors](OBSProjector *projector) {
		if (!projector) {
			return;
		}

		OBSDataAutoRelease data = obs_data_create();
		ProjectorType type = projector->GetProjectorType();

		switch (type) {
		case ProjectorType::Scene:
		case ProjectorType::Source: {
			OBSSource source = projector->GetSource();
			const char *name = obs_source_get_name(source);
			obs_data_set_string(data, "name", name);
			break;
		}
		default:
			break;
		}

		obs_data_set_int(data, "monitor", projector->GetMonitor());
		obs_data_set_int(data, "type", static_cast<int>(type));
		obs_data_set_string(data, "geometry", projector->saveGeometry().toBase64().constData());

		if (projector->IsAlwaysOnTopOverridden()) {
			obs_data_set_bool(data, "alwaysOnTop", projector->IsAlwaysOnTop());
		}

		obs_data_set_bool(data, "alwaysOnTopOverridden", projector->IsAlwaysOnTopOverridden());

		obs_data_array_push_back(savedProjectors, data);
	};

	for (size_t i = 0; i < projectors.size(); i++) {
		saveProjector(static_cast<OBSProjector *>(projectors[i]));
	}

	return savedProjectors;
}

void OBSBasic::LoadSavedProjectors(obs_data_array_t *array)
{
	size_t num = obs_data_array_count(array);

	for (size_t i = 0; i < num; i++) {
		OBSDataAutoRelease data = obs_data_array_item(array, i);
		SavedProjectorInfo info = {};

		info.monitor = obs_data_get_int(data, "monitor");
		info.type = static_cast<ProjectorType>(obs_data_get_int(data, "type"));
		info.geometry = std::string(obs_data_get_string(data, "geometry"));
		info.name = std::string(obs_data_get_string(data, "name"));
		info.alwaysOnTop = obs_data_get_bool(data, "alwaysOnTop");
		info.alwaysOnTopOverridden = obs_data_get_bool(data, "alwaysOnTopOverridden");

		OpenSavedProjector(&info);
	}
}

void OBSBasic::updateMultiviewProjectorMenu()
{
	ui->multiviewProjectorMenu->clear();
	AddProjectorMenuMonitors(ui->multiviewProjectorMenu, this, &OBSBasic::OpenMultiviewProjector);
	ui->multiviewProjectorMenu->addSeparator();
	ui->multiviewProjectorMenu->addAction(QTStr("Projector.Window"), this, &OBSBasic::openMultiviewWindow);
}

void OBSBasic::ClearProjectors()
{
	for (size_t i = 0; i < projectors.size(); i++) {
		if (projectors[i]) {
			delete projectors[i];
		}
	}

	projectors.clear();
}

QList<QString> OBSBasic::GetProjectorMenuMonitorsFormatted()
{
	QList<QString> projectorsFormatted;
	QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		QScreen *screen = screens[i];
		QRect screenGeometry = screen->geometry();
		qreal screenPixelRatio = screen->devicePixelRatio();
		QString name = "";
#if defined(__APPLE__) || defined(_WIN32)
		name = screen->name();
#else
		name = screen->model().simplified();

		if (name.length() > 1 && name.endsWith("-")) {
			name.chop(1);
		}
#endif
		name = name.simplified();

		if (name.length() == 0) {
			name = QString("%1 %2").arg(QTStr("Display")).arg(QString::number(i + 1));
		}

		int screenPixelWidth = std::round((screenGeometry.width() * screenPixelRatio) * 0.5f) * 2;
		int screenPixelHeight = std::round((screenGeometry.height() * screenPixelRatio) * 0.5f) * 2;

		QString str = QString("%1: %2x%3 @ %4,%5")
				      .arg(name, QString::number(screenPixelWidth), QString::number(screenPixelHeight),
					   QString::number(screenGeometry.x()), QString::number(screenGeometry.y()));
		projectorsFormatted.push_back(str);
	}
	return projectorsFormatted;
}

void OBSBasic::DeleteProjector(OBSProjector *projector)
{
	for (size_t i = 0; i < projectors.size(); i++) {
		if (projectors[i] == projector) {
			projectors[i]->deleteLater();
			projectors.erase(projectors.begin() + i);
			break;
		}
	}
}

OBSProjector *OBSBasic::OpenProjector(obs_source_t *source, int monitor, ProjectorType type)
{
	/* seriously?  10 monitors? */
	if (monitor > 9 || monitor > QGuiApplication::screens().size() - 1) {
		return nullptr;
	}

	bool closeProjectors = config_get_bool(App()->GetUserConfig(), "BasicWindow", "CloseExistingProjectors");

	if (closeProjectors && monitor > -1) {
		for (size_t i = projectors.size(); i > 0; i--) {
			size_t idx = i - 1;
			if (projectors[idx]->GetMonitor() == monitor) {
				DeleteProjector(projectors[idx]);
			}
		}
	}

	OBSProjector *projector = new OBSProjector(nullptr, source, monitor, type);

	projectors.emplace_back(projector);

	return projector;
}

void OBSBasic::OpenPreviewProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OpenProjector(nullptr, monitor, ProjectorType::Preview);
}

void OBSBasic::OpenSourceProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OBSSceneItem item = GetCurrentSceneItem();
	if (!item) {
		return;
	}

	OpenProjector(obs_sceneitem_get_source(item), monitor, ProjectorType::Source);
}

void OBSBasic::OpenMultiviewProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OpenProjector(nullptr, monitor, ProjectorType::Multiview);
}

void OBSBasic::OpenSceneProjector()
{
	int monitor = sender()->property("monitor").toInt();
	OBSScene scene = GetCurrentScene();
	if (!scene) {
		return;
	}

	OpenProjector(obs_scene_get_source(scene), monitor, ProjectorType::Scene);
}

void OBSBasic::OpenPreviewWindow()
{
	OpenProjector(nullptr, -1, ProjectorType::Preview);
}

void OBSBasic::OpenSourceWindow()
{
	OBSSceneItem item = GetCurrentSceneItem();
	if (!item) {
		return;
	}

	OBSSource source = obs_sceneitem_get_source(item);

	OpenProjector(obs_sceneitem_get_source(item), -1, ProjectorType::Source);
}

void OBSBasic::OpenSceneWindow()
{
	OBSScene scene = GetCurrentScene();
	if (!scene) {
		return;
	}

	OBSSource source = obs_scene_get_source(scene);

	OpenProjector(obs_scene_get_source(scene), -1, ProjectorType::Scene);
}

void OBSBasic::OpenSavedProjector(SavedProjectorInfo *info)
{
	if (info) {
		OBSProjector *projector = nullptr;
		switch (info->type) {
		case ProjectorType::Source:
		case ProjectorType::Scene: {
			OBSSourceAutoRelease source = obs_get_source_by_name(info->name.c_str());
			if (!source) {
				return;
			}

			projector = OpenProjector(source, info->monitor, info->type);
			break;
		}
		default: {
			projector = OpenProjector(nullptr, info->monitor, info->type);
			break;
		}
		}

		if (projector && !info->geometry.empty() && info->monitor < 0) {
			QByteArray byteArray = QByteArray::fromBase64(QByteArray(info->geometry.c_str()));
			projector->restoreGeometry(byteArray);

			if (!WindowPositionValid(projector->normalGeometry())) {
				QRect rect = QGuiApplication::primaryScreen()->geometry();
				projector->setGeometry(
					QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), rect));
			}

			if (info->alwaysOnTopOverridden) {
				projector->SetIsAlwaysOnTop(info->alwaysOnTop, true);
			}
		}
	}
}

void OBSBasic::openMultiviewWindow()
{
	OpenProjector(nullptr, -1, ProjectorType::Multiview);
}

/* ------------------------------------------------------------------------- */
/* Project button */

void OBSBasic::on_projectButton_toggled(bool checked)
{
	const int monitor = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectMonitor");
	const uint32_t fadeMs = (uint32_t)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration");

	if (checked) {
		if (projectOutput) {
			return;
		}

		/* Preview without a source renders the main texture, so the
		 * screen follows the program output through transitions rather
		 * than being pinned to one scene. */
		OBSProjector *projector = OpenProjector(nullptr, monitor, ProjectorType::Preview);

		if (!projector) {
			QSignalBlocker block(ui->projectButton);
			ui->projectButton->setChecked(false);
			OBSMessageBox::warning(this, QTStr("Basic.Project"), QTStr("Basic.Project.NoMonitor"));
			return;
		}

		projector->StartFade(true, fadeMs);
		projectOutput = projector;
		return;
	}

	if (!projectOutput) {
		return;
	}

	OBSProjector *projector = projectOutput;
	projectOutput = nullptr;

	if (fadeMs == 0) {
		DeleteProjector(projector);
		return;
	}

	/* Hold the window open until the fade to black has finished, otherwise
	 * closing it would be the very cut the fade exists to avoid. */
	projector->StartFade(false, fadeMs);

	QTimer::singleShot((int)fadeMs, this, [this, projector]() {
		for (OBSProjector *existing : projectors) {
			if (existing == projector) {
				DeleteProjector(projector);
				break;
			}
		}
	});
}

void OBSBasic::on_projectSettingsButton_clicked()
{
	QMenu menu(this);

	const int currentMonitor = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectMonitor");
	const int currentFade = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration");

	QMenu *monitorMenu = menu.addMenu(QTStr("Basic.Project.Monitor"));
	QList<QScreen *> screens = QGuiApplication::screens();

	for (int i = 0; i < screens.size(); i++) {
		QScreen *screen = screens[i];
		const QRect geometry = screen->geometry();

		QString name = QString("%1: %2x%3 @ %4,%5")
				       .arg(QString::number(i + 1), QString::number(geometry.width()),
					    QString::number(geometry.height()), QString::number(geometry.x()),
					    QString::number(geometry.y()));

		QAction *action = monitorMenu->addAction(name);
		action->setCheckable(true);
		action->setChecked(i == currentMonitor);

		connect(action, &QAction::triggered, this, [i]() {
			config_set_int(App()->GetUserConfig(), "BasicWindow", "ProjectMonitor", i);
		});
	}

	QMenu *fadeMenu = menu.addMenu(QTStr("Basic.Project.Fade"));

	for (int ms : {0, 250, 500, 1000, 2000}) {
		QAction *action = fadeMenu->addAction(ms == 0 ? QTStr("Basic.Project.Fade.None")
							      : QTStr("Basic.Project.Fade.Ms").arg(QString::number(ms)));
		action->setCheckable(true);
		action->setChecked(ms == currentFade);

		connect(action, &QAction::triggered, this, [ms]() {
			config_set_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration", ms);
		});
	}

	menu.exec(QCursor::pos());
}
