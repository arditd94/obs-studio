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

#include <QObject>
#include <QString>

class OBSBasic;
class QTcpServer;
class QTcpSocket;

/* Serves a small control page so a second machine in the control room can raise
 * and drop projections without touching the OBS running the show.
 *
 * Deliberately minimal: it answers a handful of paths over plain HTTP, refuses
 * anything that does not come from a private address, and requires a key on
 * every call that changes something. */
class ProjectionServer : public QObject {
	Q_OBJECT

	OBSBasic *main;
	QTcpServer *server = nullptr;
	QString key;

	void HandleRequest(QTcpSocket *socket, const QString &request);
	void Respond(QTcpSocket *socket, int status, const QString &contentType, const QByteArray &body);

	QByteArray StateJson() const;

	/* The control page itself, held as a literal so there is no file to
	 * install or find at runtime. */
	static QByteArray ControlPage();

private slots:
	void OnNewConnection();

public:
	explicit ProjectionServer(OBSBasic *parent);
	~ProjectionServer();

	bool Start(quint16 port, const QString &accessKey);
	void Stop();

	bool IsRunning() const;
	quint16 Port() const;

	/* Addresses the control room can reach this server on, for display. */
	static QStringList LocalAddresses();
};
