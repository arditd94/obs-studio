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

#include <QDialog>

class OBSBasic;
class OverlayManager;
class QComboBox;
class QLabel;
class QTableWidget;

/* What each overlay layer holds, one row per layer: the source it shows, how it
 * comes and goes, and a switch for that layer alone.
 *
 * The rows are fixed rather than added and removed, because the layers are the
 * output channels above the scene and there are as many as the manager declares.
 *
 * Edits apply immediately rather than on OK, so the panel can stay open beside
 * the mixer while a show runs. */
class OBSBasicOverlays : public QDialog {
	Q_OBJECT

	OBSBasic *main;
	OverlayManager *manager;

	QTableWidget *table = nullptr;
	QLabel *hintLabel = nullptr;

	/* Set while a row is being rebuilt from the manager's state, so the
	 * widgets' own signals do not write back what they were just handed. */
	bool refreshing = false;

	void BuildUI();
	void RefreshRow(int index);

	QComboBox *CreateSourceCombo(int index);
	QComboBox *CreateTransitionCombo(int index);

	void CommitRow(int index);

protected:
	/* Sources come and go in the main window while this stays open, so the
	 * lists are read again whenever the panel is come back to. */
	void changeEvent(QEvent *event) override;

private slots:
	void OnClearAll();

public:
	explicit OBSBasicOverlays(OBSBasic *parent);
};
