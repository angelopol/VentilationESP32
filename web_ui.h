// Interfaz web (PWA) servida por el ESP32.
#pragma once
#include <Arduino.h>

const char MANIFEST_JSON[] PROGMEM = R"json({
  "name": "Vento",
  "short_name": "Vento",
  "description": "Control del ventilador",
  "start_url": "/",
  "scope": "/",
  "display": "standalone",
  "orientation": "portrait",
  "background_color": "#0b1220",
  "theme_color": "#0b1220",
  "icons": [
    { "src": "/icon-192.png", "sizes": "192x192", "type": "image/png" },
    { "src": "/icon-512.png", "sizes": "512x512", "type": "image/png" },
    { "src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable" }
  ]
})json";

const char APP_CSS[] PROGMEM = R"css(:root{
  --bg:#0b1220;--card:#131c2e;--card2:#1a2540;--text:#e8eefc;--muted:#8a97b4;
  --accent:#0ea5e9;--accent2:#38bdf8;--ok:#22c55e;--bad:#ef4444;--line:#24314f;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);
  font:16px/1.4 -apple-system,BlinkMacSystemFont,"SF Pro Text","Segoe UI",Roboto,sans-serif}
body{padding:calc(env(safe-area-inset-top) + 12px) 16px calc(env(safe-area-inset-bottom) + 24px);
  max-width:520px;margin:0 auto;-webkit-user-select:none;user-select:none}
header{display:flex;align-items:center;justify-content:space-between;margin:4px 0 16px}
h1{font-size:28px;margin:0;letter-spacing:-.5px}
.status{display:flex;align-items:center;gap:6px;font-size:13px;color:var(--muted)}
.dot{width:8px;height:8px;border-radius:50%;background:var(--bad)}
.dot.on{background:var(--ok)}
.card{background:var(--card);border:1px solid var(--line);border-radius:20px;padding:18px;margin-bottom:14px}
.label{font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin-bottom:10px}
.hero{display:flex;align-items:center;gap:18px}
.fan{width:84px;height:84px;flex:none;color:var(--accent2)}
.fan svg{width:100%;height:100%}
.fan .rotor{transform-origin:50% 50%;animation:spin var(--spd,0s) linear infinite}
.fan.off .rotor{animation:none}
@keyframes spin{to{transform:rotate(360deg)}}
.big{font-size:46px;font-weight:700;letter-spacing:-1.5px;line-height:1}
.sub{color:var(--muted);margin-top:6px;font-size:15px}
.bar{height:6px;background:var(--card2);border-radius:3px;margin-top:16px;overflow:hidden}
.bar i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--accent),var(--accent2));transition:width .4s}
.row{display:grid;grid-template-columns:repeat(6,1fr);gap:8px}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}
button{font:inherit;color:var(--text);background:var(--card2);border:1px solid var(--line);
  border-radius:14px;padding:14px 4px;font-weight:600;cursor:pointer;transition:background .15s,transform .1s}
button:active{transform:scale(.96)}
button.sel{background:var(--accent);border-color:var(--accent);color:#fff}
button small{display:block;font-weight:400;font-size:12px;opacity:.75;margin-top:2px}
.sp{display:flex;align-items:baseline;justify-content:space-between}
.sp b{font-size:28px}
input[type=range]{width:100%;margin:14px 0 4px;accent-color:var(--accent);height:28px}
.hint{font-size:13px;color:var(--muted)}
.dim{opacity:.45}
.toast{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom) + 20px);transform:translateX(-50%);
  background:var(--bad);color:#fff;padding:10px 16px;border-radius:12px;font-size:14px;display:none}
a{color:var(--accent2);text-decoration:none}
footer{text-align:center;margin-top:18px;font-size:14px}
input[type=text],input[type=password]{width:100%;font:inherit;color:var(--text);background:var(--card2);
  border:1px solid var(--line);border-radius:12px;padding:12px;margin-top:6px;-webkit-user-select:text;user-select:text}
.field{margin-bottom:12px}
.field span{font-size:13px;color:var(--muted)}
.primary{width:100%;background:var(--accent);border-color:var(--accent);color:#fff}
.link{background:none;border:0;color:var(--accent2);font-weight:400;padding:8px}
.nets{display:flex;flex-direction:column;gap:6px;margin-bottom:12px}
.net{display:flex;justify-content:space-between;text-align:left;padding:12px 14px;font-weight:500}
.net em{font-style:normal;color:var(--muted);font-size:13px}
.msg{padding:12px 14px;border-radius:12px;background:var(--card2);margin-top:12px;display:none}
.msg.ok{background:rgba(34,197,94,.15);color:#86efac}
.msg.err{background:rgba(239,68,68,.15);color:#fca5a5}
.pw{position:relative}
.pw .link{position:absolute;right:6px;bottom:6px;font-size:13px}
)css";

const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover, user-scalable=no">
<title>Vento</title>
<meta name="theme-color" content="#0b1220">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="Vento">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="icon" type="image/png" href="/icon-192.png">
<link rel="stylesheet" href="/app.css">
</head>
<body>
<header>
  <h1>Vento</h1>
  <div class="status"><span class="dot" id="dot"></span><span id="conn">Conectando…</span></div>
</header>

<section class="card">
  <div class="hero">
    <div class="fan off" id="fan">
      <svg viewBox="0 0 100 100" fill="currentColor">
        <circle cx="50" cy="50" r="46" fill="none" stroke="currentColor" stroke-width="4"/>
        <g class="rotor">
          <path d="M50 50C44 36 46 16 60 14c10 0 12 14 2 24-4 4-8 8-12 12z"/>
          <path d="M50 50C44 36 46 16 60 14c10 0 12 14 2 24-4 4-8 8-12 12z" transform="rotate(120 50 50)"/>
          <path d="M50 50C44 36 46 16 60 14c10 0 12 14 2 24-4 4-8 8-12 12z" transform="rotate(240 50 50)"/>
          <circle cx="50" cy="50" r="8"/>
        </g>
      </svg>
    </div>
    <div>
      <div class="label" style="margin:0 0 6px">Sensación térmica</div>
      <div class="big" id="hic">--°</div>
      <div class="sub"><span id="temp">--</span> · <span id="hum">--</span></div>
    </div>
  </div>
  <div class="bar"><i id="pwm"></i></div>
</section>

<section class="card">
  <div class="label">Velocidad</div>
  <div class="row" id="speeds">
    <button data-m="0">Off</button>
    <button data-m="1">1</button>
    <button data-m="2">2</button>
    <button data-m="3">3</button>
    <button data-m="4">4</button>
    <button data-m="5">5</button>
  </div>
  <div class="row2">
    <button data-m="6">Auto<small>Enciende al superar</small></button>
    <button data-m="7">Progresivo<small>Según temperatura</small></button>
  </div>
</section>

<section class="card" id="spCard">
  <div class="sp"><div class="label" style="margin:0">Temperatura objetivo</div><b id="spv">--°</b></div>
  <input type="range" id="sp" min="16" max="70" step="3">
  <div class="hint">Se usa en los modos Auto y Progresivo.</div>
</section>

<footer><a href="/wifi">Configurar WiFi</a></footer>

<div class="toast" id="toast">No se pudo contactar con el ventilador</div>

<script>
const $ = id => document.getElementById(id);
const btns = [...document.querySelectorAll('button[data-m]')];
let state = null, dragging = false, timer = null, toastT = null;

function fmt(v, unit){ return v === null || v === undefined ? '--' : v.toFixed(1) + unit; }

function render(s){
  state = s;
  $('hic').textContent = s.hic === null ? '--°' : s.hic.toFixed(1) + '°';
  $('temp').textContent = fmt(s.temp, ' °C');
  $('hum').textContent = fmt(s.hum, ' %');
  btns.forEach(b => b.classList.toggle('sel', +b.dataset.m === s.mode));
  if (!dragging){ $('sp').value = s.setpoint; $('spv').textContent = s.setpoint + '°'; }
  $('spCard').classList.toggle('dim', s.mode < 6);
  const pct = Math.round(s.pwm / 255 * 100);
  $('pwm').style.width = pct + '%';
  const fan = $('fan');
  fan.classList.toggle('off', pct === 0);
  if (pct) fan.style.setProperty('--spd', (2.2 - pct / 100 * 1.8).toFixed(2) + 's');
}

function online(ok){
  $('dot').classList.toggle('on', ok);
  $('conn').textContent = ok ? 'Conectado' : 'Sin conexión';
}

function toast(){
  $('toast').style.display = 'block';
  clearTimeout(toastT);
  toastT = setTimeout(() => $('toast').style.display = 'none', 2500);
}

async function api(path, opts){
  const r = await fetch(path, Object.assign({cache: 'no-store'}, opts));
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

async function poll(){
  try { render(await api('/api/state')); online(true); }
  catch(e){ online(false); }
}

async function send(path){
  try { render(await api(path, {method: 'POST'})); online(true); }
  catch(e){ online(false); toast(); poll(); }
}

btns.forEach(b => b.addEventListener('click', () => {
  if (state) render(Object.assign({}, state, {mode: +b.dataset.m}));
  send('/api/mode?v=' + b.dataset.m);
}));

const sp = $('sp');
sp.addEventListener('input', () => { dragging = true; $('spv').textContent = sp.value + '°'; });
sp.addEventListener('change', () => { dragging = false; send('/api/setpoint?v=' + sp.value); });

function start(){ poll(); clearInterval(timer); timer = setInterval(poll, 2000); }
document.addEventListener('visibilitychange', () => {
  if (document.hidden) clearInterval(timer); else start();
});
start();
</script>
</body>
</html>
)html";

const char WIFI_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Vento · WiFi</title>
<meta name="theme-color" content="#0b1220">
<link rel="apple-touch-icon" href="/apple-touch-icon.png">
<link rel="icon" type="image/png" href="/icon-192.png">
<link rel="stylesheet" href="/app.css">
</head>
<body>
<header>
  <h1>WiFi</h1>
  <a href="/">Control ›</a>
</header>

<section class="card">
  <div class="label">Estado</div>
  <div id="status">Consultando…</div>
  <div class="hint" id="saved"></div>
  <button class="link" id="forget" style="display:none;padding-left:0">Olvidar red guardada</button>
</section>

<section class="card">
  <div class="sp"><div class="label" style="margin:0">Redes cercanas</div>
    <button class="link" id="rescan">Buscar</button></div>
  <div class="nets" id="nets"><div class="hint">Buscando redes…</div></div>

  <form id="form" autocomplete="off">
    <label class="field" style="display:block"><span>Nombre de la red</span>
      <input type="text" id="ssid" maxlength="32" autocapitalize="none" autocorrect="off" required></label>
    <label class="field pw" style="display:block"><span>Contraseña</span>
      <input type="password" id="pass" maxlength="63" autocapitalize="none" autocorrect="off">
      <button type="button" class="link" id="show">Mostrar</button></label>
    <div class="hint" style="margin-bottom:12px">Vento solo funciona con redes de 2,4 GHz.</div>
    <button class="primary" type="submit" id="go">Conectar</button>
  </form>
  <div class="msg" id="msg"></div>
</section>

<script>
const $ = id => document.getElementById(id);
let target = null, watch = null, lost = 0;

function stopWatch(){ clearInterval(watch); watch = null; }

function msg(text, cls){
  const m = $('msg');
  m.textContent = text; m.className = 'msg ' + (cls || ''); m.style.display = text ? 'block' : 'none';
}

async function get(path){
  const r = await fetch(path, {cache: 'no-store'});
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

async function post(path, body){
  const r = await fetch(path, {method: 'POST', body: new URLSearchParams(body || {})});
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(j.error || r.status);
  return j;
}

function showStatus(s){
  let t;
  if (s.connected) t = 'Conectado a «' + s.ssid + '» · ' + s.ip;
  else if (s.attempting) t = 'Conectando a «' + s.target + '»…';
  else t = 'Sin conexión al router';
  if (s.ap) t += '\nRed propia «' + s.apSsid + '» activa (' + s.apIp + ')';
  $('status').style.whiteSpace = 'pre-line';
  $('status').textContent = t;
  $('saved').textContent = s.saved ? 'Red guardada: «' + s.saved + '»' : '';
  $('forget').style.display = s.saved ? 'inline-block' : 'none';
  return s;
}

async function refreshStatus(){
  try { return showStatus(await get('/api/wifi/status')); } catch(e) { return null; }
}

async function scan(){
  $('nets').innerHTML = '<div class="hint">Buscando redes…</div>';
  for (let i = 0; i < 20; i++){
    let r;
    try { r = await get('/api/wifi/scan'); } catch(e) { break; }
    if (!r.scanning){
      const seen = new Set();
      const nets = r.networks.sort((a, b) => b.rssi - a.rssi)
        .filter(n => n.ssid && !seen.has(n.ssid) && seen.add(n.ssid));
      $('nets').innerHTML = '';
      if (!nets.length) $('nets').innerHTML = '<div class="hint">No se encontraron redes.</div>';
      nets.forEach(n => {
        const b = document.createElement('button');
        b.type = 'button'; b.className = 'net';
        const name = document.createElement('span'); name.textContent = n.ssid;
        const info = document.createElement('em');
        info.textContent = (n.open ? 'abierta · ' : '') + (n.rssi > -60 ? 'buena' : n.rssi > -75 ? 'media' : 'débil');
        b.append(name, info);
        b.onclick = () => { $('ssid').value = n.ssid; $('pass').value = ''; $('pass').focus(); };
        $('nets').append(b);
      });
      return;
    }
    await new Promise(r => setTimeout(r, 1000));
  }
  $('nets').innerHTML = '<div class="hint">No se pudo buscar redes. Escribe el nombre a mano.</div>';
}

async function follow(){
  let s;
  try { s = showStatus(await get('/api/wifi/status')); lost = 0; }
  catch(e){
    // Al conectarse al router la red propia cambia de canal y el móvil puede desconectarse un momento
    if (++lost >= 4){
      stopWatch();
      msg('Se perdió el contacto con Vento. Si la luz roja queda fija, se conectó: vuelve a tu red WiFi y abre http://vento.local', '');
      $('go').disabled = false;
    }
    return;
  }
  if (s.connected && s.ssid === target){
    stopWatch();
    msg('¡Conectado! Vuelve a tu red WiFi y abre http://vento.local (o http://' + s.ip + '). La red «' + s.apSsid + '» se apagará en unos segundos.', 'ok');
    $('go').disabled = false;
  } else if (s.failed === target){
    stopWatch();
    msg('No se pudo conectar a «' + target + '». Revisa la contraseña e inténtalo de nuevo.', 'err');
    $('go').disabled = false;
  }
}

$('form').onsubmit = async e => {
  e.preventDefault();
  const ssid = $('ssid').value, pass = $('pass').value;
  if (pass && pass.length < 8){ msg('La contraseña WiFi debe tener al menos 8 caracteres.', 'err'); return; }
  $('go').disabled = true;
  try {
    await post('/api/wifi', {ssid, pass});
  } catch(err){
    msg('Error: ' + err.message, 'err'); $('go').disabled = false; return;
  }
  target = ssid; lost = 0;
  msg('Conectando a «' + ssid + '»…', '');
  stopWatch(); watch = setInterval(follow, 1500);
};

$('show').onclick = () => {
  const p = $('pass'); const hide = p.type === 'text';
  p.type = hide ? 'password' : 'text'; $('show').textContent = hide ? 'Mostrar' : 'Ocultar';
};

$('rescan').onclick = scan;

$('forget').onclick = async () => {
  if (!confirm('¿Olvidar la red guardada? Se usará la de secrets.h.')) return;
  try { await post('/api/wifi/forget'); } catch(e) {}
  refreshStatus();
};

refreshStatus();
scan();
setInterval(() => { if (!watch) refreshStatus(); }, 5000);
</script>
</body>
</html>
)html";
