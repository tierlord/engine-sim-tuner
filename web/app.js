// Engine-Sim-Tuner user interface: talks to engine-sim-server over HTTP (same origin).
//   GET /events      live values (Server-Sent Events), an open stream tells the server that this page is alive
//   GET/POST /tune   values that change while the engine runs
//   POST /control    driver input (gas, starter, ignition)

import { $, nums, api, soon } from './util.js';
import { initBuild } from './build.js';
import { initDyno } from './dyno.js';

/* ------------------------------------------------------------------ views */

let buildView = 'b-layout';

function show(view) {                                   // build pages only; the tuning cockpit is one screen
    document.querySelectorAll('main section').forEach(sec => sec.classList.remove('act'));
    buildView = view;
    $('v-' + view).classList.add('act');
    document.querySelectorAll('nav a').forEach(a => a.classList.toggle('act', a.dataset.v === view));
}

function setMode(mode) {
    document.body.dataset.mode = mode;
    $('m-build').classList.toggle('act', mode === 'build');
    $('m-tune').classList.toggle('act', mode === 'tune');
    if (mode === 'build') { show(buildView); if (build) build.updateStale(); }
}

$('m-build').onclick = () => setMode('build');
$('m-tune').onclick = () => setMode('tune');
document.querySelectorAll('nav a').forEach(a => a.onclick = () => show(a.dataset.v));

/* ------------------------------------------------------------------ live values */

let last = null;
const fmt = (v, d = 0) => Number.isFinite(v) ? v.toLocaleString('de-DE', { maximumFractionDigits: d, minimumFractionDigits: d }) : '–';

// Smoothing of the readouts: the raw values swing with every engine cycle. Exponential filter with a time
// constant chosen in the header; the numbers on screen refresh at ~5 Hz, the operating dot moves smoothly.
const FILTER_KEYS = ['rpm', 'map_kpa', 'advance_deg', 'torque_nm', 'power_kw', 'boost_bar', 'turbo_rpm', 'wastegate', 'lambda', 'backpressure'];
const FILTER_TAU = { off: 0, light: 0.25, medium: 0.6, strong: 1.2 };
let filterMode = 'medium';
try { filterMode = localStorage.getItem('engineTunerFilter') || 'medium'; } catch (e) { /* ignore */ }
$('filter').value = filterMode;
$('filter').onchange = () => { filterMode = $('filter').value; try { localStorage.setItem('engineTunerFilter', filterMode); } catch (e) { /* ignore */ } };
let filtered = null, filteredAt = 0, shownAt = 0;

function smoothed(d) {
    const now = performance.now();
    const tau = FILTER_TAU[filterMode] ?? 0.6;
    if (!filtered || !d.engine || tau === 0) filtered = { ...d };
    else {
        const k = 1 - Math.exp(-(now - filteredAt) / 1000 / tau);
        filtered = { ...d };
        for (const key of FILTER_KEYS) filtered[key] = (smoothedPrev[key] ?? d[key]) + (d[key] - (smoothedPrev[key] ?? d[key])) * k;
    }
    filteredAt = now;
    smoothedPrev = filtered;
    return filtered;
}
let smoothedPrev = {};

function onTelemetry(d) {
    last = d;
    if (typeof dyno !== 'undefined') dyno.onTelemetry(d);
    d = smoothed(d);
    if (d.engine && filterMode !== 'off') {
        placeDot(d.rpm, d.map_kpa);
        const now = performance.now();
        if (now - shownAt < 200) return;
        shownAt = now;
    }
    $('engineName').textContent = d.engine ? `${d.name} · ${fmt(d.octane)} ROZ` : 'kein Motor';
    if (!d.engine) return;

    $('t-rpm').textContent = fmt(d.rpm);
    $('t-redline').textContent = `U/min · Redline ${fmt(d.redline)}`;
    $('t-rpmbar').style.width = Math.min(100, d.rpm / d.redline * 100) + '%';
    $('t-map').textContent = fmt(d.map_kpa);
    $('t-adv').textContent = fmt(d.advance_deg);
    $('t-nm').textContent = fmt(d.torque_nm);
    $('t-ps').textContent = d.torque_nm !== 0 ? `Nm · ${fmt(d.power_kw * 1.35962)} PS` : 'Nm';
    $('tb-line').textContent = d.turbo ? `${d.boost_bar.toFixed(2)} bar · ${fmt(d.turbo_rpm / 1000)}k/min · WG ${fmt(d.wastegate * 100)} %` : '';
    $('tb-state').innerHTML = `<span>Ladedruck</span><b>${d.boost_bar.toFixed(2)} bar</b><span>Soll</span><b>${d.boost_target.toFixed(2)} bar</b><span>Laderdrehzahl</span><b>${fmt(d.turbo_rpm)} /min</b>`
        + `<span>Wastegate</span><b>${fmt(d.wastegate * 100)} %</b><span>Abgasgegendruck</span><b>${d.backpressure.toFixed(2)} ×</b><span>Schubumluft</span><b>${d.blowoff ? 'offen' : 'zu'}</b>`;
    if (document.activeElement !== $('tb-boost') && document.activeElement !== $('tb-boost-r')) $('q-boost-note').textContent = MAPS.boost.on ? `Kennfeld aktiv · gerade ${d.boost_target.toFixed(2)} bar` : 'Wastegate öffnet hier';
    $('t-lambda').textContent = d.lambda.toFixed(2);
    $('t-temp').textContent = fmt(d.engine_temp);
    $('t-temp-box').className = 'v ' + (d.engine_temp < 60 ? 'warn' : 'ok');
    $('t-knock').textContent = fmt(d.knock * 100);
    $('t-knock-box').className = 'v ' + (d.knock >= 0.1 ? 'warn' : 'ok');
    $('t-dmg').textContent = fmt(d.damage * 100);
    $('t-dmg-box').className = 'v ' + (d.destroyed ? 'err' : d.damage > 0.05 ? 'warn' : 'ok');
    $('t-cam').textContent = d.high_lift ? 'hoch' : 'niedrig';
    $('t-cam').className = 'v ' + (d.high_lift ? 'ok' : 'warn');

    $('ign').className = 'only-tune ' + (d.ignition ? 'on' : '');
    $('ign').textContent = d.ignition ? 'Zündung an' : 'Zündung aus';
    $('starter').className = 'only-tune ' + (d.starter ? 'on' : '');

    placeDot(d.rpm, d.map_kpa);
}

function connect() {
    const events = new EventSource('/events');
    const chip = $('conn');
    events.onopen = () => { chip.className = 'chip'; chip.querySelector('span').textContent = 'Simulator verbunden'; loadTune(); };
    events.onmessage = e => { try { onTelemetry(JSON.parse(e.data)); } catch (err) { /* ignore a broken frame */ } };
    events.onerror = () => { chip.className = 'chip stale'; chip.querySelector('span').textContent = 'getrennt – verbinde neu …'; };
}

/* ------------------------------------------------------------------ driver input */

$('gas').oninput = () => {
    const v = +$('gas').value;
    $('gasv').textContent = v + ' %';
    soon('gas', () => api.post('/control', `throttle=${v / 100}`), 30);
};

const starter = on => api.post('/control', `starter=${on ? 1 : 0}`);
$('starter').addEventListener('pointerdown', e => { $('starter').setPointerCapture(e.pointerId); starter(true); });
['pointerup', 'pointercancel', 'lostpointercapture'].forEach(ev => $('starter').addEventListener(ev, () => starter(false)));
window.addEventListener('blur', () => starter(false));
$('cold').onclick = () => api.post('/tune', 'engine_temp 25');
$('warm').onclick = () => api.post('/tune', 'engine_temp 90');
$('ign').onclick = () => api.post('/control', `ignition=${last && last.ignition ? 0 : 1}`);

/* ------------------------------------------------------------------ tuning state (server is the source of truth) */

let tune = {};

// The tables: ignition (speed x load), lambda (speed x load) and boost target (speed only).
// vals[speedIndex * load.length + loadIndex]; keys are the names used with POST /tune.
const MAPS = {
    ign: { keys: ['map_rpm', 'map_load', 'map_adv'], off: 'map_off 1', dec: 1, step: 0.5, stepDefault: 1, unit: '°', title: 'Zündwinkel in ° vor OT', rowDelta: -1,
        color: v => v < 0 ? '#b3272a' : `hsl(${210 - Math.min(1, v / 50) * 170}, 55%, ${24 + Math.min(1, v / 50) * 14}%)` },
    lambda: { keys: ['lambda_map_rpm', 'lambda_map_load', 'lambda_map_val'], off: 'lambda_map_off 1', dec: 2, step: 0.01, stepDefault: 0.02, unit: '', title: 'Soll-Lambda · kleiner = fetter', rowDelta: -0.03,
        color: v => `hsl(${210 - Math.min(1, Math.max(0, (v - 0.7) / 0.5)) * 210}, 55%, 28%)` },
    boost: { keys: ['boost_map_rpm', null, 'boost_map_val'], off: 'boost_map_off 1', dec: 2, step: 0.05, stepDefault: 0.1, unit: ' bar', title: 'Ladedruck-Soll in bar', single: true,
        color: v => `hsl(${210 - Math.min(1, Math.max(0, v / 2)) * 190}, 55%, 28%)` },
};
Object.values(MAPS).forEach(m => Object.assign(m, { rpm: [], load: [], vals: [], on: false }));
let map = MAPS.ign;                                     // the table that is shown

async function loadTune() {
    try { tune = (await api.get('/tune')).values || {}; } catch (e) { tune = {}; }
    const curve = (tune.timing_curve || '').trim().split(/\s+/).map(p => p.split(':').map(Number)).filter(p => p.length === 2 && !p.some(isNaN));
    setField('c-idle', curve.length ? curve[0][1] : 12);
    setField('c-max', curve.length ? curve[curve.length - 1][1] : 34);
    setField('c-rev', tune.rev_limit_rpm !== undefined ? Number(tune.rev_limit_rpm) : 8000);
    setField('c-idlethr', tune.idle_throttle_plate_position !== undefined ? Number(tune.idle_throttle_plate_position) : 0.99);
    const cam = (tune.cam_center || '0 0').trim().split(/\s+/).map(Number);
    const tb = (tune.turbo || '0 0.8 0.35 1 0.7 1').trim().split(/\s+/).map(Number);
    $('tb-on').checked = tb[0] > 0.5;
    setField('tb-boost', tb[1] ?? 0.8);
    setField('tb-size', tb[2] ?? 0.35);
    setField('tb-inertia', tb[3] ?? 1);
    setField('tb-ic', tb[4] ?? 0.7);
    setField('tb-vol', tb[5] ?? 1);
    setField('c-lambda', tune.lambda !== undefined ? Number(tune.lambda) : 0.8);
    setField('cam-in', cam[0] || 0);
    setField('cam-ex', cam[1] || 0);
    if (tune.fuel_octane !== undefined) $('q-fuel').value = String(Math.round(Number(tune.fuel_octane)));

    readMaps();
    renderMap();
    updateMapState();
}

function setField(id, value) { $(id).value = value; $(id + '-r').value = value; }

function linkField(id, onChange) {
    const number = $(id), range = $(id + '-r');
    number.oninput = () => { range.value = number.value; onChange(+number.value); };
    range.oninput = () => { number.value = range.value; onChange(+range.value); };
}

function curveText() {
    const idle = +$('c-idle').value, max = +$('c-max').value;
    const points = [];
    for (let r = 0; r <= 8000; r += 1000) points.push(`${r}:${(idle + (max - idle) * Math.min(1, r / 4000)).toFixed(2)}`);
    return points.join(' ');
}

linkField('c-idle', () => soon('curve', () => api.post('/tune', 'timing_curve ' + curveText())));
linkField('c-max', () => soon('curve', () => api.post('/tune', 'timing_curve ' + curveText())));
linkField('c-rev', v => soon('rev', () => api.post('/tune', 'rev_limit_rpm ' + v)));
linkField('c-idlethr', v => soon('idlethr', () => api.post('/tune', 'idle_throttle_plate_position ' + v)));
const turboText = () => `turbo ${$('tb-on').checked ? 1 : 0} ${$('tb-boost').value} ${$('tb-size').value} ${$('tb-inertia').value} ${$('tb-ic').value} ${$('tb-vol').value}`;
const sendTurbo = () => soon('turbo', () => api.post('/tune', turboText()), 120);
$('tb-on').onchange = sendTurbo;
['tb-boost', 'tb-size', 'tb-inertia', 'tb-ic', 'tb-vol'].forEach(id => linkField(id, sendTurbo));
linkField('c-lambda', v => soon('lambda', () => api.post('/tune', 'lambda ' + v)));
$('repair').onclick = () => api.post('/tune', 'repair 1');
$('q-fuel').innerHTML = [91, 95, 98, 100, 105].map(r => `<option value="${r}">ROZ ${r}</option>`).join('');
$('q-fuel').onchange = () => api.post('/tune', 'fuel_octane ' + $('q-fuel').value);
linkField('cam-in', () => soon('cam', () => api.post('/tune', `cam_center ${$('cam-in').value} ${$('cam-ex').value}`), 120));
linkField('cam-ex', () => soon('cam', () => api.post('/tune', `cam_center ${$('cam-in').value} ${$('cam-ex').value}`), 120));

/* ------------------------------------------------------------------ tables */

const round = (v, dec) => Math.round(v * 10 ** dec) / 10 ** dec;

function readMaps() {
    const idle = +$('c-idle').value || 12, max = +$('c-max').value || 34;
    const base = r => idle + (max - idle) * Math.min(1, r / 4000);
    const rpm = [500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000], load = [20, 40, 60, 80, 100];

    for (const [name, m] of Object.entries(MAPS)) {
        const [kr, kl, kv] = m.keys;
        const r = nums(tune[kr] || ''), v = nums(tune[kv] || '');
        const l = m.single ? [0] : nums(tune[kl] || '');
        if (r.length && l.length && v.length === r.length * l.length) {
            Object.assign(m, { rpm: r, load: l, vals: v, on: true });
        } else {                                       // no table in the engine yet: start from the single values, switched off
            m.rpm = rpm; m.load = m.single ? [0] : load; m.on = false;
            if (name === 'ign') m.vals = rpm.flatMap(x => load.map(y => round(base(x) + (100 - y) * 0.25, 1)));
            else if (name === 'lambda') m.vals = rpm.flatMap(() => load.map(() => +$('c-lambda').value || 0.8));
            else m.vals = rpm.map(() => +$('tb-boost').value || 0.8);
        }
    }
}

function updateMapState() {
    $('map-on').checked = map.on;
    $('curve-card').style.opacity = MAPS.ign.on ? 0.5 : 1;
    $('c-lambda').closest('.field').classList.toggle('off', MAPS.lambda.on);
    $('q-lambda-note').textContent = MAPS.lambda.on ? 'Kennfeld aktiv – dieser Wert wird nicht verwendet' : 'gilt, wenn das Lambda-Kennfeld aus ist';
    $('tb-boost').closest('.field').classList.toggle('off', MAPS.boost.on);
}

function setMap(name) {
    map = MAPS[name];
    document.querySelectorAll('#map-tabs span').forEach(t => t.classList.toggle('act', t.dataset.map === name));
    $('map-title').textContent = map.title;
    $('sel-step').value = map.stepDefault; $('sel-step').step = map.step; $('sel-step').min = map.step;
    $('sel-unit').textContent = map.unit.trim();
    $('axis-ctl').style.visibility = map.single ? 'hidden' : 'visible';
    renderMap();
    updateMapState();
}
document.querySelectorAll('#map-tabs span').forEach(t => { t.onclick = () => setMap(t.dataset.map); });

function renderMap() {
    const grid = $('map-grid');
    const columns = map.rpm.length, rows = map.load.length;
    const top = map.load[rows - 1];
    $('row-info').textContent = map.single ? '' : `bis ${top} kPa (${((top - 101.3) / 100).toFixed(2)} bar Ladedruck)`;
    grid.style.gridTemplateColumns = `56px repeat(${columns}, 1fr)`;
    grid.style.gridTemplateRows = `repeat(${rows}, minmax(30px, 1fr)) 22px`;
    grid.innerHTML = '';

    // rows: load from high (top) to low (bottom); columns: speed from low to high
    for (let j = rows - 1; j >= 0; j--) {
        const label = document.createElement('div');
        label.className = 'lab'; label.textContent = map.single ? 'Soll' : map.load[j] + ' kPa';
        grid.appendChild(label);

        for (let i = 0; i < columns; i++) {
            const cell = document.createElement('input');
            cell.type = 'number'; cell.step = String(map.step);
            cell.value = map.vals[i * rows + j];
            cell.style.background = map.color(+cell.value);
            cell.dataset.i = i; cell.dataset.j = j;
            const index = i * rows + j;
            cell.oninput = () => {
                if (cell.value === '') return;            // an empty field keeps the last value until ENTER decides
                map.vals[index] = +cell.value;
                cell.style.background = map.color(+cell.value);
                if (map.on) soon('map', () => pushMap(map), 150);
            };

            // The whole value is selected on entry, so the first keystroke replaces it
            cell.onfocus = () => cell.select();
            // The click is taken over: a plain click picks one cell, SHIFT or dragging picks a range
            cell.onmousedown = e => {
                if (e.button !== 0) return;
                e.preventDefault();
                dragging = true;
                if (e.shiftKey && selection) selection.end = [i, j];
                else selection = { anchor: [i, j], end: [i, j] };
                cell.focus();
                cell.select();
                showSelection();
            };
            cell.onmouseenter = () => {
                if (!dragging || !selection) return;
                selection.end = [i, j];
                showSelection();
            };
            cell.onkeydown = e => {
                if (e.key === 'Enter') { e.preventDefault(); commitCell(cell, index); return; }
                if (e.key === 'PageUp' || e.key === 'PageDown') {
                    e.preventDefault();
                    adjustSelection((e.key === 'PageUp' ? 1 : -1) * selectionStep());
                    return;
                }
                const step = ARROW_STEPS[e.key];
                if (step) {
                    e.preventDefault();
                    const end = [i + step[0], j + step[1]];
                    if (!cellAt(end[0], end[1])) return;           // outside the map: stay where we are
                    selection = { anchor: e.shiftKey && selection ? selection.anchor : end, end };
                    cellAt(end[0], end[1]).focus();
                    showSelection();
                }
            };
            grid.appendChild(cell);
        }
    }

    selection = null;
    showSelection();

    grid.appendChild(document.createElement('div'));
    map.rpm.forEach(r => {
        const l = document.createElement('div');
        l.className = 'lab col'; l.textContent = r; l.title = `Prüfstand „Halten“ auf ${r} U/min setzen`;
        l.onclick = () => { $('h-rpm').value = r; $('h-rpm').dispatchEvent(new Event('input')); };
        grid.appendChild(l);
    });
}

// Arrow keys move through the map: [column (speed), row (load)]. Rows are drawn with high load on top.
const ARROW_STEPS = { ArrowUp: [0, 1], ArrowDown: [0, -1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] };

const cellAt = (i, j) => document.querySelector(`#map-grid input[data-i="${i}"][data-j="${j}"]`);
const cellIndex = c => +c.dataset.i * map.load.length + +c.dataset.j;

// Range selection: a rectangle between the anchor and the end cell, both given as [column, row]
let selection = null;
let dragging = false;
document.addEventListener('mouseup', () => { dragging = false; });

function selectedCells() {
    if (!selection) return [];
    const [i0, i1] = [selection.anchor[0], selection.end[0]].sort((a, b) => a - b);
    const [j0, j1] = [selection.anchor[1], selection.end[1]].sort((a, b) => a - b);
    const cells = [];
    for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) { const c = cellAt(i, j); if (c) cells.push(c); }
    return cells;
}

function showSelection() {
    const chosen = new Set(selectedCells());
    document.querySelectorAll('#map-grid input').forEach(c => c.classList.toggle('sel', chosen.size > 1 && chosen.has(c)));
    $('sel-info').textContent = chosen.size > 1 ? `${chosen.size} Zellen markiert` : 'eine Zelle';
}

const selectionStep = () => Math.abs(+$('sel-step').value) || map.step;

function setCellValue(cell, value) {
    const v = round(value, map.dec);
    map.vals[cellIndex(cell)] = v;
    cell.value = v;
    cell.style.background = map.color(v);
}

// raise or lower the marked cells (or the single focused one) by the same amount
function adjustSelection(delta) {
    const cells = selectedCells();
    if (!cells.length && document.activeElement && document.activeElement.dataset.i !== undefined) cells.push(document.activeElement);
    cells.forEach(c => setCellValue(c, map.vals[cellIndex(c)] + delta));
    // setting a value drops the text selection, so the next typed number would be appended
    const active = document.activeElement;
    if (active && active.dataset && active.dataset.i !== undefined) active.select();
    if (map.on) soon('map', () => pushMap(map), 150);
}

// ENTER accepts the typed value at once (for every marked cell) and sends it to the engine
function commitCell(cell, index) {
    const value = parseFloat(cell.value);
    if (Number.isNaN(value)) cell.value = map.vals[index];   // empty or not a number: keep the old value
    else {
        const cells = selectedCells();
        (cells.length > 1 ? cells : [cell]).forEach(c => setCellValue(c, value));
    }
    if (map.on) pushMap(map);
    cell.select();
}

// The load axis can grow beyond the ambient pressure for boosted engines: a new top row is a copy of the old one,
// a little leaner/later or richer depending on the table (more charge pressure wants less advance and more fuel)
function resizeLoadAxis(add) {
    const rows = map.load.length;
    if (map.single || (add ? rows >= 20 : rows <= 3)) return;
    const newRows = add ? rows + 1 : rows - 1;
    const vals = [];
    for (let i = 0; i < map.rpm.length; i++) {
        for (let j = 0; j < newRows; j++) {
            vals.push(j < rows ? map.vals[i * rows + j] : round(map.vals[i * rows + rows - 1] + map.rowDelta, map.dec));
        }
    }
    map.vals = vals;
    if (add) map.load.push(map.load[rows - 1] + 20); else map.load.pop();
    renderMap();
    if (map.on) pushMap(map);
}
$('row-add').onclick = () => resizeLoadAxis(true);
$('row-del').onclick = () => resizeLoadAxis(false);

function pushMap(m) {
    const [kr, kl, kv] = m.keys;
    api.post('/tune', `${kr} ${m.rpm.join(' ')}\n` + (kl ? `${kl} ${m.load.join(' ')}\n` : '') + `${kv} ${m.vals.join(' ')}`);
}

$('sel-up').onclick = () => adjustSelection(selectionStep());
$('sel-dn').onclick = () => adjustSelection(-selectionStep());

$('map-on').onchange = () => {
    map.on = $('map-on').checked;
    if (map.on) pushMap(map); else api.post('/tune', map.off);
    updateMapState();
};

// position of the white dot: interpolate between the centres of the neighbouring cells
function fraction(axis, v) {
    if (v <= axis[0]) return [0, 0, 0];
    if (v >= axis[axis.length - 1]) return [axis.length - 1, axis.length - 1, 0];
    let k = 1; while (axis[k] < v) k++;
    return [k - 1, k, (v - axis[k - 1]) / (axis[k] - axis[k - 1])];
}

function placeDot(rpm, kpa) {
    const dot = $('opdot');
    if (!map.rpm.length || document.body.dataset.mode !== 'tune') { dot.classList.add('hidden'); return; }

    const [i0, i1, ti] = fraction(map.rpm, rpm), [j0, j1, tj] = fraction(map.load, kpa);
    const cell = (i, j) => $('map-grid').querySelector(`input[data-i="${i}"][data-j="${j}"]`);
    const a = cell(i0, j0), b = cell(i1, j1);
    if (!a || !b) return;

    const cx = (c) => c.offsetLeft + c.offsetWidth / 2, cy = (c) => c.offsetTop + c.offsetHeight / 2;
    const x = cx(a) + (cx(cell(i1, j0)) - cx(a)) * ti, y = cy(a) + (cy(cell(i0, j1)) - cy(a)) * tj;
    dot.style.left = x + 'px'; dot.style.top = y + 'px';
    dot.classList.remove('hidden');
}

/* ------------------------------------------------------------------ start */

const build = initBuild({
    hasEngine: () => !!(last && last.engine),
    onLoaded: () => { setMode('tune'); loadTune(); },      // after loading: tuning, the engine stands still
});

// a first start without an engine opens the build view
const dyno = initDyno({
    getSnapshot: () => build.snapshot(),
    getTune: async () => { try { return (await api.get('/tune')).values || {}; } catch (e) { return {}; } },
});

setMode('tune');
setMap('ign');
connect();
setTimeout(() => { if (!(last && last.engine)) setMode('build'); }, 1500);
