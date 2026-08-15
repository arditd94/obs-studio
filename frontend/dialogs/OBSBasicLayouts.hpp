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

#include <components/LayoutView.hpp>
#include <components/SceneLayout.hpp>

#include <obs.hpp>

#include <QDialog>

#include <string>
#include <vector>

class FlowLayout;
class OBSBasic;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/* Applies a layout to the current scene: the preset grid picks the geometry,
 * the slot column picks which source lands in which box. */
class OBSBasicLayouts : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	FlowLayout *presetFlow = nullptr;
	QWidget *presetContainer = nullptr;
	std::vector<LayoutView *> presetViews;

	QVBoxLayout *slotBox = nullptr;
	std::vector<QComboBox *> slotCombos;

	LayoutView *editor = nullptr;
	QLabel *statusLabel = nullptr;

	QPushButton *applyButton = nullptr;
	QPushButton *invertButton = nullptr;
	QPushButton *duplicateButton = nullptr;
	QPushButton *deleteButton = nullptr;
	QPushButton *renameButton = nullptr;
	QPushButton *exportButton = nullptr;
	QPushButton *addSlotButton = nullptr;
	QPushButton *removeSlotButton = nullptr;

	/* Alignment commands, enabled together whenever an editable slot is
	 * selected. */
	std::vector<QPushButton *> alignButtons;

	/* Working copy of the selected layout. Edits are written back to the
	 * manager only for user layouts. */
	SceneLayout current;

	void BuildUI();
	void RebuildPresets();
	void RebuildSlotRows();
	void PopulateSourceCombo(QComboBox *combo, int slotIndex);

	/* Pushes the current assignment names into the editor so each box shows
	 * which source it holds. */
	void UpdateSlotLabels();
	void SelectLayout(const std::string &id);
	void UpdateButtonStates();
	void CommitCurrentLayout();

	/* Returns the scene the dialog acts on, or nullptr when no scene is
	 * selected in the main window. */
	OBSScene CurrentScene();

private slots:
	void OnPresetClicked();
	void OnApply();
	void OnAdd();
	void OnDuplicate();
	void OnDelete();
	void OnRename();
	void OnImport();
	void OnExport();
	void OnAddSlot();
	void OnRemoveSlot();
	void OnEditorChanged();
	void OnSlotSelected(int index);
	void OnInvert();

public:
	explicit OBSBasicLayouts(OBSBasic *parent);
};
