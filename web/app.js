// Engine-Sim-Tuner user interface: talks to engine-sim-server over HTTP (same origin).
//   GET /events      live values (Server-Sent Events), an open stream tells the server that this page is alive
//   GET/POST /tune   values that change while the engine runs
//   POST /control    driver input (gas, starter, ignition)

import { $, nums, api, soon } from './util.js';
import { initBuild } from './build.js';
import { initDyno } from './dyno.js';

/* ------------------------------------------------------------------ views */

const SOON = {
    cam: ['Ventiltrieb', 'Hub, Dauer und Lage von Ein- und Auslass im Betrieb, dazu die VTEC-Werte.'],
    air: ['Ansaugung & Abgas', 'Kommt später.'],
};

let buildView = 'b-layout';

function show(view) {
    document.querySelectorAll('main section').forEach(s => s.classList.remove('act'));
    if (view.startsWith('b-')) buildView = view;
    if (view === 'ign' || view === 'dyno' || view === 'cam' || view === 'fuel' || view === 'turbo' || view.startsWith('b-')) $('v-' + view).classList.add('act');
    else {
        $('soon-title').textContent = SOON[view][0];
        $('soon-text').textContent = SOON[view][1];
        $('v-soon').classList.add('act');
    }
    document.querySelectorAll('nav a').forEach(a => a.classList.toggle('act', a.dataset.v === view));
}

function setMode(mode) {
    document.body.dataset.mode = mode;
    $('m-build').classList.toggle('act', mode === 'build');
    $('m-tune').classList.toggle('act', mode === 'tune');
    show(mode === 'build' ? buildView : 'ign');
    if (mode === 'build' && build) build.updateStale();
}

$('m-build').onclick = () => setMode('build');
$('m-tune').onclick = () => setMode('tune');
document.querySelectorAll('nav a').forEach(a => a.onclick = () => { if (!a.querySelector('small')?.textContent.includes('später')) show(a.dataset.v); });

/* ------------------------------------------------------------------ live values */

let last = null;
const fmt = (v, d = 0) => Number.isFinite(v) ? v.toLocaleString('de-DE', { maximumFractionDigits: d, minimumFractionDigits: d }) : '–';

function onTelemetry(d) {
    last = d;
    if (typeof dyno !== 'undefined') dyno.onTelemetry(d);
    $('engineName').textContent = d.engine ? `${d.name} · ${fmt(d.octane)} ROZ` : 'kein Motor';
    if (!d.engine) return;

    $('t-rpm').textContent = fmt(d.rpm);
    $('t-redline').textContent = `U/min · Redline ${fmt(d.redline)}`;
    $('t-rpmbar').style.width = Math.min(100, d.rpm / d.redline * 100) + '%';
    $('t-map').textContent = fmt(d.map_kpa);
    $('t-adv').textContent = fmt(d.advance_deg);
    $('t-nm').textContent = fmt(d.torque_nm);
    $('t-ps').textContent = d.torque_nm !== 0 ? `Nm · ${fmt(d.power_kw * 1.35962)} PS` : 'Nm';
    $('tb-t-boost').textContent = d.boost_bar.toFixed(2);
    $('tb-t-rpm').textContent = fmt(d.turbo_rpm);
    $('tb-t-wg').textContent = fmt(d.wastegate * 100);
    $('tb-t-back').textContent = d.backpressure.toFixed(2);
    $('tb-t-bov').textContent = d.blowoff ? 'offen' : 'zu';
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

    $('kv').innerHTML = `<span>Gang</span><b>${d.gear || 'Leerlauf'}</b><span>Tempo</span><b>${fmt(d.speed_kmh)} km/h</b><span>Klopfereignisse</span><b>${fmt(d.knock_events)}</b>`;

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
const map = { rpm: [], load: [], adv: [], on: false };

async function loadTune() {
    try { tune = (await api.get('/tune')).values || {}; } catch (e) { tune = {}; }
    const curve = (tune.timing_curve || '').trim().split(/\s+/).map(p => p.split(':').map(Number)).filter(p => p.length === 2 && !p.some(isNaN));
    setField('c-idle', curve.length ? curve[0][1] : 12);
    setField('c-max', curve.length ? curve[curve.length - 1][1] : 34);
    setField('c-rev', tune.rev_limit_rpm !== undefined ? Number(tune.rev_limit_rpm) : 8000);
    setField('c-idlethr', tune.idle_throttle_plate_position !== undefined ? Number(tune.idle_throttle_plate_position) : 0.99);
    const cam = (tune.cam_center || '0 0').trim().split(/\s+/).map(Number);
    const tb = (tune.turbo || '0 0.8 0.35 1 0.7').trim().split(/\s+/).map(Number);
    $('tb-on').checked = tb[0] > 0.5;
    setField('tb-boost', tb[1] ?? 0.8);
    setField('tb-size', tb[2] ?? 0.35);
    setField('tb-inertia', tb[3] ?? 1);
    setField('tb-ic', tb[4] ?? 0.7);
    setField('c-lambda', tune.lambda !== undefined ? Number(tune.lambda) : 0.8);
    setField('cam-in', cam[0] || 0);
    setField('cam-ex', cam[1] || 0);

    readMap();
    renderMap();
    $('curve-card').style.opacity = map.on ? 0.5 : 1;
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
const turboText = () => `turbo ${$('tb-on').checked ? 1 : 0} ${$('tb-boost').value} ${$('tb-size').value} ${$('tb-inertia').value} ${$('tb-ic').value}`;
const sendTurbo = () => soon('turbo', () => api.post('/tune', turboText()), 120);
$('tb-on').onchange = sendTurbo;
['tb-boost', 'tb-size', 'tb-inertia', 'tb-ic'].forEach(id => linkField(id, sendTurbo));
linkField('c-lambda', v => soon('lambda', () => api.post('/tune', 'lambda ' + v)));
linkField('cam-in', () => soon('cam', () => api.post('/tune', `cam_center ${$('cam-in').value} ${$('cam-ex').value}`), 120));
linkField('cam-ex', () => soon('cam', () => api.post('/tune', `cam_center ${$('cam-in').value} ${$('cam-ex').value}`), 120));

/* ------------------------------------------------------------------ ignition map */

function readMap() {
    if (tune.map_rpm && tune.map_load && tune.map_adv && nums(tune.map_adv).length === nums(tune.map_rpm).length * nums(tune.map_load).length) {
        Object.assign(map, { rpm: nums(tune.map_rpm), load: nums(tune.map_load), adv: nums(tune.map_adv), on: true });
    } else {                                           // no map in the engine yet: start from the curve, switched off
        const idle = +$('c-idle').value || 12, max = +$('c-max').value || 34;
        const base = r => idle + (max - idle) * Math.min(1, r / 4000);
        map.rpm = [500, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000];
        map.load = [20, 40, 60, 80, 100];
        map.adv = map.rpm.flatMap(r => map.load.map(l => Math.round((base(r) + (100 - l) * 0.25) * 10) / 10));
        map.on = false;
    }
    $('map-on').checked = map.on;
}

const cellColor = v => v < 0 ? '#b3272a' : `hsl(${210 - Math.min(1, v / 50) * 170}, 55%, ${24 + Math.min(1, v / 50) * 14}%)`;

function renderMap() {
    const grid = $('map-grid');
    const columns = map.rpm.length;
    grid.style.gridTemplateColumns = `56px repeat(${columns}, 1fr)`;
    grid.innerHTML = '';

    // rows: load from high (top) to low (bottom); columns: speed from low to high
    for (let j = map.load.length - 1; j >= 0; j--) {
        const label = document.createElement('div');
        label.className = 'lab'; label.textContent = map.load[j] + ' kPa';
        grid.appendChild(label);

        for (let i = 0; i < columns; i++) {
            const cell = document.createElement('input');
            cell.type = 'number'; cell.step = '1';
            cell.value = map.adv[i * map.load.length + j];
            cell.style.background = cellColor(+cell.value);
            cell.dataset.i = i; cell.dataset.j = j;
            const index = i * map.load.length + j;
            cell.oninput = () => {
                if (cell.value === '') return;            // an empty field keeps the last value until ENTER decides
                map.adv[index] = +cell.value;
                cell.style.background = cellColor(+cell.value);
                if (map.on) soon('map', pushMap, 150);
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
                    const anchor = e.shiftKey && selection ? selection.anchor : [i + step[0], j + step[1]];
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
    map.rpm.forEach(r => { const l = document.createElement('div'); l.className = 'lab'; l.textContent = r; grid.appendChild(l); });
}

// Arrow keys move through the map: [column (speed), row (load)]. Rows are drawn with high load on top.
const ARROW_STEPS = { ArrowUp: [0, 1], ArrowDown: [0, -1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] };

const cellAt = (i, j) => document.querySelector(`#map-grid input[data-i="${i}"][data-j="${j}"]`);

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

const selectionStep = () => Math.abs(+$('sel-step').value) || 1;

function setCellValue(cell, value) {
    const v = Math.round(value * 10) / 10;
    map.adv[+cell.dataset.i * map.load.length + +cell.dataset.j] = v;
    cell.value = v;
    cell.style.background = cellColor(v);
}

// raise or lower the marked cells (or the single focused one) by the same amount
function adjustSelection(delta) {
    const cells = selectedCells();
    if (!cells.length && document.activeElement && document.activeElement.dataset.i !== undefined) cells.push(document.activeElement);
    cells.forEach(c => setCellValue(c, map.adv[+c.dataset.i * map.load.length + +c.dataset.j] + delta));
    // setting a value drops the text selection, so the next typed number would be appended
    const active = document.activeElement;
    if (active && active.dataset && active.dataset.i !== undefined) active.select();
    if (map.on) soon('map', pushMap, 150);
}

// ENTER accepts the typed value at once (for every marked cell) and sends it to the engine
function commitCell(cell, index) {
    const value = parseFloat(cell.value);
    if (Number.isNaN(value)) cell.value = map.adv[index];   // empty or not a number: keep the old value
    else {
        const cells = selectedCells();
        (cells.length > 1 ? cells : [cell]).forEach(c => setCellValue(c, value));
    }
    if (map.on) pushMap();
    cell.select();
}

function pushMap() {
    api.post('/tune', `map_rpm ${map.rpm.join(' ')}\nmap_load ${map.load.join(' ')}\nmap_adv ${map.adv.join(' ')}`);
}

$('sel-up').onclick = () => adjustSelection(selectionStep());
$('sel-dn').onclick = () => adjustSelection(-selectionStep());

$('map-on').onchange = () => {
    map.on = $('map-on').checked;
    if (map.on) pushMap(); else api.post('/tune', 'map_off 1');
    $('curve-card').style.opacity = map.on ? 0.5 : 1;
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
    if (!map.rpm.length || !$('v-ign').classList.contains('act')) { dot.classList.add('hidden'); return; }

    const [i0, i1, ti] = fraction(map.rpm, rpm), [j0, j1, tj] = fraction(map.load, kpa);
    const cell = (i, j) => $('map-grid').querySelector(`input[data-i="${i}"][data-j="${j}"]`);
    const a = cell(i0, j0), b = cell(i1, j1);
    if (!a || !b) return;

    const cx = (c) => c.offsetLeft + c.offsetWidth / 2, cy = (c) => c.offsetTop + c.offsetHeight / 2;
    const x = cx(a) + (cx(cell(i1, j0)) - cx(a)) * ti, y = cy(a) + (cy(cell(i0, j1)) - cy(a)) * tj;
    dot.style.left = x + 'px'; dot.style.top = y + 'px';
    dot.classList.remove('hidden');
}

function foldAdvance(offset, d) {
    const round = v => Math.round(v * 10) / 10;
    if (map.on) {
        const [i0, i1] = fraction(map.rpm, d.rpm), [j0, j1] = fraction(map.load, d.map_kpa);
        const rows = map.load.length;
        for (const i of new Set([i0, i1])) for (const j of new Set([j0, j1])) map.adv[i * rows + j] = round(map.adv[i * rows + j] + offset);
        renderMap();
        pushMap();
        return `${offset > 0 ? '+' : ''}${offset}° im Kennfeld bei ${Math.round(d.rpm)} U/min und ${Math.round(d.map_kpa)} kPa übernommen.`;
    }

    setField('c-idle', round(+$('c-idle').value + offset));
    setField('c-max', round(+$('c-max').value + offset));
    api.post('/tune', 'timing_curve ' + curveText());
    return `${offset > 0 ? '+' : ''}${offset}° in der Zündkurve übernommen (Leerlauf und Maximum).`;
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
    foldAdvance,
});

setMode('tune');
connect();
setTimeout(() => { if (!(last && last.engine)) setMode('build'); }, 1500);
