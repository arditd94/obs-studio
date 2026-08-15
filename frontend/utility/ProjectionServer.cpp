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

#include "ProjectionServer.hpp"

#include <widgets/OBSBasic.hpp>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

ProjectionServer::ProjectionServer(OBSBasic *parent) : QObject(parent), main(parent) {}

ProjectionServer::~ProjectionServer()
{
	Stop();
}

bool ProjectionServer::Start(quint16 port, const QString &accessKey)
{
	Stop();

	key = accessKey;
	server = new QTcpServer(this);

	connect(server, &QTcpServer::newConnection, this, &ProjectionServer::OnNewConnection);

	if (!server->listen(QHostAddress::Any, port)) {
		blog(LOG_WARNING, "Projection server could not listen on port %u: %s", port,
		     QT_TO_UTF8(server->errorString()));

		server->deleteLater();
		server = nullptr;
		return false;
	}

	blog(LOG_INFO, "Projection server listening on port %u", port);
	return true;
}

void ProjectionServer::Stop()
{
	if (!server) {
		return;
	}

	server->close();
	server->deleteLater();
	server = nullptr;
}

bool ProjectionServer::IsRunning() const
{
	return server && server->isListening();
}

quint16 ProjectionServer::Port() const
{
	return server ? server->serverPort() : 0;
}

QStringList ProjectionServer::LocalAddresses()
{
	QStringList out;

	for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
		if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) {
			continue;
		}

		if (address.isInSubnet(QHostAddress("10.0.0.0"), 8) ||
		    address.isInSubnet(QHostAddress("172.16.0.0"), 12) ||
		    address.isInSubnet(QHostAddress("192.168.0.0"), 16)) {
			out.append(address.toString());
		}
	}

	return out;
}

/* Windows hands back IPv4 peers as IPv4-mapped IPv6 addresses, which match no
 * IPv4 subnet, so the address is folded back to IPv4 before it is tested. */
static bool IsPrivateAddress(const QHostAddress &address)
{
	QHostAddress peer = address;
	bool converted = false;
	const quint32 asIPv4 = peer.toIPv4Address(&converted);

	if (converted) {
		peer = QHostAddress(asIPv4);
	}

	return peer.isLoopback() || peer.isInSubnet(QHostAddress("10.0.0.0"), 8) ||
	       peer.isInSubnet(QHostAddress("172.16.0.0"), 12) || peer.isInSubnet(QHostAddress("192.168.0.0"), 16);
}

void ProjectionServer::OnNewConnection()
{
	while (server && server->hasPendingConnections()) {
		QTcpSocket *socket = server->nextPendingConnection();

		/* Only the local network is ever meant to reach this, so anything
		 * routed in from outside is dropped before it is even read. */
		if (!IsPrivateAddress(socket->peerAddress())) {
			blog(LOG_WARNING, "Projection server refused a connection from %s",
			     QT_TO_UTF8(socket->peerAddress().toString()));
			socket->abort();
			socket->deleteLater();
			continue;
		}

		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
			const QString request = QString::fromUtf8(socket->readAll());

			if (!request.isEmpty()) {
				HandleRequest(socket, request);
			}
		});

		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}
}

void ProjectionServer::Respond(QTcpSocket *socket, int status, const QString &contentType, const QByteArray &body)
{
	const QByteArray header = QString("HTTP/1.1 %1 %2\r\n"
					  "Content-Type: %3\r\n"
					  "Content-Length: %4\r\n"
					  "Cache-Control: no-store\r\n"
					  "Connection: close\r\n"
					  "\r\n")
					  .arg(QString::number(status), status == 200 ? "OK" : "Error", contentType,
					       QString::number(body.size()))
					  .toUtf8();

	socket->write(header);
	socket->write(body);
	socket->flush();
	socket->disconnectFromHost();
}

QByteArray ProjectionServer::StateJson() const
{
	QJsonArray rows;
	const QList<ProjectionEntry> entries = main->GetProjections();

	for (int i = 0; i < entries.size(); i++) {
		QString name = QTStr("Basic.Projections.Program");

		if (!entries[i].sceneUuid.isEmpty()) {
			OBSSourceAutoRelease scene = obs_get_source_by_uuid(QT_TO_UTF8(entries[i].sceneUuid));
			name = scene ? QString::fromUtf8(obs_source_get_name(scene)) : QString("?");
		}

		QJsonObject row;
		row["name"] = name;
		row["monitor"] = entries[i].monitor + 1;
		row["enabled"] = entries[i].enabled;
		row["locked"] = entries[i].locked;
		row["shown"] = main->IsProjectionShown(i);

		rows.append(row);
	}

	QJsonObject state;
	state["rows"] = rows;

	return QJsonDocument(state).toJson(QJsonDocument::Compact);
}

void ProjectionServer::HandleRequest(QTcpSocket *socket, const QString &request)
{
	const QStringList lines = request.split("\r\n");

	if (lines.isEmpty()) {
		Respond(socket, 400, "text/plain", "bad request");
		return;
	}

	const QStringList parts = lines.first().split(' ');

	if (parts.size() < 2) {
		Respond(socket, 400, "text/plain", "bad request");
		return;
	}

	const QUrl url(parts[1]);
	const QString path = url.path();
	const QUrlQuery query(url);

	if (path == "/" || path == "/index.html") {
		Respond(socket, 200, "text/html; charset=utf-8", ControlPage());
		return;
	}

	/* Everything past this point changes or reveals what is on the screens,
	 * so it needs the key. */
	if (query.queryItemValue("k") != key) {
		Respond(socket, 403, "application/json", "{\"error\":\"key\"}");
		return;
	}

	if (path == "/state") {
		Respond(socket, 200, "application/json", StateJson());
		return;
	}

	const int row = query.queryItemValue("row").toInt();
	const bool on = query.queryItemValue("on") == "1";

	if (path == "/toggle") {
		main->SetProjectionEnabled(row, on);
		Respond(socket, 200, "application/json", StateJson());
		return;
	}

	if (path == "/lock") {
		main->SetProjectionLocked(row, on);
		Respond(socket, 200, "application/json", StateJson());
		return;
	}

	Respond(socket, 404, "text/plain", "not found");
}

QByteArray ProjectionServer::ControlPage()
{
	static const char *page = R"HTML(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Proiezioni</title>
<style>
 body { background:#1f2126; color:#e6e6e6; font:16px system-ui,sans-serif; margin:0; padding:16px; }
 h1 { font-size:18px; margin:0 0 16px; }
 .row { display:flex; align-items:center; gap:12px; background:#2a2d33; border-radius:8px;
        padding:12px 14px; margin-bottom:10px; }
 .name { flex:1; font-weight:600; }
 .screen { color:#9aa0a8; font-size:14px; }
 .live { color:#38c172; font-size:13px; font-weight:700; }
 button { border:0; border-radius:6px; padding:10px 16px; font-size:15px; font-weight:600;
          color:#fff; background:#3a3f47; cursor:pointer; }
 button.on { background:#268a3c; }
 button:disabled { opacity:.45; cursor:not-allowed; }
 .lock { background:transparent; font-size:20px; padding:6px 10px; }
 #login { display:flex; gap:8px; margin-bottom:16px; }
 input { flex:1; padding:10px; border-radius:6px; border:1px solid #444; background:#15171a; color:#eee; }
 #err { color:#e4606d; margin-bottom:12px; }
</style>
</head>
<body>
<h1>Proiezioni</h1>
<div id="login"><input id="key" type="password" placeholder="Password" value="1408"><button onclick="load()">Entra</button></div>
<div id="err"></div>
<div id="list"></div>
<script>
let key = "";
function api(path, params) {
  const q = new URLSearchParams(Object.assign({k: key}, params || {}));
  return fetch(path + "?" + q).then(r => {
    if (!r.ok) throw new Error(r.status);
    return r.json();
  });
}
function render(state) {
  document.getElementById("err").textContent = "";
  document.getElementById("login").style.display = "none";
  const list = document.getElementById("list");
  list.innerHTML = "";
  state.rows.forEach((row, i) => {
    const div = document.createElement("div");
    div.className = "row";
    div.innerHTML = '<div class="name">' + row.name +
      '<div class="screen">Schermo ' + row.monitor + (row.shown ? ' &middot; <span class="live">IN ONDA</span>' : '') + '</div></div>';
    const lock = document.createElement("button");
    lock.className = "lock";
    lock.textContent = row.locked ? "\u{1F512}" : "\u{1F513}";
    lock.onclick = () => api("/lock", {row: i, on: row.locked ? 0 : 1}).then(render).catch(fail);
    const btn = document.createElement("button");
    btn.textContent = row.enabled ? "ON" : "OFF";
    btn.className = row.enabled ? "on" : "";
    btn.disabled = row.locked && row.enabled;
    btn.onclick = () => api("/toggle", {row: i, on: row.enabled ? 0 : 1}).then(render).catch(fail);
    div.appendChild(lock);
    div.appendChild(btn);
    list.appendChild(div);
  });
}
function fail(e) {
  document.getElementById("err").textContent = (e.message === "403") ? "Password errata" : "Errore di connessione";
  document.getElementById("login").style.display = "flex";
}
function load() {
  key = document.getElementById("key").value;
  api("/state").then(render).catch(fail);
}
setInterval(() => { if (key) api("/state").then(render).catch(() => {}); }, 1500);
</script>
</body>
</html>
)HTML";

	return QByteArray(page);
}
