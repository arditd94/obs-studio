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
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

/* Lists what goes to which screen. One row per projection: a scene, or the
 * program output, paired with a screen. */
class OBSBasicProjections : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	QTableWidget *table = nullptr;
	QComboBox *fadeCombo = nullptr;
	QPushButton *removeButton = nullptr;
	QLabel *warningLabel = nullptr;

	void BuildUI();
	void FillRow(int row, const QString &sceneUuid, int monitor);

	/* Scene combo entries carry the uuid; the program output uses an empty
	 * one so it survives scene renames. */
	QComboBox *CreateSceneCombo(const QString &sceneUuid);
	QComboBox *CreateMonitorCombo(int monitor);

	void UpdateWarning();
	void Apply();

private slots:
	void OnAdd();
	void OnRemove();
	void OnAccept();

public:
	explicit OBSBasicProjections(OBSBasic *parent);
};
