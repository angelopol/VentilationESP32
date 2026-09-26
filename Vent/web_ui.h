// Interfaz web (PWA) servida por el ESP32.
#pragma once
#include <Arduino.h>

const char MANIFEST_JSON[] PROGMEM = R"json({
  "name": "Vento",
  "short_name": "Vento",
  "description": "Control del ventilador y los aires acondicionados",
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
.ach{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}
.acname{font-size:20px;font-weight:700;letter-spacing:-.3px}
.pwr{width:52px;height:52px;border-radius:50%;padding:0;display:grid;place-items:center;flex:none}
.pwr svg{width:24px;height:24px}
.acmain{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:12px;margin-bottom:18px}
.stepper{display:flex;align-items:center;gap:8px}
.stepper button{width:48px;height:48px;padding:0;border-radius:50%;font-size:26px;line-height:1}
.stepper b{font-size:30px;min-width:66px;text-align:center}
.stepper small{display:block;text-align:center;font-size:12px;color:var(--muted);font-weight:400}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px}
.chips button{flex:1 1 auto;min-width:70px;padding:12px 8px}
.ac.offline .acbody{opacity:.45}
.acmsg{color:#fca5a5;font-size:13px;margin-top:4px}
.acmsg:empty{display:none}
[hidden]{display:none!important}
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
  <input type="range" id="sp" min="16" max="70" step="1">
  <div class="hint">Se usa en los modos Auto y Progresivo.</div>
</section>

<div id="acs"></div>

<footer><a href="/wifi">Configurar WiFi</a></footer>

<div class="toast" id="toast"></div>

<template id="acTpl">
<section class="card ac">
  <div class="ach">
    <div><div class="label" style="margin:0">Aire acondicionado</div><div class="acname"></div></div>
    <button class="pwr" aria-label="Encender o apagar">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round">
        <path d="M12 3v9"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/></svg>
    </button>
  </div>
  <div class="acbody">
    <div class="acmain">
      <div><div class="big actemp">--°</div><div class="sub acsub"></div></div>
      <div class="stepper">
        <button class="minus" aria-label="Bajar">−</button>
        <div><b class="acsp">--°</b><small>Objetivo</small></div>
        <button class="plus" aria-label="Subir">+</button>
      </div>
    </div>
    <div class="acctl">
      <div class="modew"><div class="label">Modo</div><div class="chips modes"></div></div>
      <div class="fanw"><div class="label">Ventilador</div><div class="chips fans"></div></div>
      <div class="chips toggles"></div>
    </div>
  </div>
  <div class="acmsg"></div>
</section>
</template>

<script>
const $ = id => document.getElementById(id);
const btns = [...document.querySelectorAll('button[data-m]')];
let state = null, dragging = false, toastT = null;

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

function toast(text){
  $('toast').textContent = text || 'No se pudo contactar con el ventilador';
  $('toast').style.display = 'block';
  clearTimeout(toastT);
  toastT = setTimeout(() => $('toast').style.display = 'none', 2500);
}

// El ESP32 atiende una conexión cada vez y tiene poca capacidad: todas las peticiones van
// en fila, de una en una, con límite de tiempo. En cuanto se sabe su IP se usa directamente,
// porque el nombre vento.local (mDNS) a veces tarda o no resuelve.
let base = '', queue = Promise.resolve(), pendingCmds = 0;
const TIMEOUT = 6000;

function api(path, opts){
  const cmd = !!(opts && opts.method === 'POST');
  if (cmd) pendingCmds++;
  const run = async () => {
    try { return await request(path, opts); }
    finally { if (cmd) pendingCmds--; }
  };
  const p = queue.then(run, run);
  queue = p.catch(() => {});
  return p;
}

async function request(path, opts){
  const bases = base ? [base, ''] : [''];
  for (let i = 0; i < bases.length; i++){
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), TIMEOUT);
    try {
      let r;
      try { r = await fetch(bases[i] + path, Object.assign({cache: 'no-store', signal: ctl.signal}, opts)); }
      catch(e){
        if (i + 1 < bases.length){ base = ''; continue; }   // la IP no responde: vuelve al nombre
        throw e;
      }
      if (!r.ok) throw new Error(r.status);
      return await r.json();
    } finally { clearTimeout(t); }
  }
}

// Una sola consulta periódica (ventilador + aires). Si Vento no responde se espera cada vez
// más (hasta 20 s) para no saturarlo mientras se recupera; las órdenes pasan primero.
let pollTimer = null, fails = 0;
function schedule(ms){ clearTimeout(pollTimer); pollTimer = setTimeout(poll, ms); }

async function poll(){
  if (document.hidden) return;
  if (pendingCmds){ schedule(1000); return; }
  const seq = acSeq;
  try {
    const s = await api('/api/state?ac=1');
    if (s.ip && location.hostname !== s.ip) base = 'http://' + s.ip;
    render(s); online(true); fails = 0;
    // Si se mandó una orden mientras esta consulta esperaba, su respuesta ya está vieja
    if (s.acs && Array.isArray(s.acs.devices) && seq === acSeq){ acs = s.acs.devices; renderAc(); }
  }
  catch(e){ online(false); fails++; }
  schedule(fails ? Math.min(2000 * 2 ** fails, 20000) : 2000);
}

async function send(path){
  try { render(await api(path, {method: 'POST'})); online(true); }
  catch(e){ online(false); toast(); }
}

btns.forEach(b => b.addEventListener('click', () => {
  if (state) render(Object.assign({}, state, {mode: +b.dataset.m}));
  send('/api/mode?v=' + b.dataset.m);
}));

const sp = $('sp');
sp.addEventListener('input', () => { dragging = true; $('spv').textContent = sp.value + '°'; });
sp.addEventListener('change', () => { dragging = false; send('/api/setpoint?v=' + sp.value); });

// ---------- Aires acondicionados (/api/ac) ----------
const ICONS = {cold:'❄️', cool:'❄️', hot:'☀️', heat:'☀️', wet:'💧', dry:'💧', dyr:'💧', wind:'💨', fan:'💨', auto:'🔄'};
let acs = [], acEls = [];
let acSeq = 0;   // cambia con cada orden: las consultas que salieron antes se descartan
const spPending = {};   // temperatura elegida con +/- que aún no se envió

function acCard(i){
  const c = $('acTpl').content.firstElementChild.cloneNode(true);
  const q = sel => c.querySelector(sel);
  const el = {c, name: q('.acname'), pwr: q('.pwr'), temp: q('.actemp'), sub: q('.acsub'), sp: q('.acsp'),
    ctl: q('.acctl'), modes: q('.modes'), fans: q('.fans'), modew: q('.modew'), fanw: q('.fanw'),
    toggles: q('.toggles'), msg: q('.acmsg')};
  el.pwr.onclick = () => { const on = !acs[i].power; acSend(i, 'power', on ? 1 : 0, {power: on}); };
  q('.minus').onclick = () => acStep(i, -1);
  q('.plus').onclick = () => acStep(i, 1);
  return el;
}

function chips(box, options, current, icons, pick){
  const sig = JSON.stringify(options);
  if (box.dataset.sig !== sig){
    box.dataset.sig = sig;
    box.innerHTML = '';
    Object.entries(options).forEach(([value, label]) => {
      const b = document.createElement('button');
      b.dataset.v = value;
      const icon = icons && ICONS[value.toLowerCase()];
      b.textContent = (icon ? icon + ' ' : '') + label;
      b.onclick = () => pick(value);
      box.append(b);
    });
  }
  [...box.children].forEach(b => b.classList.toggle('sel', b.dataset.v === current));
}

function renderAc(){
  while (acEls.length < acs.length){ const el = acCard(acEls.length); acEls.push(el); $('acs').append(el.c); }
  while (acEls.length > acs.length) acEls.pop().c.remove();
  acs.forEach((a, i) => {
    const el = acEls[i];
    const digits = a.step < 1 ? 1 : 0;
    el.name.textContent = a.name;
    el.pwr.classList.toggle('sel', a.power);
    el.temp.textContent = a.temp === null ? '--°' : a.temp.toFixed(1) + '°';
    const sp = i in spPending ? spPending[i].v : a.setpoint;
    el.sp.textContent = sp === null ? '--°' : sp.toFixed(digits) + '°';
    const status = !a.polled ? 'Conectando…' : !a.online ? 'Sin conexión'
      : a.power ? (a.modes[a.mode] || 'Encendido') : 'Apagado';
    el.sub.textContent = (a.temp === null ? '' : 'Ambiente · ') + status;
    el.msg.textContent = a.polled && !a.online ? a.error : '';
    el.c.classList.toggle('offline', a.polled && !a.online);
    el.ctl.classList.toggle('dim', !a.power);
    el.modew.hidden = !Object.keys(a.modes).length;
    el.fanw.hidden = !Object.keys(a.fans).length;
    chips(el.modes, a.modes, a.mode, true, v => acSend(i, 'mode', v, {mode: v}));
    chips(el.fans, a.fans, a.fan, false, v => acSend(i, 'fan', v, {fan: v}));
    renderToggles(el.toggles, i, a.toggles || []);
  });
}

// Interruptores extra (oscilación, modo sueño...): cada uno se enciende y apaga por separado
function renderToggles(box, i, toggles){
  const sig = JSON.stringify(toggles.map(t => [t.dp, t.name]));
  if (box.dataset.sig !== sig){
    box.dataset.sig = sig;
    box.innerHTML = '';
    toggles.forEach(t => {
      const b = document.createElement('button');
      b.dataset.dp = t.dp;
      b.textContent = t.name;
      b.onclick = () => {
        const cur = acs[i].toggles.find(x => x.dp === t.dp);
        const on = !(cur && cur.on);
        const next = acs[i].toggles.map(x => x.dp === t.dp ? Object.assign({}, x, {on}) : x);
        acSend(i, 'toggle', t.dp + ':' + (on ? 1 : 0), {toggles: next});
      };
      box.append(b);
    });
  }
  box.hidden = !toggles.length;
  [...box.children].forEach(b => b.classList.toggle('sel', toggles.some(t => t.dp === +b.dataset.dp && t.on)));
}

function acStep(i, dir){
  const a = acs[i];
  let v = i in spPending ? spPending[i].v : (a.setpoint === null ? a.min : a.setpoint);
  v = Math.min(a.max, Math.max(a.min, Math.round((v + dir * a.step) * 10) / 10));
  if (i in spPending) clearTimeout(spPending[i].t);
  // Espera a que se deje de pulsar para mandar una sola orden
  spPending[i] = {v, t: setTimeout(() => { delete spPending[i]; acSend(i, 'temp', v, {setpoint: v}); }, 700)};
  renderAc();
}

async function acSend(i, what, v, patch){
  acSeq++;
  acs[i] = Object.assign({}, acs[i], patch);
  renderAc();
  try {
    const r = await api('/api/ac/' + what + '?d=' + i + '&v=' + encodeURIComponent(v), {method: 'POST'});
    if (Array.isArray(r.devices)) acs = r.devices;
  }
  catch(e){ toast('No se pudo enviar la orden al aire'); }
  renderAc();
}

function start(){ fails = 0; schedule(0); }
document.addEventListener('visibilitychange', () => {
  if (document.hidden) clearTimeout(pollTimer); else start();
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
