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
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

/* Lists what goes to which screen, one row per projection: a scene or the
 * program output, a screen, and a switch for that line alone.
 *
 * Edits apply immediately rather than on OK, since the panel is meant to stay
 * open beside the mixer while a show runs. */
class OBSBasicProjections : public QDialog {
	Q_OBJECT

	OBSBasic *main;

	QTableWidget *table = nullptr;
	QComboBox *fadeCombo = nullptr;
	QCheckBox *remoteCheck = nullptr;
	QSpinBox *portSpin = nullptr;
	QLineEdit *keyEdit = nullptr;
	QLabel *remoteLabel = nullptr;
	QPushButton *removeButton = nullptr;
	QLabel *warningLabel = nullptr;

	/* Set while the table is being rebuilt from the current state, so the
	 * widgets' own signals do not write back what they just displayed. */
	bool refreshing = false;

	void BuildUI();
	void Refresh();

	QComboBox *CreateSceneCombo(const QString &sceneUuid, int row);
	QComboBox *CreateMonitorCombo(int monitor, int row);

	void CommitRow(int row);
	void UpdateWarning();
	void UpdateRemoteLabel();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void OnAdd();
	void OnRemove();
	void OnClear();
	void OnRemoteToggled(bool on);

public:
	explicit OBSBasicProjections(OBSBasic *parent);
};
