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

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

OBSBasicProjections::OBSBasicProjections(OBSBasic *parent) : QDialog(parent), main(parent)
{
	setWindowTitle(QTStr("Basic.Projections"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	resize(720, 400);

	BuildUI();
	Refresh();

	/* The state also changes from the master button or from a projector
	 * being dismissed, so the panel follows rather than owns it. */
	connect(main, &OBSBasic::projectionsChanged, this, &OBSBasicProjections::Refresh);
}

void OBSBasicProjections::BuildUI()
{
	table = new QTableWidget(0, 4, this);
	table->setHorizontalHeaderLabels({QTStr("Basic.Projections.Layer"), QTStr("Basic.Projections.Content"),
					  QTStr("Basic.Projections.Screen"), QTStr("Basic.Projections.On")});
	/* The first column stays a plain item on purpose: every other cell holds
	 * a widget that would swallow the press, leaving nothing to drag a row
	 * by. */
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
	table->verticalHeader()->setVisible(false);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);

	/* Rows are layers, so they can be dragged into order. The drop is
	 * handled here rather than by the view because the cell widgets do not
	 * travel with a row the view moves itself. */
	table->setDragDropMode(QAbstractItemView::InternalMove);
	table->setDragDropOverwriteMode(false);
	table->viewport()->installEventFilter(this);

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

	/* Applies to both the master switch and the per-row ones, so it is
	 * stored the moment it changes. */
	connect(fadeCombo, &QComboBox::currentIndexChanged, this, [this]() {
		config_set_int(App()->GetUserConfig(), "BasicWindow", "ProjectFadeDuration",
			       fadeCombo->currentData().toInt());
	});

	QHBoxLayout *fadeRow = new QHBoxLayout();
	fadeRow->addWidget(new QLabel(QTStr("Basic.Project.Fade"), this));
	fadeRow->addWidget(fadeCombo);
	fadeRow->addStretch();

	warningLabel = new QLabel(this);
	warningLabel->setWordWrap(true);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(table, 1);
	layout->addLayout(rowButtons);
	layout->addLayout(fadeRow);
	layout->addWidget(warningLabel);
	layout->addWidget(buttons);
	setLayout(layout);
}

QComboBox *OBSBasicProjections::CreateSceneCombo(const QString &sceneUuid, int row)
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

	connect(combo, &QComboBox::currentIndexChanged, this, [this, row]() { CommitRow(row); });

	return combo;
}

QComboBox *OBSBasicProjections::CreateMonitorCombo(int monitor, int row)
{
	QComboBox *combo = new QComboBox();

	const QList<QString> screens = OBSBasic::GetProjectorMenuMonitorsFormatted();

	for (int i = 0; i < screens.size(); i++) {
		combo->addItem(screens[i], i);
	}

	const int index = combo->findData(monitor);
	combo->setCurrentIndex(index >= 0 ? index : 0);

	connect(combo, &QComboBox::currentIndexChanged, this, [this, row]() { CommitRow(row); });

	return combo;
}

void OBSBasicProjections::Refresh()
{
	refreshing = true;

	const QList<ProjectionEntry> entries = main->GetProjections();

	table->setRowCount(0);

	for (int row = 0; row < entries.size(); row++) {
		table->insertRow(row);

		QTableWidgetItem *handle = new QTableWidgetItem(QString::number(row + 1));
		handle->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
		handle->setTextAlignment(Qt::AlignCenter);
		handle->setToolTip(QTStr("Basic.Projections.DragHint"));
		table->setItem(row, 0, handle);

		table->setCellWidget(row, 1, CreateSceneCombo(entries[row].sceneUuid, row));
		table->setCellWidget(row, 2, CreateMonitorCombo(entries[row].monitor, row));

		QCheckBox *check = new QCheckBox();
		check->setChecked(entries[row].enabled);

		/* A line can be enabled yet hidden behind a layer in front of
		 * it, so the tooltip says which of the two it is. */
		check->setToolTip(main->IsProjectionShown(row) ? QTStr("Basic.Projections.OnAir")
							      : QTStr("Basic.Projections.Covered"));

		connect(check, &QCheckBox::toggled, this, [this, row](bool on) {
			if (!refreshing) {
				main->SetProjectionEnabled(row, on);
			}
		});

		/* Centred in its cell, since a bare checkbox hugs the left edge
		 * and reads as belonging to the screen column. */
		QWidget *holder = new QWidget();
		QHBoxLayout *holderLayout = new QHBoxLayout(holder);
		holderLayout->setContentsMargins(0, 0, 0, 0);
		holderLayout->setAlignment(Qt::AlignCenter);
		holderLayout->addWidget(check);

		table->setCellWidget(row, 3, holder);
	}

	removeButton->setEnabled(table->rowCount() > 0);

	refreshing = false;

	UpdateWarning();
}

void OBSBasicProjections::CommitRow(int row)
{
	if (refreshing || row < 0 || row >= table->rowCount()) {
		return;
	}

	QComboBox *sceneCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
	QComboBox *monitorCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 2));

	if (!sceneCombo || !monitorCombo) {
		return;
	}

	ProjectionEntry entry;
	entry.sceneUuid = sceneCombo->currentData().toString();
	entry.monitor = monitorCombo->currentData().toInt();

	main->SetProjectionEntry(row, entry);

	UpdateWarning();
}

void OBSBasicProjections::UpdateWarning()
{
	/* Several lines on one screen is the point rather than a mistake: the
	 * hint explains which one wins. */
	warningLabel->setText(QTStr("Basic.Projections.LayerHint"));
}

bool OBSBasicProjections::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == table->viewport() && event->type() == QEvent::Drop) {
		QDropEvent *drop = static_cast<QDropEvent *>(event);
		const int from = table->currentRow();

		QModelIndex target = table->indexAt(drop->position().toPoint());
		int to = target.isValid() ? target.row() : table->rowCount();

		/* Dropping on the lower half of a row means after it. */
		if (target.isValid()) {
			const QRect rect = table->visualRect(target);

			if (drop->position().toPoint().y() > rect.center().y()) {
				to += 1;
			}
		}

		main->MoveProjectionEntry(from, to);

		drop->accept();
		return true;
	}

	return QDialog::eventFilter(watched, event);
}

void OBSBasicProjections::OnAdd()
{
	ProjectionEntry entry;
	entry.sceneUuid = QString();
	entry.monitor = 0;
	entry.enabled = true;

	main->AddProjectionEntry(entry);

	table->selectRow(table->rowCount() - 1);
}

void OBSBasicProjections::OnRemove()
{
	const int row = table->currentRow();

	if (row < 0) {
		return;
	}

	main->RemoveProjectionEntry(row);
}
