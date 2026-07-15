#pragma once

// Phone-facing single-page UI, served from flash. Talks to /api/status and
// /api/plug. Kept dependency-free so the device stays fully offline-capable.
static const char WEB_UI_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>室内环境</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; margin: 0; }
  body {
    font-family: -apple-system, "PingFang SC", "Noto Sans SC", sans-serif;
    background: #101418; color: #e8edf2; min-height: 100vh;
    display: flex; flex-direction: column; align-items: center;
    padding: 24px 16px calc(24px + env(safe-area-inset-bottom));
  }
  h1 { font-size: 15px; font-weight: 500; color: #8b98a5; letter-spacing: .2em; margin-bottom: 20px; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; width: 100%; max-width: 420px; }
  .card { background: #1a2129; border-radius: 16px; padding: 18px 16px; }
  .card .label { font-size: 13px; color: #8b98a5; margin-bottom: 6px; }
  .card .value { font-size: 34px; font-weight: 600; font-variant-numeric: tabular-nums; }
  .card .unit { font-size: 16px; color: #8b98a5; margin-left: 2px; }
  .wide { grid-column: 1 / -1; }
  .stale .value { color: #5a6672; }
  .row { display: flex; justify-content: space-between; align-items: center; }
  .sub { font-size: 13px; color: #8b98a5; margin-top: 8px; }
  #acbtn {
    width: 100%; margin-top: 14px; padding: 16px; border: 0; border-radius: 12px;
    font-size: 17px; font-weight: 600; color: #fff; background: #2b3644;
    transition: background .2s; -webkit-tap-highlight-color: transparent;
  }
  #acbtn.on { background: #0a84ff; }
  #acbtn:disabled { opacity: .5; }
  .acbtns { display: flex; gap: 8px; margin-top: 12px; }
  .acbtns button, .chip {
    flex: 1; padding: 12px 6px; border: 0; border-radius: 10px; font-size: 15px;
    font-weight: 600; color: #e8edf2; background: #2b3644;
    -webkit-tap-highlight-color: transparent;
  }
  .chip.sel { background: #0a84ff; color: #fff; }
  .chiprow { display: flex; gap: 6px; margin-top: 8px; flex-wrap: wrap; }
  .chip { font-size: 13px; padding: 9px 4px; }
  .tempctl { display: flex; align-items: center; justify-content: center; gap: 18px; margin-top: 12px; }
  .tempctl button { width: 48px; height: 48px; border: 0; border-radius: 50%; background: #2b3644; color: #fff; font-size: 24px; }
  .tempval { font-size: 30px; font-weight: 600; font-variant-numeric: tabular-nums; min-width: 78px; text-align: center; }
  .tempval .u { font-size: 15px; color: #8b98a5; }
  #status { font-size: 12px; color: #5a6672; margin-top: 18px; }
  .dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; background: #5a6672; margin-right: 6px; }
  .dot.ok { background: #30d158; }
</style>
</head>
<body>
<h1>室内环境</h1>
<div class="grid">
  <div class="card" id="tempcard"><div class="label">温度</div>
    <div><span class="value" id="temp">--.-</span><span class="unit">°C</span></div></div>
  <div class="card" id="humcard"><div class="label">湿度</div>
    <div><span class="value" id="hum">--</span><span class="unit">%</span></div></div>
  <div class="card wide">
    <div class="row">
      <div><div class="label">空调插座</div>
        <div><span class="value" id="plug" style="font-size:24px">--</span></div></div>
      <div style="text-align:right"><div class="label">当前功率</div>
        <div><span class="value" id="power" style="font-size:24px">--</span><span class="unit">W</span></div></div>
    </div>
    <div class="sub"><span id="kwh">--</span> kWh 今日 · 运行 <span id="runtime">--</span></div>
    <button id="acbtn" disabled>...</button>
  </div>
  <div class="card wide">
    <div class="row">
      <div class="label">空调（红外遥控）</div>
      <div class="sub" id="acstate" style="margin-top:0">--</div>
    </div>
    <div class="acbtns">
      <button id="acon" onclick="acOn()">开 (冷房)</button>
      <button id="acoff" onclick="ac({power:0})">关</button>
    </div>
    <div class="tempctl">
      <button onclick="acTemp(-1)">−</button>
      <div class="tempval"><span id="actemp">24</span><span class="u">°C</span></div>
      <button onclick="acTemp(1)">+</button>
    </div>
    <div class="chiprow" id="modes"></div>
    <div class="chiprow" id="fans"></div>
    <div class="chiprow">
      <button class="chip" id="swingchip" onclick="acToggle('swing')">上下扫风</button>
      <button class="chip" id="streamerchip" onclick="acToggle('streamer')">Streamer</button>
    </div>
  </div>
</div>
<div id="status"><span class="dot" id="dot"></span><span id="statustext">连接中…</span></div>
<script>
let plugOn = null, busy = false;
const $ = id => document.getElementById(id);

function fmtRuntime(min) {
  if (min < 0 || min == null) return '--';
  return min >= 60 ? Math.floor(min / 60) + ' 小时 ' + (min % 60) + ' 分' : min + ' 分';
}

async function poll() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    $('temp').textContent = s.tempC == null ? '--.-' : s.tempC.toFixed(1);
    $('hum').textContent = s.humPct == null ? '--' : Math.round(s.humPct);
    const stale = s.sensorAgeSec == null || s.sensorAgeSec > 300;
    $('tempcard').classList.toggle('stale', stale);
    $('humcard').classList.toggle('stale', stale);
    if (s.plugKnown) {
      plugOn = s.plugOn;
      $('plug').textContent = plugOn ? '开' : '关';
      $('plug').style.color = plugOn ? '#0a84ff' : '#8b98a5';
      $('power').textContent = s.powerW == null ? '--' : s.powerW.toFixed(s.powerW < 10 ? 1 : 0);
      $('kwh').textContent = s.todayKWh == null ? '--' : s.todayKWh.toFixed(2);
      $('runtime').textContent = fmtRuntime(s.todayRuntimeMin);
      if (!busy) {
        const b = $('acbtn');
        b.disabled = false;
        b.className = plugOn ? 'on' : '';
        b.textContent = plugOn ? '关闭空调' : '打开空调';
      }
    } else {
      $('acbtn').disabled = true;
      $('acbtn').textContent = '插座离线';
    }
    if (s.acKnown) {
      acTempVal = s.acTemp; $('actemp').textContent = s.acTemp;
      $('acstate').textContent = '最后发送: ' + (s.acOn ? '开' : '关') + ' · ' + s.acMode + ' · ' + s.acTemp + '°C · 风' + s.acFan + (s.acSwing ? ' · 扫风' : '') + (s.acStreamer ? ' · Streamer' : '');
      MODES.forEach(([k]) => $('mode-' + k) && $('mode-' + k).classList.toggle('sel', s.acOn && s.acMode === k));
      FANS.forEach(([k]) => $('fan-' + k) && $('fan-' + k).classList.toggle('sel', s.acFan === k));
      $('swingchip').classList.toggle('sel', s.acSwing);
      $('streamerchip').classList.toggle('sel', s.acStreamer);
    } else {
      $('acstate').textContent = '尚未发送过指令';
    }
    $('dot').className = 'dot ok';
    $('statustext').textContent = (s.host || 'm5stick') + '.local · 信号 ' + s.rssi + ' dBm';
  } catch (e) {
    $('dot').className = 'dot';
    $('statustext').textContent = '连接断开，重试中…';
  }
}

$('acbtn').addEventListener('click', async () => {
  if (plugOn == null || busy) return;
  busy = true;
  const b = $('acbtn');
  b.disabled = true;
  b.textContent = plugOn ? '正在关闭…' : '正在打开…';
  try { await fetch('/api/plug?on=' + (plugOn ? 0 : 1), { method: 'POST' }); } catch (e) {}
  setTimeout(() => { busy = false; poll(); }, 2500);
});

// --- AC (IR) control ---
const MODES = [['auto', '自动'], ['cool', '冷房'], ['heat', '暖房'], ['dry', '除湿'], ['fan', '送风']];
const FANS = [['auto', '自动'], ['quiet', '静音'], ['low', '弱'], ['medium', '中'], ['high', '强']];
let acTempVal = 24;

function buildChips() {
  const mk = (parent, list, pfx, onclick) => {
    const el = $(parent); el.innerHTML = '';
    list.forEach(([k, label]) => {
      const b = document.createElement('button');
      b.className = 'chip'; b.id = pfx + k; b.textContent = label;
      b.onclick = () => onclick(k);
      el.appendChild(b);
    });
  };
  mk('modes', MODES, 'mode-', k => ac({ power: 1, mode: k }));
  mk('fans', FANS, 'fan-', k => ac({ fan: k }));
}

function ac(params) {
  const q = Object.entries(params).map(([k, v]) => k + '=' + encodeURIComponent(v)).join('&');
  return fetch('/api/ac?' + q, { method: 'POST' }).catch(() => {}).then(() => setTimeout(poll, 700));
}
// Summer default: turn on in COOL at the chosen temp with vertical swing.
function acOn() { ac({ power: 1, mode: 'cool', temp: acTempVal, swing: 1 }); }
function acTemp(d) {
  acTempVal = Math.max(16, Math.min(30, acTempVal + d));
  $('actemp').textContent = acTempVal;
  ac({ temp: acTempVal });
}
let acToggleState = { swing: false, streamer: false };
function acToggle(which) {
  acToggleState[which] = !$( which === 'swing' ? 'swingchip' : 'streamerchip').classList.contains('sel');
  ac({ [which]: acToggleState[which] ? 1 : 0 });
}

buildChips();
poll();
setInterval(() => { if (!busy) poll(); }, 5000);
</script>
</body>
</html>
)HTML";
