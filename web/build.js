// "Bauen": everything that needs the engine to be loaded again (arrangement, dimensions, valve train layout ...).
// The engine script is made by generator.js and sent to the server with POST /engine.

import { SECTIONS, PRESETS, AUTO_DEFAULTS, FUELS, autoLayout, analyze, generate, defaultFiring, fireOf, bankDelta, mod, r4 } from './generator.js';
import { $, api } from './util.js';

// Values that are edited in the "Tunen" views: they keep their defaults here and the running simulator's
// values are used when the engine is loaded (see generate(p, cfg, tuning)).
const HIDDEN = new Set(['idlePlate', 'revLimit', 'advanceIdle', 'advanceMax']);

const CONTAINERS = {
    'Allgemein': 'f-allgemein', 'Hubraum': 'f-hubraum', 'Massen': 'f-massen', 'Nockenwelle': 'f-nocken',
    'Nockenumschaltung (VTEC)': 'f-vtec', 'Ansaugung / Abgas': 'f-ansaugung', 'Kraftstoff / Klopfen': 'f-kraftstoff',
    'Zündung / Drehzahl': 'f-zuendung', 'Fahrzeug': 'f-fahrzeug',
};

// [min, max, step]: same limits as the validation (lift 3...22 mm, duration 150...330 deg)
const SLIDERS = {};
for (const k of ['intakeLift', 'exhaustLift', 'highIntakeLift', 'highExhaustLift']) SLIDERS[k] = [3, 22, 0.1];
for (const k of ['intakeDuration', 'exhaustDuration', 'highIntakeDuration', 'highExhaustDuration']) SLIDERS[k] = [150, 330, 1];
for (const k of ['intakeCenter', 'exhaustCenter', 'highIntakeCenter', 'highExhaustCenter']) SLIDERS[k] = [90, 135, 1];
SLIDERS.camSwitchRpm = [1000, 12000, 100];

const AUTO_ROWS = [
    ['layout', 'Bauart', '', ['inline:Reihe', 'v:V-Motor', 'flat:Boxer', 'w:W-Motor (3 Bänke)', 'radial:Sternmotor']],
    ['cylinders', 'Zylinderzahl', '', 'n'],
    ['angle', 'Winkel (°)', 'V: Gesamtwinkel · W: Abstand der Außenbänke', 'n'],
    ['firing', 'Zündfolge', 'gleichmäßige Zündabstände', 't'],
];
const AUTO_KEYS = ['layout', 'cylinders', 'angle', 'firing'];
const COLORS = ['#e5533d', '#8794a3', '#3d8fe5', '#e5b83d'];            // power, exhaust, intake, compression
const LAYOUT_NAMES = { inline: 'Reihe', v: 'V', flat: 'Boxer', w: 'W', radial: 'Stern' };
const f1 = x => (Math.round(x * 10) / 10).toFixed(1);
const STORE = 'engineTunerBuild';

export function initBuild(hooks) {
    const inputs = {};
    const defaults = { ...AUTO_DEFAULTS };
    SECTIONS.forEach(([, fields]) => fields.forEach(([k, , , d]) => { defaults[k] = d; }));
    defaults.exhaustMode = 'bank';

    let cfg = autoLayout({ ...AUTO_DEFAULTS }).cfg;
    let autoErrors = [];
    let alpha = 0, sel = -1, keepFire = true, playing = false, lastT = 0;
    let lastCode = '', dirty = false, loadedOnce = false;

    /* ------------------------------------------------------------ form */

    function addField(parent, key, label, hint, type, def) {
        const row = document.createElement('label');
        row.className = 'bfield';
        row.innerHTML = `<span>${label}${hint ? `<small>${hint}</small>` : ''}</span>`;
        let el;
        if (Array.isArray(type)) {
            el = document.createElement('select');
            type.forEach(o => { const [v, t] = o.split(':'); el.add(new Option(t, v)); });
        } else {
            el = document.createElement('input');
            el.type = type === 'n' ? 'number' : 'text';
            if (type === 'n') el.step = 'any';
        }
        el.value = def;
        inputs[key] = { el, num: type === 'n' };

        const range = SLIDERS[key];
        if (range && type === 'n') {
            const box = document.createElement('div');
            box.className = 'sliderbox';
            const slider = document.createElement('input');
            slider.type = 'range'; slider.min = range[0]; slider.max = range[1]; slider.step = range[2]; slider.value = def;
            slider.addEventListener('input', () => { el.value = slider.value; el.dispatchEvent(new Event('input', { bubbles: true })); });
            el.addEventListener('input', () => { slider.value = el.value; });
            inputs[key].sync = () => { slider.value = el.value; };
            box.append(el, slider);
            row.appendChild(box);
        } else row.appendChild(el);
        parent.appendChild(row);
    }

    AUTO_ROWS.forEach(([key, label, hint, type]) => addField($('autoFields'), key, label, hint, type, defaults[key]));
    SECTIONS.forEach(([title, fields]) => fields.forEach(([key, label, hint, def, type]) => {
        addField(HIDDEN.has(key) ? $('hidden-fields') : $(CONTAINERS[title]), key, label, hint, type, def);
    }));

    Object.keys(PRESETS).forEach(k => $('preset').add(new Option(k, k)));

    function readParams() {
        const p = {};
        for (const [k, { el, num }] of Object.entries(inputs)) p[k] = num ? parseFloat(el.value) : el.value;
        p.singlePin = $('singlePin').checked;
        return p;
    }

    const cylinderCc = () => Math.PI / 4 * parseFloat(inputs.bore.el.value) ** 2 * parseFloat(inputs.stroke.el.value) / 1000;

    // The chamber volume is independent of bore and stroke; only the compression ratio ties the three together.
    function syncCompression(key) {
        const vd = cylinderCc();
        const chamber = parseFloat(inputs.chamber.el.value);
        const cr = parseFloat(inputs.compression.el.value);
        if (!(vd > 0)) return;

        if (key === 'compression') {
            if (cr > 1) inputs.chamber.el.value = r4(vd / (cr - 1));
        }
        else if ((key === 'bore' || key === 'stroke') && $('keepCR').checked && cr > 1) {
            inputs.chamber.el.value = r4(vd / (cr - 1));
        }
        else if (chamber > 0) {
            inputs.compression.el.value = r4((vd + chamber) / chamber);
        }
    }

    function writeParams(p) {
        for (const [k, v] of Object.entries(p)) {
            if (k === 'singlePin') $('singlePin').checked = !!v;
            else if (inputs[k]) { inputs[k].el.value = v; if (inputs[k].sync) inputs[k].sync(); }
        }
        syncCompression('chamber');        // presets and saved states only carry the chamber volume
    }

    /* ------------------------------------------------------------ bank / cylinder tables */

    function applyAuto() {
        const r = autoLayout(readParams());
        cfg = r.cfg; autoErrors = r.errors; sel = -1;
        renderTables(); refresh();
    }

    function renderTables() {
        $('banksTbl').innerHTML = `<table><tr><th>Bank</th><th>Winkel °</th><th>Zyl.</th><th></th></tr>` +
            cfg.banks.map((a, i) => {
                const cnt = cfg.cyls.filter(c => c.bank === i).length;
                return `<tr><td>${i + 1}</td><td><input type="number" step="any" data-k="bank" data-i="${i}" value="${r4(a)}"></td>
                    <td>${cnt}</td><td><button class="x" data-act="delbank" data-i="${i}" ${cnt || cfg.banks.length < 2 ? 'disabled' : ''}>✕</button></td></tr>`;
            }).join('') + `</table><div class="row"><button data-act="addbank">+ Bank</button></div>`;

        const opts = cfg.banks.map((_, i) => `<option value="${i}">Bank ${i + 1}</option>`).join('');
        $('cylsTbl').innerHTML = `<table><tr><th>Nr</th><th>Bank</th><th>Zapfen °</th><th>Umdr.</th><th>Zündung °</th><th></th></tr>` +
            cfg.cyls.map((c, i) => `<tr data-row="${i}" class="${i === sel ? 'sel' : ''}"><td>${i + 1}</td>
                <td><select data-k="cbank" data-i="${i}">${opts.replace(`value="${c.bank}"`, `value="${c.bank}" selected`)}</select></td>
                <td><input type="number" step="any" data-k="pin" data-i="${i}" value="${r4(c.pin)}"></td>
                <td><select data-k="rev" data-i="${i}"><option value="0"${c.rev ? '' : ' selected'}>1.</option><option value="1"${c.rev ? ' selected' : ''}>2.</option></select></td>
                <td class="fire" data-fire="${i}"></td>
                <td><button class="x" data-act="delcyl" data-i="${i}">✕</button></td></tr>`).join('') +
            `</table><div class="row"><button data-act="addcyl">+ Zylinder</button></div>`;
    }

    function syncTables() {
        document.querySelectorAll('#v-b-layout input[data-k]').forEach(el => {
            if (el === document.activeElement) return;
            const i = +el.dataset.i;
            el.value = el.dataset.k === 'bank' ? r4(cfg.banks[i]) : r4(cfg.cyls[i].pin);
        });
        const an = analyze(readParams(), cfg);
        document.querySelectorAll('[data-fire]').forEach(td => { td.textContent = f1(an.fires[+td.dataset.fire]); });
        document.querySelectorAll('#cylsTbl tr[data-row]').forEach(tr => tr.classList.toggle('sel', +tr.dataset.row === sel));
    }

    function setBankAngle(i, v) {
        const fires = cfg.cyls.map(c => fireOf(cfg, c));
        cfg.banks[i] = v;
        if (keepFire) cfg.cyls.forEach((c, j) => { c.pin = r4(mod(mod(fires[j], 360) + bankDelta(cfg, c), 360)); });
    }

    const main = document.querySelector('main');
    const inBuild = ev => ev.target.closest && ev.target.closest('.bsec');

    main.addEventListener('input', ev => {
        if (!inBuild(ev)) return;
        const t = ev.target, k = t.dataset && t.dataset.k;
        const paramKey = Object.keys(inputs).find(name => inputs[name].el === t);
        if (paramKey) syncCompression(paramKey);
        if (k === 'bank' && t.value !== '') setBankAngle(+t.dataset.i, parseFloat(t.value));
        else if (k === 'pin' && t.value !== '') cfg.cyls[+t.dataset.i].pin = mod(parseFloat(t.value), 360);
        touch(); refresh();
    });

    main.addEventListener('change', ev => {
        if (!inBuild(ev)) return;
        const t = ev.target, k = t.dataset && t.dataset.k;
        touch();
        if (k === 'cbank') { cfg.cyls[+t.dataset.i].bank = +t.value; renderTables(); }
        else if (k === 'rev') cfg.cyls[+t.dataset.i].rev = +t.value;
        else if (t.id === 'keep') keepFire = t.checked;
        else if (t === inputs.layout.el || t === inputs.cylinders.el) {
            const p = readParams();
            inputs.firing.el.value = defaultFiring(p.layout, p.cylinders);
            if (p.layout === 'radial') $('singlePin').checked = true;
            applyAuto(); return;
        } else if (AUTO_KEYS.some(a => inputs[a].el === t) || t.id === 'singlePin') { applyAuto(); return; }
        refresh();
    });

    main.addEventListener('click', ev => {
        if (!inBuild(ev)) return;
        const t = ev.target.closest('[data-act]');
        if (!t) {
            const tr = ev.target.closest('tr[data-row]');
            if (tr) { sel = +tr.dataset.row; syncTables(); drawGraphics(); }
            return;
        }

        const i = +t.dataset.i;
        if (t.dataset.act === 'addbank') cfg.banks.push(0);
        else if (t.dataset.act === 'delbank') { cfg.banks.splice(i, 1); cfg.cyls.forEach(c => { if (c.bank > i) c.bank--; }); }
        else if (t.dataset.act === 'addcyl') cfg.cyls.push({ bank: 0, pin: 0, rev: 0 });
        else if (t.dataset.act === 'delcyl') { cfg.cyls.splice(i, 1); sel = -1; }
        else return;
        touch(); renderTables(); refresh();
    });

    $('regen').addEventListener('click', () => { touch(); applyAuto(); });

    /* ------------------------------------------------------------ graphics */

    const CX = 210, CY = 200;
    const pt = (r, deg) => [CX + r * Math.cos(deg * Math.PI / 180), CY - r * Math.sin(deg * Math.PI / 180)];
    const stroke = (a, fire) => Math.floor(mod(a - fire, 720) / 180);

    function pinGroups() {
        const groups = [];
        cfg.cyls.forEach((c, i) => {
            const key = r4(mod(c.pin, 360));
            let g = groups.find(q => q.key === key);
            if (!g) { g = { key, ids: [] }; groups.push(g); }
            g.ids.push(i);
        });
        return groups;
    }

    function drawEnd(an) {
        let s = `<svg viewBox="0 0 420 420" width="420" height="420">`;
        cfg.banks.forEach((ang, i) => {
            const dir = 90 + ang;
            const [x1, y1] = pt(60, dir), [x2, y2] = pt(186, dir), [lx, ly] = pt(204, dir);
            s += `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#39414a" stroke-width="2" stroke-dasharray="4 4"/>
                  <circle cx="${x2}" cy="${y2}" r="7" fill="#2f6feb" stroke="#fff" stroke-width="1.5" data-drag="bank:${i}" style="cursor:grab"/>
                  <text x="${lx}" y="${ly}" text-anchor="middle" dominant-baseline="middle">B${i + 1} ${f1(ang)}°</text>`;
            const mine = cfg.cyls.map((c, j) => ({ c, j })).filter(x => x.c.bank === i);
            mine.forEach((x, k) => {
                const row = Math.floor(k / 6), col = k % 6, cols = Math.min(6, mine.length - row * 6);
                const off = (col - (cols - 1) / 2) * 26;
                const [bx, by] = pt(96 + row * 26, dir);
                const px = bx + off * Math.cos((dir + 90) * Math.PI / 180), py = by - off * Math.sin((dir + 90) * Math.PI / 180);
                const st = stroke(alpha, an.fires[x.j]);
                s += `<g data-cyl="${x.j}" style="cursor:pointer"><circle cx="${px}" cy="${py}" r="11" fill="${COLORS[st]}" stroke="${x.j === sel ? '#fff' : '#15171a'}" stroke-width="${x.j === sel ? 3 : 1.5}"/>
                      <text x="${px}" y="${py + 0.5}" text-anchor="middle" dominant-baseline="middle" style="fill:#111;font-weight:600">${x.j + 1}</text></g>`;
            });
        });
        s += `<circle cx="${CX}" cy="${CY}" r="52" fill="#1b1f24" stroke="#4a535d" stroke-width="1.5"/><circle cx="${CX}" cy="${CY}" r="5" fill="#8b95a1"/>`;
        pinGroups().forEach(g => {
            const dir = 90 + cfg.banks[0] + g.key - alpha;
            const [x, y] = pt(52, dir);
            const label = g.ids.map(i => i + 1).join(',');
            const hl = g.ids.includes(sel);
            s += `<line x1="${CX}" y1="${CY}" x2="${x}" y2="${y}" stroke="#6b7785" stroke-width="3"/>
                  <circle cx="${x}" cy="${y}" r="${g.ids.length > 1 ? 12 : 9}" fill="${hl ? '#fff' : '#d0d7de'}" stroke="#2f6feb" stroke-width="2" data-drag="pin:${g.key}" style="cursor:grab"/>
                  <text x="${x}" y="${y + 0.5}" text-anchor="middle" dominant-baseline="middle" style="fill:#111;font-size:${g.ids.length > 2 ? 8 : 10}px;pointer-events:none">${label}</text>`;
        });
        const [ax, ay] = pt(36, 40), [bx, by] = pt(36, -10);
        s += `<path d="M ${ax} ${ay} A 36 36 0 0 1 ${bx} ${by}" fill="none" stroke="#6f7a86" stroke-width="1.5" marker-end="url(#ah)"/>
              <defs><marker id="ah" markerWidth="6" markerHeight="6" refX="3" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6 z" fill="#6f7a86"/></marker></defs>`;
        $('endwrap').innerHTML = s + `</svg>`;
    }

    function drawTimeline(an) {
        const n = cfg.cyls.length;
        const W = 760, X0 = 30, PW = 710, ax = a => X0 + a / 720 * PW;
        const rh = Math.max(6, Math.min(20, Math.floor(300 / Math.max(n, 1))));
        const top = 22, H1 = top + n * rh, chartH = 70, H = H1 + 16 + chartH + 24;
        let s = `<svg viewBox="0 0 ${W} ${H}" width="100%">`;
        for (let a = 0; a <= 720; a += 180) s += `<line x1="${ax(a)}" y1="${top - 4}" x2="${ax(a)}" y2="${H1}" stroke="#39414a"/><text x="${ax(a)}" y="12" text-anchor="middle">${a}°</text>`;
        cfg.cyls.forEach((_, i) => {
            const y = top + i * rh, fire = an.fires[i];
            s += `<text x="${X0 - 6}" y="${y + rh / 2 + 1}" text-anchor="end" dominant-baseline="middle" ${i === sel ? 'style="fill:#fff;font-weight:700"' : ''}>${i + 1}</text>`;
            for (let k = 0; k < 4; k++) {
                const a0 = mod(fire + 180 * k, 720), a1 = a0 + 180;
                const rect = (x0, x1) => `<rect x="${ax(x0)}" y="${y + 1}" width="${ax(x1) - ax(x0)}" height="${rh - 2}" fill="${COLORS[k]}" opacity=".85"/>`;
                if (a1 <= 720) s += rect(a0, a1); else s += rect(a0, 720) + rect(0, a1 - 720);
            }
            s += `<line x1="${ax(fire)}" y1="${y}" x2="${ax(fire)}" y2="${y + rh}" stroke="#fff" stroke-width="2"/>`;
        });

        const cy0 = H1 + 16;
        const counts = [];
        let mx = 1;
        for (let a = 0; a < 720; a += 2) {
            let c = 0; an.fires.forEach(fi => { if (mod(a - fi, 720) < 180) c++; });
            counts.push(c); mx = Math.max(mx, c);
        }
        let d = '';
        counts.forEach((c, i) => { const x = ax(i * 2), y = cy0 + chartH - c / mx * (chartH - 4); d += (i ? 'L' : 'M') + `${x} ${y} `; });
        s += `<text x="${X0}" y="${cy0 - 4}" style="fill:#8b95a1">Zylinder im Arbeitstakt (max ${mx}, Mittel ${(n / 4).toFixed(2)})</text>
              <rect x="${X0}" y="${cy0}" width="${PW}" height="${chartH}" fill="none" stroke="#39414a"/>
              <line x1="${X0}" x2="${X0 + PW}" y1="${cy0 + chartH - n / 4 / mx * (chartH - 4)}" y2="${cy0 + chartH - n / 4 / mx * (chartH - 4)}" stroke="#6f7a86" stroke-dasharray="3 3"/>
              <path d="${d}" fill="none" stroke="#e5533d" stroke-width="1.5"/>`;
        s += `<line x1="${ax(alpha)}" y1="${top - 4}" x2="${ax(alpha)}" y2="${cy0 + chartH}" stroke="#fff" stroke-width="1.5"/>`;
        $('tlwrap').innerHTML = s + `</svg>`;
    }

    function drawGraphics() {
        const an = analyze(readParams(), cfg);
        drawEnd(an); drawTimeline(an);
        $('alphaLabel').textContent = Math.round(alpha) + '°';
    }

    /* dragging the bank and crank pin handles */
    let drag = null;
    const ew = $('endwrap');
    function svgAngle(ev) {
        const svg = ew.querySelector('svg'), point = svg.createSVGPoint();
        point.x = ev.clientX; point.y = ev.clientY;
        const q = point.matrixTransform(svg.getScreenCTM().inverse());
        return Math.atan2(-(q.y - CY), q.x - CX) * 180 / Math.PI;
    }
    ew.addEventListener('pointerdown', ev => {
        const t = ev.target.closest('[data-drag]');
        if (t) {
            const [kind, v] = t.dataset.drag.split(':');
            drag = { kind, v: parseFloat(v), i: parseInt(v) };
            ew.setPointerCapture(ev.pointerId); ev.preventDefault();
            return;
        }
        const c = ev.target.closest('[data-cyl]');
        if (c) { sel = +c.dataset.cyl; syncTables(); drawGraphics(); }
    });
    ew.addEventListener('pointermove', ev => {
        if (!drag) return;
        const snap = ev.shiftKey ? 5 : 1;
        const phi = svgAngle(ev);
        if (drag.kind === 'bank') {
            let v = phi - 90; v = mod(v + 180, 360) - 180;
            setBankAngle(drag.i, Math.round(v / snap) * snap);
        } else {
            const v = mod(Math.round((phi - 90 - cfg.banks[0] + alpha) / snap) * snap, 360);
            cfg.cyls.forEach(c => { if (r4(mod(c.pin, 360)) === drag.v) c.pin = v; });
            drag.v = v;
        }
        touch(); syncTables(); refresh(true);
    });
    ew.addEventListener('pointerup', () => { drag = null; });

    /* the crank turns, to see which cylinder is in which stroke */
    $('alpha').addEventListener('input', () => { alpha = +$('alpha').value; drawGraphics(); });
    $('play').addEventListener('click', () => {
        playing = !playing; $('play').textContent = playing ? '❚❚' : '▶'; lastT = 0;
        if (playing) requestAnimationFrame(tick);
    });
    function tick(t) {
        if (!playing) return;
        if (lastT) { alpha = mod(alpha + (t - lastT) * 0.12, 720); $('alpha').value = alpha; drawGraphics(); }
        lastT = t; requestAnimationFrame(tick);
    }

    /* ------------------------------------------------------------ fuel presets */

    function applyFuelPreset() {
        const preset = FUELS[inputs.fuel.el.value];
        if (!preset) return;
        inputs.octane.el.value = preset.ron;
        inputs.ethanol.el.value = preset.ethanol;
    }
    function syncFuelSelect() {
        const ron = parseFloat(inputs.octane.el.value), eth = parseFloat(inputs.ethanol.el.value);
        const match = Object.keys(FUELS).find(k => FUELS[k].ron === ron && FUELS[k].ethanol === eth);
        inputs.fuel.el.value = match || 'custom';
    }
    inputs.fuel.el.addEventListener('change', applyFuelPreset);
    inputs.octane.el.addEventListener('input', syncFuelSelect);
    inputs.ethanol.el.addEventListener('input', syncFuelSelect);

    /* ------------------------------------------------------------ summary, checks, load */

    function touch() { dirty = true; updateStale(); }

    function updateStale() {
        const chip = $('stale');
        if (!hooks.hasEngine() && !loadedOnce) { chip.className = 'chip stale only-build'; chip.querySelector('span').textContent = 'Noch kein Motor geladen'; }
        else if (dirty) { chip.className = 'chip stale only-build'; chip.querySelector('span').textContent = 'Änderungen noch nicht geladen – der alte Motor läuft weiter'; }
        else if (!loadedOnce) { chip.className = 'chip stale only-build'; chip.querySelector('span').textContent = 'Ein Motor läuft – diese Einstellungen sind noch nicht geladen'; }
        else { chip.className = 'chip only-build'; chip.querySelector('span').textContent = 'Der geladene Motor entspricht den Einstellungen'; }
    }

    let errorsNow = [];
    function refresh(skipTables) {
        const p = readParams();
        let res;
        try { res = generate(p, cfg); } catch (e) { $('msgs').innerHTML = `<div class="err">${e.message}</div>`; return; }
        const a = res.analysis;

        $('dispTotal').innerHTML = a.n && a.vdCyl > 0
            ? `${a.displacement.toFixed(3)} L <small>${a.n} × ${a.vdCyl.toFixed(1)} cc = ${(a.displacement * 1000).toFixed(0)} cc</small>`
            : '–';

        const tile = (label, value, small = '') => `<div class="tile"><label>${label}</label><div class="v">${value} <small>${small}</small></div></div>`;
        $('build-bar').innerHTML =
            tile('Hubraum', a.displacement.toFixed(2), `L · ${a.n} Zyl.`) +
            tile('Bohrung × Hub', `${p.bore} × ${p.stroke}`, 'mm') +
            tile('Verdichtung', a.cr.toFixed(1), ': 1') +
            tile('Anordnung', LAYOUT_NAMES[p.layout] || p.layout, `${cfg.banks.length} Bank${cfg.banks.length > 1 ? 'en' : ''}`) +
            tile('Zündfolge', a.order.join('-'), a.even ? 'gleichmäßig' : 'ungleichmäßig') +
            tile('Ventiltrieb', p.camSwitch === 'on' ? 'VTEC' : 'Standard', p.camSwitch === 'on' ? '2 Profile' : '1 Profil');

        $('b-kv').innerHTML = `<span>Motor</span><b>${p.name}</b><span>Hubraum</span><b>${(a.displacement * 1000).toFixed(0)} cm³</b>` +
            `<span>Verdichtung</span><b>${a.cr.toFixed(1)} : 1</b><span>Bohrung/Hub</span><b>${(p.bore / p.stroke).toFixed(2)}</b>` +
            `<span>Pleuel/Hub</span><b>${(p.rodLength / p.stroke).toFixed(2)}</b><span>Zündabstände</span><b>${a.intervals.map(x => f1(x) + '°').join(' ')}</b>`;

        errorsNow = [...autoErrors, ...a.errors];
        $('msgs').innerHTML = (errorsNow.length ? '' : '<div class="ok">✓ Konfiguration gültig</div>')
            + errorsNow.map(e => `<div class="err">✗ ${e}</div>`).join('')
            + a.warnings.map(e => `<div class="warn">⚠ ${e}</div>`).join('');
        $('b-load').disabled = errorsNow.length > 0;
        lastCode = errorsNow.length ? '' : res.text;

        if (!skipTables) syncTables();
        drawGraphics();
        try { localStorage.setItem(STORE, JSON.stringify({ p, cfg })); } catch (e) { /* ignore */ }
    }

    function restore(state) {
        writeParams(state.p);
        cfg = state.cfg; autoErrors = []; sel = -1;
        renderTables(); refresh();
    }

    function status(text, cls = '') { $('b-status').className = 'note ' + cls; $('b-status').textContent = text; }

    async function loadEngine() {
        if (errorsNow.length) { status('Bitte zuerst die Fehler beheben.', 'err'); return; }
        $('b-load').disabled = true;
        status('Lade den Motor …');

        let tuning = null;                               // what was tuned in the running simulator stays
        try { tuning = (await api.get('/tune')).values; } catch (e) { /* no simulator yet */ }

        try {
            const text = generate(readParams(), cfg, tuning).text;
            const answer = await api.post('/engine', text);
            if (answer.ok) {
                dirty = false; loadedOnce = true; updateStale();
                status(answer.mode === 'live' ? 'Geladen (ohne Neustart).' : 'Motor neu geladen.', 'ok');
                hooks.onLoaded();
            } else {
                status('Der Simulator meldet einen Fehler (der alte Motor läuft weiter): ' + (answer.error || 'unbekannt'), 'err');
            }
        } catch (e) {
            status('Keine Verbindung zum Simulator.', 'err');
        }
        $('b-load').disabled = errorsNow.length > 0;
    }

    $('b-load').addEventListener('click', loadEngine);
    $('b-load-top').addEventListener('click', loadEngine);
    $('preset').addEventListener('change', () => {
        writeParams({ ...defaults, ...PRESETS[$('preset').value] });
        if (!PRESETS[$('preset').value].firing) inputs.firing.el.value = defaultFiring(inputs.layout.el.value, +inputs.cylinders.el.value);
        touch(); applyAuto();
    });

    const download = (name, text, type) => {
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([text], { type }));
        a.download = name; a.click();
    };
    $('b-mr').addEventListener('click', () => { if (lastCode) download(($('b-file').value || 'engine') .replace(/\.mr$/, '') + '.mr', lastCode, 'text/plain'); });
    $('b-jsave').addEventListener('click', () => download(($('b-file').value || 'engine').replace(/\.mr$/, '') + '.json', JSON.stringify({ p: readParams(), cfg }, null, 1), 'application/json'));
    $('b-jload').addEventListener('click', () => $('b-jfile').click());
    $('b-jfile').addEventListener('change', async () => {
        const file = $('b-jfile').files[0];
        if (file) { try { restore(JSON.parse(await file.text())); touch(); } catch (e) { alert('Datei konnte nicht gelesen werden: ' + e.message); } }
    });

    // initial state: the last session (or the one of the old engine builder), else defaults
    let saved = null;
    for (const key of [STORE, 'engineBuilder']) {
        try { saved = JSON.parse(localStorage.getItem(key)); } catch (e) { saved = null; }
        if (saved && saved.p && saved.cfg && Array.isArray(saved.cfg.banks)) break;
        saved = null;
    }
    if (saved) restore(saved); else { writeParams(defaults); applyAuto(); }
    updateStale();

    return { updateStale };
}
