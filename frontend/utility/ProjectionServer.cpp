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
	state["running"] = main->AreProjectionsRunning();
	state["fade"] = main->ProjectionFadeDuration();

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

	if (path == "/master") {
		main->SetProjectionsRunning(on);
		Respond(socket, 200, "application/json", StateJson());
		return;
	}

	if (path == "/fade") {
		main->SetProjectionFadeDuration(query.queryItemValue("ms").toInt());
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
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Proiezioni</title>
<style>
 :root {
   --bg:#15171b; --card:#20242b; --line:#2d323b; --text:#e9ecf1; --dim:#8b939f;
   --live:#e02d3c; --on:#1f9d4d; --off:#39404a;
 }
 * { box-sizing:border-box; }
 body { margin:0; background:var(--bg); color:var(--text);
        font:16px/1.4 system-ui,-apple-system,Segoe UI,sans-serif;
        padding:16px 16px calc(16px + env(safe-area-inset-bottom)); }
 header { display:flex; align-items:center; gap:10px; margin-bottom:18px; }
 header h1 { font-size:17px; margin:0; letter-spacing:.02em; }
 #dot { width:9px; height:9px; border-radius:50%; background:var(--dim); flex:none; }
 #dot.ok { background:var(--on); }
 #dot.bad { background:var(--live); }

 .screen { background:var(--card); border-radius:14px; margin-bottom:16px; overflow:hidden; }
 .screen > h2 { font-size:12px; text-transform:uppercase; letter-spacing:.09em;
                color:var(--dim); margin:0; padding:14px 16px 10px; }
 .onair { padding:0 16px 14px; display:flex; align-items:center; gap:10px; }
 .onair .badge { background:var(--live); color:#fff; font-size:11px; font-weight:800;
                 letter-spacing:.08em; padding:4px 8px; border-radius:5px; flex:none; }
 .onair .what { font-size:19px; font-weight:700; }
 .onair.empty .what { color:var(--dim); font-weight:500; font-size:16px; }

 .layer { display:flex; align-items:center; gap:12px; padding:12px 16px;
          border-top:1px solid var(--line); }
 .layer .idx { width:22px; text-align:center; color:var(--dim); font-size:13px;
               font-variant-numeric:tabular-nums; flex:none; }
 .layer .name { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
 .layer.front .name { font-weight:700; }
 .layer.covered .name { color:var(--dim); }
 .tag { font-size:11px; font-weight:700; letter-spacing:.05em; color:var(--dim);
        border:1px solid var(--line); border-radius:4px; padding:2px 6px; flex:none; }

 button { border:0; font:inherit; color:#fff; cursor:pointer; -webkit-tap-highlight-color:transparent; }
 .sw { width:74px; height:42px; border-radius:9px; font-size:14px; font-weight:800;
       letter-spacing:.05em; background:var(--off); flex:none; }
 .sw.on { background:var(--on); }
 .sw:disabled { opacity:.4; cursor:not-allowed; }
 .lk { width:42px; height:42px; border-radius:9px; background:transparent; font-size:19px;
       flex:none; opacity:.75; }
 .lk.locked { opacity:1; }

 #gate { max-width:360px; margin:12vh auto 0; text-align:center; }
 #gate p { color:var(--dim); font-size:14px; margin:0 0 18px; }
 #gate input { width:100%; padding:14px; font-size:18px; text-align:center; letter-spacing:.3em;
               border-radius:10px; border:1px solid var(--line); background:#0f1114; color:var(--text); }
 #gate button { width:100%; margin-top:10px; padding:14px; border-radius:10px;
                background:var(--on); font-size:16px; font-weight:700; }
 #err { color:var(--live); font-size:14px; min-height:20px; margin-top:10px; }
 #main { display:none; }
 .master { width:100%; padding:16px; border-radius:12px; font-size:16px; font-weight:800;
           letter-spacing:.06em; background:var(--off); margin-bottom:18px; }
 .master.on { background:var(--live); }

 .fade { background:var(--card); border-radius:14px; padding:14px 16px; margin-bottom:16px; }
 .fade h2 { font-size:12px; text-transform:uppercase; letter-spacing:.09em; color:var(--dim);
            margin:0 0 10px; }
 .fade .opts { display:flex; gap:8px; flex-wrap:wrap; }
 .fade button { flex:1; min-width:64px; height:42px; border-radius:9px; background:var(--off);
                font-size:14px; font-weight:700; }
 .fade button.sel { background:#3d6fd6; }
</style>
</head>
<body>

<div id="gate">
  <h1>Proiezioni</h1>
  <p>Controllo remoto della regia</p>
  <input id="key" type="password" inputmode="numeric" placeholder="Password" autocomplete="off" autofocus>
  <button onclick="enter()">Entra</button>
  <div id="err"></div>
</div>

<div id="main">
  <header><span id="dot"></span><h1>Proiezioni</h1></header>
  <button id="master" class="master" onclick="toggleMaster()">PROIEZIONE</button>
  <div class="fade"><h2>Dissolvenza</h2><div class="opts" id="fades"></div></div>
  <div id="screens"></div>
</div>

<script>
let key = "";
let busy = false;

function api(path, params) {
  const q = new URLSearchParams(Object.assign({k: key}, params || {}));
  return fetch(path + "?" + q).then(r => {
    if (!r.ok) throw new Error(r.status);
    return r.json();
  });
}

function enter() {
  key = document.getElementById("key").value;
  api("/state").then(state => {
    document.getElementById("gate").style.display = "none";
    document.getElementById("main").style.display = "block";
    render(state, true);
  }).catch(e => {
    document.getElementById("err").textContent =
      (e.message === "403") ? "Password errata" : "Nessuna risposta dalla regia";
  });
}

let running = false;
let lastState = "";

/* Rebuilding the list on every poll would move a button out from under a
   finger mid-press, so nothing is redrawn unless something actually changed. */
function render(state, force) {
  const signature = JSON.stringify(state);
  if (!force && signature === lastState) return;
  lastState = signature;

  document.getElementById("dot").className = "ok";
  running = state.running;

  const fades = document.getElementById("fades");
  fades.innerHTML = "";
  [0, 250, 500, 1000, 2000].forEach(ms => {
    const b = document.createElement("button");
    b.textContent = ms === 0 ? "Nessuna" : ms + " ms";
    if (ms === state.fade) b.className = "sel";
    b.onclick = () => setFade(ms);
    fades.appendChild(b);
  });

  const master = document.getElementById("master");
  master.textContent = running ? "PROIEZIONE ATTIVA" : "PROIEZIONE SPENTA";
  master.className = "master" + (running ? " on" : "");

  const byScreen = new Map();
  state.rows.forEach((row, i) => {
    row.index = i;
    if (!byScreen.has(row.monitor)) byScreen.set(row.monitor, []);
    byScreen.get(row.monitor).push(row);
  });

  const wrap = document.getElementById("screens");
  wrap.innerHTML = "";

  [...byScreen.keys()].sort((a, b) => a - b).forEach(monitor => {
    const rows = byScreen.get(monitor);
    const live = running ? rows.find(r => r.shown) : null;

    const card = document.createElement("div");
    card.className = "screen";

    const title = document.createElement("h2");
    title.textContent = "Schermo " + monitor;
    card.appendChild(title);

    const onair = document.createElement("div");
    onair.className = live ? "onair" : "onair empty";
    onair.innerHTML = live
      ? '<span class="badge">IN ONDA</span><span class="what"></span>'
      : '<span class="what">' + (running ? "Schermo nero" : "Proiezione spenta") + '</span>';
    if (live) onair.querySelector(".what").textContent = live.name;
    card.appendChild(onair);

    rows.forEach((row, pos) => {
      const el = document.createElement("div");
      el.className = "layer " + (row.shown ? "front" : (row.enabled ? "covered" : ""));

      const idx = document.createElement("span");
      idx.className = "idx";
      idx.textContent = pos + 1;

      const name = document.createElement("span");
      name.className = "name";
      name.textContent = row.name;

      el.appendChild(idx);
      el.appendChild(name);

      if (running && row.enabled && !row.shown) {
        const tag = document.createElement("span");
        tag.className = "tag";
        tag.textContent = "COPERTA";
        el.appendChild(tag);
      }

      const lock = document.createElement("button");
      lock.className = "lk" + (row.locked ? " locked" : "");
      lock.textContent = row.locked ? "\u{1F512}" : "\u{1F513}";
      lock.onclick = () => send("/lock", row.index, row.locked ? 0 : 1);

      const sw = document.createElement("button");
      sw.className = "sw" + (row.enabled ? " on" : "");
      sw.textContent = row.enabled ? "ON" : "OFF";
      sw.disabled = row.locked && row.enabled;
      sw.onclick = () => send("/toggle", row.index, row.enabled ? 0 : 1);

      el.appendChild(lock);
      el.appendChild(sw);
      card.appendChild(el);
    });

    wrap.appendChild(card);
  });
}

function setFade(ms) {
  busy = true;
  api("/fade", {ms: ms})
    .then(render)
    .catch(() => { document.getElementById("dot").className = "bad"; })
    .finally(() => { busy = false; });
}

function toggleMaster() {
  busy = true;
  api("/master", {on: running ? 0 : 1})
    .then(render)
    .catch(() => { document.getElementById("dot").className = "bad"; })
    .finally(() => { busy = false; });
}

function send(path, row, on) {
  busy = true;
  api(path, {row: row, on: on})
    .then(render)
    .catch(() => { document.getElementById("dot").className = "bad"; })
    .finally(() => { busy = false; });
}

setInterval(() => {
  if (!key || busy) return;
  api("/state").then(render).catch(() => {
    document.getElementById("dot").className = "bad";
  });
}, 1200);
</script>
</body>
</html>
)HTML";

	return QByteArray(page);
}
