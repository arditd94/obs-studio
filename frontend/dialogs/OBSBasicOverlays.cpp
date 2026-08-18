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

#include "OBSBasicOverlays.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <utility/OverlayManager.hpp>
#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum Column {
	ColumnLayer,
	ColumnSource,
	ColumnTransition,
	ColumnDuration,
	ColumnOn,
	ColumnClear,
	ColumnCount,
};

/* Every source a layer can show, scenes included: a scene makes one layer out of
 * several graphics without needing a layer for each. */
QList<QPair<QString, QString>> AssignableSources()
{
	QList<QPair<QString, QString>> found;

	auto collect = [](void *param, obs_source_t *source) {
		auto *list = static_cast<QList<QPair<QString, QString>> *>(param);

		if (obs_source_removed(source)) {
			return true;
		}

		const char *name = obs_source_get_name(source);
		const char *uuid = obs_source_get_uuid(source);

		if (name && uuid) {
			list->append({QString::fromUtf8(name), QString::fromUtf8(uuid)});
		}

		return true;
	};

	obs_enum_sources(collect, &found);
	obs_enum_scenes(collect, &found);

	std::sort(found.begin(), found.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
		return a.first.localeAwareCompare(b.first) < 0;
	});

	return found;
}

} // namespace

OBSBasicOverlays::OBSBasicOverlays(OBSBasic *parent)
	: QDialog(parent),
	  main(parent),
	  manager(parent->GetOverlayManager())
{
	setWindowTitle(QTStr("Basic.Overlay.Manage"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	resize(760, 300);

	BuildUI();

	for (int i = 0; i < manager->Count(); i++) {
		RefreshRow(i);
	}

	/* Layers are also switched from the toolbar buttons and the hotkeys, so
	 * the panel follows the manager rather than owning the state. */
	connect(manager, &OverlayManager::overlayChanged, this, &OBSBasicOverlays::RefreshRow);
}

void OBSBasicOverlays::BuildUI()
{
	const int count = manager->Count();

	table = new QTableWidget(count, ColumnCount, this);
	table->setHorizontalHeaderLabels({QTStr("Basic.Overlay.Column.Layer"), QTStr("Basic.Overlay.Column.Source"),
					  QTStr("Basic.Overlay.Column.Transition"),
					  QTStr("Basic.Overlay.Column.Duration"), QTStr("Basic.Overlay.Column.On"),
					  QString()});
	table->horizontalHeader()->setSectionResizeMode(ColumnLayer, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColumnSource, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(ColumnTransition, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColumnDuration, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColumnOn, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(ColumnClear, QHeaderView::ResizeToContents);
	table->verticalHeader()->setVisible(false);
	table->setSelectionMode(QAbstractItemView::NoSelection);

	/* Centres a bare control in its cell, which otherwise hugs the left edge
	 * and reads as belonging to the column before it. */
	auto centred = [](QWidget *widget) {
		QWidget *holder = new QWidget();
		QHBoxLayout *layout = new QHBoxLayout(holder);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setAlignment(Qt::AlignCenter);
		layout->addWidget(widget);
		return holder;
	};

	for (int i = 0; i < count; i++) {
		/* Each layer sits on the output channel above the one before it,
		 * so a higher number is drawn over a lower one. */
		QTableWidgetItem *number = new QTableWidgetItem(QString::number(i + 1));
		number->setFlags(Qt::ItemIsEnabled);
		number->setTextAlignment(Qt::AlignCenter);
		table->setItem(i, ColumnLayer, number);

		table->setCellWidget(i, ColumnSource, CreateSourceCombo(i));
		table->setCellWidget(i, ColumnTransition, CreateTransitionCombo(i));

		QSpinBox *duration = new QSpinBox();
		duration->setRange(0, 10000);
		duration->setSingleStep(50);
		duration->setSuffix(QTStr("Basic.Overlay.Duration.Suffix"));

		connect(duration, &QSpinBox::valueChanged, this, [this, i]() { CommitRow(i); });

		table->setCellWidget(i, ColumnDuration, duration);

		QCheckBox *on = new QCheckBox();

		connect(on, &QCheckBox::toggled, this, [this, i](bool checked) {
			if (!refreshing) {
				manager->SetOn(i, checked);
			}
		});

		table->setCellWidget(i, ColumnOn, centred(on));

		QPushButton *clear = new QPushButton(QTStr("Basic.Overlay.Clear"));
		clear->setToolTip(QTStr("Basic.Overlay.Clear.Hint"));

		connect(clear, &QPushButton::clicked, this, [this, i]() { manager->Clear(i); });

		table->setCellWidget(i, ColumnClear, centred(clear));
	}

	hintLabel = new QLabel(QTStr("Basic.Overlay.Hint"), this);
	hintLabel->setWordWrap(true);

	QPushButton *clearAll = new QPushButton(QTStr("Basic.Overlay.ClearAll"), this);
	connect(clearAll, &QPushButton::clicked, this, &OBSBasicOverlays::OnClearAll);

	QHBoxLayout *bottomRow = new QHBoxLayout();
	bottomRow->addWidget(clearAll);
	bottomRow->addStretch();

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(table, 1);
	layout->addLayout(bottomRow);
	layout->addWidget(hintLabel);
	layout->addWidget(buttons);
	setLayout(layout);
}

QComboBox *OBSBasicOverlays::CreateSourceCombo(int index)
{
	QComboBox *combo = new QComboBox();

	connect(combo, &QComboBox::currentIndexChanged, this, [this, index]() { CommitRow(index); });

	return combo;
}

QComboBox *OBSBasicOverlays::CreateTransitionCombo(int index)
{
	QComboBox *combo = new QComboBox();

	for (const char *id : {"cut_transition", "fade_transition"}) {
		combo->addItem(QString::fromUtf8(obs_source_get_display_name(id)), QString::fromUtf8(id));
	}

	connect(combo, &QComboBox::currentIndexChanged, this, [this, index]() { CommitRow(index); });

	return combo;
}

void OBSBasicOverlays::RefreshRow(int index)
{
	if (index < 0 || index >= manager->Count()) {
		return;
	}

	QComboBox *sourceCombo = qobject_cast<QComboBox *>(table->cellWidget(index, ColumnSource));
	QComboBox *transitionCombo = qobject_cast<QComboBox *>(table->cellWidget(index, ColumnTransition));
	QSpinBox *duration = qobject_cast<QSpinBox *>(table->cellWidget(index, ColumnDuration));
	QCheckBox *on = table->cellWidget(index, ColumnOn)->findChild<QCheckBox *>();

	if (!sourceCombo || !transitionCombo || !duration || !on) {
		return;
	}

	refreshing = true;

	const OverlayManager::Config &config = manager->GetConfig(index);

	QList<QPair<QString, QString>> sources = AssignableSources();

	/* A source the layer points at but that is gone stays in the list, or
	 * the row would silently retarget to whatever happens to be first. */
	if (manager->IsSourceMissing(index)) {
		sources.prepend({QTStr("Basic.Overlay.SourceMissing").arg(config.name), config.sourceUuid});
	}

	/* Rebuilt only when the sources themselves changed: the panel is also
	 * refreshed on every toggle, and a combo rebuilt under the pointer would
	 * shut its own popup. */
	bool stale = sourceCombo->count() != sources.size() + 1;

	for (int i = 0; !stale && i < sources.size(); i++) {
		stale = sourceCombo->itemData(i + 1).toString() != sources[i].second ||
			sourceCombo->itemText(i + 1) != sources[i].first;
	}

	if (stale) {
		sourceCombo->clear();
		sourceCombo->addItem(QTStr("Basic.Overlay.NoSource"), QString());

		for (const auto &source : sources) {
			sourceCombo->addItem(source.first, source.second);
		}
	}

	const int sourceIndex = sourceCombo->findData(config.sourceUuid);
	sourceCombo->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 0);

	const int transitionIndex = transitionCombo->findData(config.transitionId);
	transitionCombo->setCurrentIndex(transitionIndex >= 0 ? transitionIndex : 0);

	/* A cut has no length to set, so the box says so by going out rather
	 * than by holding a number that does nothing. */
	duration->setEnabled(config.transitionId != "cut_transition");
	duration->setValue((int)config.durationMs);

	on->setChecked(manager->IsOn(index));

	/* An empty layer has nothing to raise, so its switch is out rather than
	 * looking available. */
	on->setEnabled(manager->GetItem(index) != nullptr);

	refreshing = false;
}

void OBSBasicOverlays::CommitRow(int index)
{
	if (refreshing || index < 0 || index >= manager->Count()) {
		return;
	}

	QComboBox *sourceCombo = qobject_cast<QComboBox *>(table->cellWidget(index, ColumnSource));
	QComboBox *transitionCombo = qobject_cast<QComboBox *>(table->cellWidget(index, ColumnTransition));
	QSpinBox *duration = qobject_cast<QSpinBox *>(table->cellWidget(index, ColumnDuration));

	if (!sourceCombo || !transitionCombo || !duration) {
		return;
	}

	OverlayManager::Config config = manager->GetConfig(index);

	config.sourceUuid = sourceCombo->currentData().toString();
	config.transitionId = transitionCombo->currentData().toString();
	config.durationMs = (uint32_t)duration->value();

	/* The layer takes the name of what it shows, so a renamed source does
	 * not leave a stale label on the button. */
	config.name = sourceCombo->currentData().toString().isEmpty() ? QString() : sourceCombo->currentText();

	manager->SetConfig(index, config);
}

void OBSBasicOverlays::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
		for (int i = 0; i < manager->Count(); i++) {
			RefreshRow(i);
		}
	}

	QDialog::changeEvent(event);
}

void OBSBasicOverlays::OnClearAll()
{
	manager->ClearAll();
}
