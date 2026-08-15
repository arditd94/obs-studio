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
#include <dialogs/OBSBasicProjections.hpp>
#include <json11.hpp>
#include <qt-wrappers.hpp>

#include <QMenu>
#include <QScreen>
#include <QTimer>

using namespace json11;

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
/* Projections */

static uint32_t ProjectionFadeMs()
{
	return (uint32_t)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration");
}

bool OBSBasic::IsProjectionActive(int index) const
{
	return index >= 0 && index < projectOutputs.size() && !projectOutputs[index].isNull();
}

void OBSBasic::SetProjectionActive(int index, bool active)
{
	if (index < 0 || index >= projectionEntries.size()) {
		return;
	}

	while (projectOutputs.size() < projectionEntries.size()) {
		projectOutputs.append(QPointer<OBSProjector>());
	}

	if (active == IsProjectionActive(index)) {
		return;
	}

	const uint32_t fadeMs = ProjectionFadeMs();

	if (!active) {
		OBSProjector *projector = projectOutputs[index];
		projectOutputs[index] = nullptr;

		if (projector) {
			if (fadeMs == 0) {
				DeleteProjector(projector);
			} else {
				/* Hold the window open until the fade to black has
				 * finished, otherwise closing it would be the very
				 * cut the fade exists to avoid. */
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
		}

		UpdateProjectButtonState();
		emit projectionsChanged();
		return;
	}

	const ProjectionEntry &entry = projectionEntries[index];

	/* A screen can only show one thing, so an entry whose monitor is already
	 * busy is left off rather than stealing the screen. */
	for (int i = 0; i < projectionEntries.size(); i++) {
		if (i != index && IsProjectionActive(i) && projectionEntries[i].monitor == entry.monitor) {
			return;
		}
	}

	OBSProjector *projector = nullptr;

	if (entry.sceneUuid.isEmpty()) {
		/* Preview without a source renders the main texture, so the screen
		 * follows the program through transitions. */
		projector = OpenProjector(nullptr, entry.monitor, ProjectorType::Preview);
	} else {
		OBSSourceAutoRelease scene = obs_get_source_by_uuid(QT_TO_UTF8(entry.sceneUuid));

		if (!scene) {
			return;
		}

		projector = OpenProjector(scene.Get(), entry.monitor, ProjectorType::Scene);
	}

	if (!projector) {
		return;
	}

	projector->StartFade(true, fadeMs);

	/* A projector can also be dismissed with Escape or its close box, which
	 * must leave the panel and the button in step with reality. */
	connect(projector, &QObject::destroyed, this, &OBSBasic::ProjectionClosed);

	projectOutputs[index] = projector;

	UpdateProjectButtonState();
	emit projectionsChanged();
}

void OBSBasic::StartProjections()
{
	for (int i = 0; i < projectionEntries.size(); i++) {
		SetProjectionActive(i, true);
	}
}

void OBSBasic::StopProjections()
{
	for (int i = 0; i < projectionEntries.size(); i++) {
		SetProjectionActive(i, false);
	}
}

void OBSBasic::AddProjectionEntry(const ProjectionEntry &entry)
{
	projectionEntries.append(entry);
	projectOutputs.append(QPointer<OBSProjector>());

	SaveProjections();
	emit projectionsChanged();
}

void OBSBasic::RemoveProjectionEntry(int index)
{
	if (index < 0 || index >= projectionEntries.size()) {
		return;
	}

	SetProjectionActive(index, false);

	projectionEntries.removeAt(index);

	if (index < projectOutputs.size()) {
		projectOutputs.removeAt(index);
	}

	SaveProjections();
	emit projectionsChanged();
}

void OBSBasic::SetProjectionEntry(int index, const ProjectionEntry &entry)
{
	if (index < 0 || index >= projectionEntries.size()) {
		return;
	}

	/* Editing a running line restarts it, so the change lands on the screen
	 * straight away instead of at the next master toggle. */
	const bool wasActive = IsProjectionActive(index);

	if (wasActive) {
		SetProjectionActive(index, false);
	}

	projectionEntries[index] = entry;
	SaveProjections();

	if (wasActive) {
		SetProjectionActive(index, true);
	}

	emit projectionsChanged();
}

void OBSBasic::UpdateProjectButtonState()
{
	bool any = false;

	for (const QPointer<OBSProjector> &pointer : projectOutputs) {
		if (!pointer.isNull()) {
			any = true;
			break;
		}
	}

	QSignalBlocker block(ui->projectButton);
	ui->projectButton->setChecked(any);
	SetProjectButtonActive(any);
}

void OBSBasic::ProjectionClosed(QObject *projector)
{
	/* The guarded pointer is not necessarily cleared yet when destroyed()
	 * fires, so the entry is matched on the raw pointer instead. */
	for (int i = 0; i < projectOutputs.size(); i++) {
		if (projectOutputs[i].data() == projector) {
			projectOutputs[i] = nullptr;
		}
	}

	UpdateProjectButtonState();
	emit projectionsChanged();
}

void OBSBasic::on_projectButton_toggled(bool checked)
{
	if (checked) {
		if (projectionEntries.isEmpty()) {
			QSignalBlocker block(ui->projectButton);
			ui->projectButton->setChecked(false);
			OBSMessageBox::warning(this, QTStr("Basic.Project"), QTStr("Basic.Project.NoEntries"));
			return;
		}

		StartProjections();

		bool any = false;

		for (const QPointer<OBSProjector> &pointer : projectOutputs) {
			if (!pointer.isNull()) {
				any = true;
				break;
			}
		}

		if (!any) {
			QSignalBlocker block(ui->projectButton);
			ui->projectButton->setChecked(false);
			OBSMessageBox::warning(this, QTStr("Basic.Project"), QTStr("Basic.Project.NoMonitor"));
		}

		return;
	}

	StopProjections();
}

void OBSBasic::SetProjectButtonActive(bool active)
{
	/* Green while a screen is being fed, so the state is readable across the
	 * room rather than only from the pressed look of the button. */
	ui->projectButton->setStyleSheet(
		active ? "QPushButton { background-color: rgb(38, 138, 60); color: rgb(255, 255, 255); }" : "");
}

void OBSBasic::on_projectSettingsButton_clicked()
{
	/* Modeless so it can stay open beside the mixer during a show. */
	if (projectionsDialog) {
		projectionsDialog->show();
		projectionsDialog->raise();
		projectionsDialog->activateWindow();
		return;
	}

	projectionsDialog = new OBSBasicProjections(this);
	projectionsDialog->setAttribute(Qt::WA_DeleteOnClose, true);
	projectionsDialog->show();
}

void OBSBasic::LoadProjections()
{
	projectionEntries.clear();
	projectOutputs.clear();

	const char *jsonStr = config_get_string(App()->GetUserConfig(), "BasicWindow", "Projections");

	if (!jsonStr || !*jsonStr) {
		return;
	}

	std::string err;
	Json json = Json::parse(jsonStr, err);

	if (!err.empty()) {
		return;
	}

	for (const Json &item : json.array_items()) {
		ProjectionEntry entry;
		entry.sceneUuid = QString::fromStdString(item["scene"].string_value());
		entry.monitor = item["monitor"].int_value();

		projectionEntries.append(entry);
		projectOutputs.append(QPointer<OBSProjector>());
	}
}

void OBSBasic::SaveProjections()
{
	Json::array array;

	for (const ProjectionEntry &entry : projectionEntries) {
		array.push_back(Json::object{
			{"scene", QT_TO_UTF8(entry.sceneUuid)},
			{"monitor", entry.monitor},
		});
	}

	config_set_string(App()->GetUserConfig(), "BasicWindow", "Projections", Json(array).dump().c_str());
}
