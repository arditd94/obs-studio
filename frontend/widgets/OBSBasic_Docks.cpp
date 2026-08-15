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

#include <OBSApp.hpp>
#include <docks/SourceDock.hpp>
#include <json11.hpp>
#include <qt-wrappers.hpp>

#include <QMenu>

using namespace json11;

void setupDockAction(QDockWidget *dock)
{
	QAction *action = dock->toggleViewAction();

	auto neverDisable = [action]() {
		QSignalBlocker block(action);
		action->setEnabled(true);
	};

	auto newToggleView = [dock](bool check) {
		QSignalBlocker block(dock);
		dock->setVisible(check);
	};

	// Replace the slot connected by default
	QObject::disconnect(action, &QAction::triggered, nullptr, 0);
	QObject::connect(action, &QAction::triggered, dock, newToggleView);

	// Make the action unable to be disabled
	QObject::connect(action, &QAction::enabledChanged, action, neverDisable);
}

void OBSBasic::on_resetDocks_triggered(bool force)
{
#ifdef BROWSER_AVAILABLE
	if ((extraDocks.size() || extraCustomDocks.size() || extraBrowserDocks.size()) && !force)
#else
	if ((extraDocks.size() || extraCustomDocks.size()) && !force)
#endif
	{
		QMessageBox::StandardButton button =
			OBSMessageBox::question(this, QTStr("ResetUIWarning.Title"), QTStr("ResetUIWarning.Text"));

		if (button == QMessageBox::No) {
			return;
		}
	}

#define RESET_DOCKLIST(dockList)                                                                               \
	for (int i = dockList.size() - 1; i >= 0; i--) {                                                       \
		dockList[i]->setVisible(true);                                                                 \
		dockList[i]->setFloating(true);                                                                \
		dockList[i]->move(frameGeometry().topLeft() + rect().center() - dockList[i]->rect().center()); \
		dockList[i]->setVisible(false);                                                                \
	}

	RESET_DOCKLIST(extraDocks)
	RESET_DOCKLIST(extraCustomDocks)
#ifdef BROWSER_AVAILABLE
	RESET_DOCKLIST(extraBrowserDocks)
#endif
#undef RESET_DOCKLIST

	restoreState(startingDockLayout);
	ui->sideDocks->setChecked(true);

	int cx = width();
	int bottomDocksHeight = height();

	bottomDocksHeight = bottomDocksHeight * 225 / 1000;

	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	ui->mixerDock->setVisible(true);
	ui->transitionsDock->setVisible(true);
	controlsDock->setVisible(true);
	statsDock->setVisible(false);
	statsDock->setFloating(true);

	QList<QDockWidget *> bottomDocks{ui->mixerDock, ui->transitionsDock, controlsDock};

	resizeDocks(bottomDocks, {bottomDocksHeight, bottomDocksHeight, bottomDocksHeight}, Qt::Vertical);
	resizeDocks(bottomDocks, {cx * 45 / 100, cx * 14 / 100, cx * 16 / 100}, Qt::Horizontal);

	int sideDockWidth = std::min(width() * 30 / 100, 280);
	resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_lockDocks_toggled(bool lock)
{
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	QDockWidget::DockWidgetFeatures mainFeatures = features;
	mainFeatures &= ~QDockWidget::QDockWidget::DockWidgetClosable;

	ui->scenesDock->setFeatures(mainFeatures);
	ui->sourcesDock->setFeatures(mainFeatures);
	ui->mixerDock->setFeatures(mainFeatures);
	ui->transitionsDock->setFeatures(mainFeatures);
	controlsDock->setFeatures(mainFeatures);
	statsDock->setFeatures(features);

	for (int i = extraDocks.size() - 1; i >= 0; i--) {
		extraDocks[i]->setFeatures(features);
	}

	for (int i = extraCustomDocks.size() - 1; i >= 0; i--) {
		extraCustomDocks[i]->setFeatures(features);
	}

#ifdef BROWSER_AVAILABLE
	for (int i = extraBrowserDocks.size() - 1; i >= 0; i--) {
		extraBrowserDocks[i]->setFeatures(features);
	}
#endif
}

void OBSBasic::on_sideDocks_toggled(bool side)
{
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", side);

	setDockCornersVertical(side);
}

void OBSBasic::AddDockWidget(QDockWidget *dock, Qt::DockWidgetArea area, bool extraBrowser)
{
	if (dock->objectName().isEmpty()) {
		return;
	}

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	addDockWidget(area, dock);

#ifdef BROWSER_AVAILABLE
	if (extraBrowser && extraBrowserMenuDocksSeparator.isNull()) {
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();
	}

	if (!extraBrowser && !extraBrowserMenuDocksSeparator.isNull()) {
		ui->menuDocks->insertAction(extraBrowserMenuDocksSeparator, dock->toggleViewAction());
	} else {
		ui->menuDocks->addAction(dock->toggleViewAction());
	}

	if (extraBrowser) {
		return;
	}
#else
	UNUSED_PARAMETER(extraBrowser);

	ui->menuDocks->addAction(dock->toggleViewAction());
#endif

	extraDockNames.push_back(dock->objectName());
	extraDocks.push_back(std::shared_ptr<QDockWidget>(dock));
}

void OBSBasic::RemoveDockWidget(const QString &name)
{
	if (extraDockNames.contains(name)) {
		int idx = extraDockNames.indexOf(name);
		extraDockNames.removeAt(idx);
		extraDocks[idx].reset();
		extraDocks.removeAt(idx);
	} else if (extraCustomDockNames.contains(name)) {
		int idx = extraCustomDockNames.indexOf(name);
		extraCustomDockNames.removeAt(idx);
		removeDockWidget(extraCustomDocks[idx]);
		extraCustomDocks.removeAt(idx);
	}
}

bool OBSBasic::IsDockObjectNameUsed(const QString &name)
{
	QStringList list;
	list << "scenesDock"
	     << "sourcesDock"
	     << "mixerDock"
	     << "transitionsDock"
	     << "controlsDock"
	     << "statsDock";
	list << extraDockNames;
	list << extraCustomDockNames;

	return list.contains(name);
}

void OBSBasic::AddCustomDockWidget(QDockWidget *dock)
{
	// Prevent the object name from being changed
	connect(dock, &QObject::objectNameChanged, this, &OBSBasic::RepairCustomExtraDockName);

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	dock->setFeatures(features);
	addDockWidget(Qt::RightDockWidgetArea, dock);

	extraCustomDockNames.push_back(dock->objectName());
	extraCustomDocks.push_back(dock);
}

void OBSBasic::setDockCornersVertical(bool vertical)
{
	if (vertical) {
		setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	} else {
		setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
		setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
	}
}

void OBSBasic::RepairCustomExtraDockName()
{
	QDockWidget *dock = reinterpret_cast<QDockWidget *>(sender());
	int idx = extraCustomDocks.indexOf(dock);
	QSignalBlocker block(dock);

	if (idx == -1) {
		blog(LOG_WARNING, "A custom dock got its object name changed");
		return;
	}

	blog(LOG_WARNING, "The custom dock '%s' got its object name restored", QT_TO_UTF8(extraCustomDockNames[idx]));

	dock->setObjectName(extraCustomDockNames[idx]);
}

/* ------------------------------------------------------------------------- */
/* Source docks */

SourceDock *OBSBasic::FindSourceDock(const char *uuid)
{
	if (!uuid || !*uuid) {
		return nullptr;
	}

	for (const QPointer<SourceDock> &dock : sourceDocks) {
		if (!dock) {
			continue;
		}

		OBSSource source = dock->GetSource();

		if (source && strcmp(obs_source_get_uuid(source), uuid) == 0) {
			return dock;
		}
	}

	return nullptr;
}

void OBSBasic::AddSourceDock(OBSSource source, bool firstCreate)
{
	if (!source) {
		return;
	}

	SourceDock *dock = new SourceDock(source, this);

	AddDockWidget(dock, Qt::RightDockWidgetArea);
	sourceDocks.push_back(dock);

	/* A dock created on demand starts floating so it appears where the user
	 * is looking; restored ones let saveState() place them. */
	if (firstCreate) {
		dock->setFloating(true);
		dock->resize(400, 300);
		dock->show();
		SaveSourceDocks();
	}
}

void OBSBasic::RemoveSourceDock(const QString &uuid)
{
	for (int i = sourceDocks.size() - 1; i >= 0; i--) {
		SourceDock *dock = sourceDocks[i];

		if (!dock) {
			sourceDocks.removeAt(i);
			continue;
		}

		OBSSource source = dock->GetSource();

		if (source && QString::fromUtf8(obs_source_get_uuid(source)) != uuid) {
			continue;
		}

		const QString name = dock->objectName();
		sourceDocks.removeAt(i);
		RemoveDockWidget(name);
	}

	SaveSourceDocks();
}

void OBSBasic::ToggleSourceDock(const QString &uuid)
{
	if (FindSourceDock(QT_TO_UTF8(uuid))) {
		RemoveSourceDock(uuid);
		return;
	}

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));

	if (source) {
		AddSourceDock(source.Get(), true);
	}
}

void OBSBasic::UpdateSourceDocksMenu()
{
	if (!sourceDocksMenu) {
		return;
	}

	/* Rebuilt every time the menu opens so it always reflects the sources
	 * that exist now rather than a snapshot from startup. */
	sourceDocksMenu->clear();

	struct EnumData {
		OBSBasic *window;
		QMenu *menu;
	};

	EnumData data = {this, sourceDocksMenu};

	auto addSource = [](void *param, obs_source_t *source) {
		EnumData *enumData = static_cast<EnumData *>(param);

		if (obs_source_removed(source)) {
			return true;
		}

		const char *name = obs_source_get_name(source);
		const char *uuid = obs_source_get_uuid(source);

		if (!name || !uuid) {
			return true;
		}

		QAction *action = enumData->menu->addAction(QString::fromUtf8(name));
		action->setCheckable(true);
		action->setChecked(enumData->window->FindSourceDock(uuid) != nullptr);

		OBSBasic *window = enumData->window;
		const QString id = QString::fromUtf8(uuid);

		QObject::connect(action, &QAction::triggered, window, [window, id]() { window->ToggleSourceDock(id); });

		return true;
	};

	obs_enum_sources(addSource, &data);

	if (sourceDocksMenu->isEmpty()) {
		sourceDocksMenu->addAction(QTStr("Basic.Main.Sources"))->setEnabled(false);
	}
}

void OBSBasic::LoadSourceDocks()
{
	const char *jsonStr = config_get_string(App()->GetUserConfig(), "BasicWindow", "SourceDocks");

	if (!jsonStr || !*jsonStr) {
		return;
	}

	std::string err;
	Json json = Json::parse(jsonStr, err);

	if (!err.empty()) {
		return;
	}

	for (const Json &item : json.array_items()) {
		const std::string uuid = item["uuid"].string_value();
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.c_str());

		if (source) {
			AddSourceDock(source.Get(), false);
		}
	}
}

void OBSBasic::SaveSourceDocks()
{
	Json::array array;

	for (const QPointer<SourceDock> &dock : sourceDocks) {
		if (!dock) {
			continue;
		}

		OBSSource source = dock->GetSource();

		if (!source) {
			continue;
		}

		array.push_back(Json::object{{"uuid", obs_source_get_uuid(source)}});
	}

	config_set_string(App()->GetUserConfig(), "BasicWindow", "SourceDocks", Json(array).dump().c_str());
}
