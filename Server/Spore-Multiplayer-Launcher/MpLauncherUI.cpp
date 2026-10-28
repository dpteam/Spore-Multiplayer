/****************************************************************************
* Spore Multiplayer - Launcher UI
****************************************************************************/
#include "MpLauncherUI.h"

namespace SporeMP
{
	namespace
	{
		const char* kLauncherHtml = R"HTML(<!doctype html>
<html lang="ru">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Spore Multiplayer</title>
<style>
:root{
  color-scheme:dark;
  --bg:#0d1210;--panel:#161d19;--line:#2a3830;--muted:#8a9c90;
  --text:#e8f0ea;--accent:#7ee2a8;--warn:#f0a05a;
  font-family:"Segoe UI",Arial,sans-serif;font-size:14px;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text)}
h1,h2,p{margin:0}
button,input{font:inherit}
button{
  border:1px solid var(--line);border-radius:8px;background:#1f2a23;
  color:var(--text);padding:10px 16px;cursor:pointer;
}
button:hover:not(:disabled){border-color:var(--accent);background:#26352c}
button:disabled{opacity:.4;cursor:not-allowed}
button.primary{background:var(--accent);border-color:var(--accent);color:#0d1a12;font-weight:700}
button.primary:hover:not(:disabled){background:#a0f0c2}
input{
  width:100%;border:1px solid var(--line);border-radius:8px;background:#0f1512;
  color:var(--text);padding:10px 12px;
}
label{display:block;color:var(--muted);font-size:12px;margin-bottom:6px}
.wrap{max-width:940px;margin:0 auto;padding:32px 24px 60px}
header{display:flex;align-items:center;gap:14px;margin-bottom:26px}
.logo{
  width:44px;height:44px;border-radius:12px;background:var(--accent);
  color:#0d1a12;display:grid;place-items:center;font-weight:900;font-size:18px;
}
header h1{font-size:22px;letter-spacing:-.4px}
header p{color:var(--muted);font-size:12px;margin-top:3px}
.panel{
  border:1px solid var(--line);border-radius:12px;background:var(--panel);
  padding:20px;margin-bottom:16px;
}
.head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:16px}
.head h2{font-size:15px}
.row{display:flex;gap:10px;align-items:end}
.row>div{flex:1}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}
.servers{display:grid;gap:8px}
.server{
  display:flex;align-items:center;gap:12px;background:#0f1512;
  border:1px solid var(--line);border-radius:8px;padding:11px 13px;
}
.server .info{flex:1;min-width:0}
.server strong{display:block;font-size:13px}
.server small{color:var(--muted);font-size:11px}
.tag{
  font:10px Consolas,monospace;padding:2px 6px;border-radius:4px;
  background:#22301f;color:var(--accent);
}
.tag.relay{background:#2a2418;color:var(--warn)}
.empty{
  border:1px dashed var(--line);border-radius:8px;padding:22px;text-align:center;
  color:var(--muted);font-size:12px;
}
#status{margin-top:14px;font-size:12px;color:var(--muted);white-space:pre-wrap}
#status.error{color:var(--warn)}
.foot{color:var(--muted);font-size:11px;margin-top:22px;line-height:1.7}
@media(max-width:760px){.grid{grid-template-columns:1fr}}
</style>

<div class="wrap">
  <header>
    <div class="logo">MP</div>
    <div>
      <h1>Spore Multiplayer</h1>
      <p>Кооп для Spore / Co-op launcher</p>
    </div>
  </header>

  <section class="panel">
    <div class="head">
      <h2>Серверы / Servers</h2>
      <button id="refresh">Обновить</button>
    </div>
    <div id="servers" class="servers">
      <div class="empty">Загрузка списка...</div>
    </div>
    <div id="status"></div>
  </section>

  <div class="grid">
    <section class="panel">
      <div class="head"><h2>Подключиться напрямую</h2></div>
      <div class="row">
        <div>
          <label for="host">Адрес / Address</label>
          <input id="host" value="127.0.0.1" spellcheck="false">
        </div>
        <div style="max-width:110px">
          <label for="port">Порт</label>
          <input id="port" type="number" value="7000" min="1" max="65535">
        </div>
      </div>
      <div class="row" style="margin-top:12px">
        <div>
          <button id="join" class="primary" style="width:100%">Играть</button>
        </div>
      </div>
    </section>

    <section class="panel">
      <div class="head"><h2>Создать игру / Host</h2></div>
      <div class="row">
        <div>
          <label for="session">Название сессии</label>
          <input id="session" value="Spore Co-op">
        </div>
        <div style="max-width:110px">
          <label for="hostPort">Порт</label>
          <input id="hostPort" type="number" value="7000" min="1" max="65535">
        </div>
      </div>
      <div class="row" style="margin-top:12px">
        <div>
          <button id="startHost" class="primary" style="width:100%">Захостить</button>
        </div>
      </div>
    </section>
  </div>

  <p class="foot">
    Игра запускается через Spore ModAPI Launcher. Закройте эту страницу, когда подключитесь —
    лаунчер останется работать, пока открыта игра.
  </p>
</div>

<script>
'use strict';

const $ = (id) => document.getElementById(id);

function setStatus(text, isError) {
  const node = $('status');
  node.textContent = text || '';
  node.className = isError ? 'error' : '';
}

async function api(path, body) {
  const options = body === undefined ? {} : {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  };

  const response = await fetch(path, options);
  const text = await response.text();

  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (e) { data = { error: text }; }

  if (!response.ok) throw new Error(data.error || ('HTTP ' + response.status));
  return data;
}

function renderServers(list) {
  const container = $('servers');
  container.replaceChildren();

  if (!list.length) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = 'Серверов не найдено. Можно подключиться напрямую или захостить самому.';
    container.append(empty);
    return;
  }

  for (const server of list) {
    const row = document.createElement('div');
    row.className = 'server';

    const info = document.createElement('div');
    info.className = 'info';

    const title = document.createElement('strong');
    title.textContent = server.name;

    const detail = document.createElement('small');
    const via = server.relayHost
      ? 'через релей ' + server.relayHost + ':' + server.relayPort
      : server.host + ':' + server.port;
    detail.textContent = via + '  ·  ' + server.players + '/' + server.maxPlayers;

    info.append(title, detail);

    const tag = document.createElement('span');
    tag.className = server.relayHost ? 'tag relay' : 'tag';
    tag.textContent = server.relayHost ? 'RELAY' : 'DIRECT';

    const button = document.createElement('button');
    button.textContent = 'Играть';
    button.onclick = () => joinServer(server, button);

    row.append(info, tag, button);
    container.append(row);
  }
}

async function loadServers() {
  setStatus('Запрос к мастер-серверу...');
  try {
    const data = await api('/api/servers');
    renderServers(data.servers || []);
    setStatus('Найдено серверов: ' + (data.servers || []).length);
  } catch (error) {
    renderServers([]);
    setStatus('Мастер недоступен: ' + error.message, true);
  }
}

async function launch(payload, button) {
  if (button) button.disabled = true;
  setStatus('Запуск Spore...');

  try {
    const data = await api('/api/launch', payload);
    setStatus(data.message || 'Игра запущена.');
  } catch (error) {
    setStatus('Не удалось запустить: ' + error.message, true);
  } finally {
    if (button) button.disabled = false;
  }
}

function joinServer(server, button) {
  launch({
    host: server.host,
    port: server.port,
    relay: server.relayHost ? (server.relayHost + ':' + server.relayPort) : '',
    ticket: server.ticket || ''
  }, button);
}

$('refresh').onclick = loadServers;

$('join').onclick = () => {
  const host = $('host').value.trim();
  const port = parseInt($('port').value, 10);

  if (!host || !port) {
    setStatus('Укажите адрес и порт.', true);
    return;
  }

  launch({ host, port }, $('join'));
};

$('startHost').onclick = () => {
  const port = parseInt($('hostPort').value, 10);
  const session = $('session').value.trim() || 'Spore Co-op';

  if (!port) {
    setStatus('Укажите порт.', true);
    return;
  }

  launch({ host: '127.0.0.1', port, session, hostGame: true }, $('startHost'));
};

loadServers();
</script>
</html>
)HTML";
	}

	const char* GetLauncherHtml()
	{
		return kLauncherHtml;
	}
}
