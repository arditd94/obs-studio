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

#include "OBSBasicProjections.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

OBSBasicProjections::OBSBasicProjections(OBSBasic *parent) : QDialog(parent), main(parent)
{
	setWindowTitle(QTStr("Basic.Projections"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	resize(640, 380);

	BuildUI();

	const QList<ProjectionEntry> entries = main->GetProjections();

	for (const ProjectionEntry &entry : entries) {
		const int row = table->rowCount();
		table->insertRow(row);
		FillRow(row, entry.sceneUuid, entry.monitor);
	}

	UpdateWarning();
}

void OBSBasicProjections::BuildUI()
{
	table = new QTableWidget(0, 2, this);
	table->setHorizontalHeaderLabels({QTStr("Basic.Projections.Content"), QTStr("Basic.Projections.Screen")});
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);

	QPushButton *addButton = new QPushButton(QTStr("Basic.Projections.Add"), this);
	removeButton = new QPushButton(QTStr("Basic.Projections.Remove"), this);

	connect(addButton, &QPushButton::clicked, this, &OBSBasicProjections::OnAdd);
	connect(removeButton, &QPushButton::clicked, this, &OBSBasicProjections::OnRemove);

	QHBoxLayout *rowButtons = new QHBoxLayout();
	rowButtons->addWidget(addButton);
	rowButtons->addWidget(removeButton);
	rowButtons->addStretch();

	fadeCombo = new QComboBox(this);
	const int currentFade = (int)config_get_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration");

	for (int ms : {0, 250, 500, 1000, 2000}) {
		fadeCombo->addItem(ms == 0 ? QTStr("Basic.Project.Fade.None")
					   : QTStr("Basic.Project.Fade.Ms").arg(QString::number(ms)),
				   ms);

		if (ms == currentFade) {
			fadeCombo->setCurrentIndex(fadeCombo->count() - 1);
		}
	}

	QHBoxLayout *fadeRow = new QHBoxLayout();
	fadeRow->addWidget(new QLabel(QTStr("Basic.Project.Fade"), this));
	fadeRow->addWidget(fadeCombo);
	fadeRow->addStretch();

	warningLabel = new QLabel(this);
	warningLabel->setWordWrap(true);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &OBSBasicProjections::OnAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(table, 1);
	layout->addLayout(rowButtons);
	layout->addLayout(fadeRow);
	layout->addWidget(warningLabel);
	layout->addWidget(buttons);
	setLayout(layout);
}

QComboBox *OBSBasicProjections::CreateSceneCombo(const QString &sceneUuid)
{
	QComboBox *combo = new QComboBox();

	combo->addItem(QTStr("Basic.Projections.Program"), QString());

	auto addScene = [](void *param, obs_source_t *scene) {
		QComboBox *target = static_cast<QComboBox *>(param);

		if (obs_source_removed(scene)) {
			return true;
		}

		const char *name = obs_source_get_name(scene);
		const char *uuid = obs_source_get_uuid(scene);

		if (name && uuid) {
			target->addItem(QString::fromUtf8(name), QString::fromUtf8(uuid));
		}

		return true;
	};

	obs_enum_scenes(addScene, combo);

	const int index = combo->findData(sceneUuid);
	combo->setCurrentIndex(index >= 0 ? index : 0);

	connect(combo, &QComboBox::currentIndexChanged, this, [this]() { UpdateWarning(); });

	return combo;
}

QComboBox *OBSBasicProjections::CreateMonitorCombo(int monitor)
{
	QComboBox *combo = new QComboBox();

	const QList<QString> screens = OBSBasic::GetProjectorMenuMonitorsFormatted();

	for (int i = 0; i < screens.size(); i++) {
		combo->addItem(screens[i], i);
	}

	const int index = combo->findData(monitor);
	combo->setCurrentIndex(index >= 0 ? index : 0);

	connect(combo, &QComboBox::currentIndexChanged, this, [this]() { UpdateWarning(); });

	return combo;
}

void OBSBasicProjections::FillRow(int row, const QString &sceneUuid, int monitor)
{
	table->setCellWidget(row, 0, CreateSceneCombo(sceneUuid));
	table->setCellWidget(row, 1, CreateMonitorCombo(monitor));
}

void OBSBasicProjections::UpdateWarning()
{
	QSet<int> seen;
	bool duplicate = false;

	for (int row = 0; row < table->rowCount(); row++) {
		QComboBox *monitorCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 1));

		if (!monitorCombo) {
			continue;
		}

		const int monitor = monitorCombo->currentData().toInt();

		if (seen.contains(monitor)) {
			duplicate = true;
			break;
		}

		seen.insert(monitor);
	}

	/* A screen can only show one thing, so a duplicate is flagged rather
	 * than silently dropped when the projection starts. */
	warningLabel->setText(duplicate ? QTStr("Basic.Projections.DuplicateScreen") : QString());
	removeButton->setEnabled(table->rowCount() > 0);
}

void OBSBasicProjections::OnAdd()
{
	const int row = table->rowCount();
	table->insertRow(row);
	FillRow(row, QString(), 0);
	table->selectRow(row);

	UpdateWarning();
}

void OBSBasicProjections::OnRemove()
{
	const int row = table->currentRow();

	if (row < 0) {
		return;
	}

	table->removeRow(row);
	UpdateWarning();
}

void OBSBasicProjections::Apply()
{
	QList<ProjectionEntry> entries;

	for (int row = 0; row < table->rowCount(); row++) {
		QComboBox *sceneCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
		QComboBox *monitorCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 1));

		if (!sceneCombo || !monitorCombo) {
			continue;
		}

		ProjectionEntry entry;
		entry.sceneUuid = sceneCombo->currentData().toString();
		entry.monitor = monitorCombo->currentData().toInt();

		entries.append(entry);
	}

	config_set_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration", fadeCombo->currentData().toInt());

	main->SetProjections(entries);
}

void OBSBasicProjections::OnAccept()
{
	Apply();
	accept();
}
