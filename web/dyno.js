// "Prüfstand & Vergleich": measured runs on the running simulator (pull / steps), results kept for comparison,
// and the speed hold for tuning (full load at one speed while the ignition advance is changed live).

import { SECTIONS } from './generator.js';
import { $, api, soon } from './util.js';

const RUN_COLORS = ['#e5533d', '#3d8fe5', '#7ee787', '#e5b83d', '#c678dd', '#56d4dd', '#ff9f68', '#b0b8c1'];
const STORE = 'engineTunerRuns';
const PS = kw => kw * 1000 / 735.49875;
const peak = (pts, key) => pts.reduce((b, p) => (p[key] > b[key] ? p : b), pts[0]);

// parameters that belong to the tuning views: their values come from GET /tune, not from the build form
const TUNE_OWNED = new Set(['idlePlate', 'revLimit', 'advanceIdle', 'advanceMax']);
const TUNE_LABELS = {
    rev_limit_rpm: 'Drehzahlbegrenzer', idle_throttle_plate_position: 'Leerlauf-Drossel', advance_offset: 'Zündwinkel-Versatz (°)',
};

const labelOf = {};
SECTIONS.forEach(([, fields]) => fields.forEach(([k, l]) => { labelOf[k] = l; }));

function diffSnapshots(a, b) {
    if (!a) return [];
    const out = [];
    for (const k of Object.keys(labelOf)) {
        if (k === 'name' || k === 'compression' || TUNE_OWNED.has(k)) continue;
        if (a.p[k] !== undefined && String(a.p[k]) !== String(b.p[k])) out.push(`${labelOf[k].replace(/ \(.*\)$/, '')}: ${a.p[k]} → ${b.p[k]}`);
    }
    if (JSON.stringify(a.cfg) !== JSON.stringify(b.cfg)) out.push('Anordnung geändert');

    if (a.tune && b.tune) {                                     // values of the tuning views (older runs have none)
        const get = (s, k) => (s.tune[k] === undefined ? (k === 'advance_offset' ? '0' : undefined) : String(s.tune[k]));
        for (const [k, label] of Object.entries(TUNE_LABELS)) {
            if (get(a, k) !== get(b, k)) out.push(`${label}: ${get(a, k)} → ${get(b, k)}`);
        }
        if (get(a, 'timing_curve') !== get(b, 'timing_curve')) out.push('Zündkurve geändert');
        if (get(a, 'map_adv') !== get(b, 'map_adv') || get(a, 'map_rpm') !== get(b, 'map_rpm')) out.push('Zündkennfeld geändert');
    }
    return out;
}

export function initDyno(hooks) {
    let runs = [];
    try { runs = JSON.parse(localStorage.getItem(STORE) || localStorage.getItem('engineBuilderRuns')) || []; } catch (e) { runs = []; }
    let running = false, livePoints = [], runMode = 'Zug', hoverRun = null;
    let tele = null;

    const saveRuns = () => { try { localStorage.setItem(STORE, JSON.stringify(runs.slice(-12))); } catch (e) { /* ignore */ } };

    /* ------------------------------------------------------------ chart of the runs */

    function drawDyno() {
        const shown = runs.filter(r => r.visible);
        const all = shown.map(r => ({ r, pts: r.points }))
            .sort((a, b) => (a.r === hoverRun) - (b.r === hoverRun))
            .concat(livePoints.length ? [{ r: { color: '#ffffff', live: true }, pts: livePoints }] : []);
        const W = 760, H = 320, L = 46, R = 46, T = 14, B = 34;
        let s = `<svg viewBox="0 0 ${W} ${H}" width="100%">`;
        if (!all.length) {
            $('d-chart').innerHTML = s + `<text x="${W / 2}" y="${H / 2}" text-anchor="middle" style="fill:#6f7a86;font-size:13px">Noch keine Messung – „Messlauf starten“</text></svg>`;
            return;
        }

        const pts = all.flatMap(a => a.pts);
        const x0 = Math.floor(Math.min(...pts.map(p => p.rpm)) / 500) * 500, x1 = Math.max(x0 + 500, Math.ceil(Math.max(...pts.map(p => p.rpm)) / 500) * 500);
        const nMax = Math.ceil(Math.max(...pts.map(p => p.nm), 50) / 50) * 50;
        const pMax = Math.ceil(Math.max(...pts.map(p => PS(p.kw)), 50) / 50) * 50;
        const X = v => L + (v - x0) / (x1 - x0) * (W - L - R);
        const Yn = v => T + (1 - Math.max(0, v) / nMax) * (H - T - B);
        const Yp = v => T + (1 - Math.max(0, v) / pMax) * (H - T - B);

        for (let i = 0; i <= 5; i++) {
            const y = T + i / 5 * (H - T - B);
            s += `<line x1="${L}" x2="${W - R}" y1="${y}" y2="${y}" stroke="#262b31"/>
                  <text x="${L - 6}" y="${y + 4}" text-anchor="end">${Math.round(nMax * (1 - i / 5))}</text>
                  <text x="${W - R + 6}" y="${y + 4}">${Math.round(pMax * (1 - i / 5))}</text>`;
        }
        for (let r = x0; r <= x1; r += 500) {
            s += `<line x1="${X(r)}" x2="${X(r)}" y1="${T}" y2="${H - B}" stroke="#20252a"/><text x="${X(r)}" y="${H - B + 16}" text-anchor="middle">${r}</text>`;
        }
        s += `<text x="${L - 6}" y="${T - 3}" text-anchor="end" style="fill:#e5b83d">Nm</text><text x="${W - R + 6}" y="${T - 3}" style="fill:#7ee787">PS</text>
              <text x="${(W + L - R) / 2}" y="${H - 4}" text-anchor="middle">U/min</text>`;

        for (const { r, pts: pp } of all) {
            const dim = hoverRun && r !== hoverRun, focus = hoverRun && r === hoverRun;
            s += `<g opacity="${dim ? 0.1 : 1}">`;
            const d = f => pp.map((p, i) => (i ? 'L' : 'M') + `${X(p.rpm).toFixed(1)} ${f(p).toFixed(1)}`).join(' ');
            s += `<path d="${d(p => Yn(p.nm))}" fill="none" stroke="${r.color}" stroke-width="${focus ? 3.4 : 2.2}"${r.live ? ' stroke-dasharray="2 3"' : ''}/>
                  <path d="${d(p => Yp(PS(p.kw)))}" fill="none" stroke="${r.color}" stroke-width="${focus ? 2.4 : 1.6}" stroke-dasharray="6 4" opacity=".85"/>`;
            if (r.snap && r.snap.p.camSwitch === 'on') {          // switching point of the second cam profile
                const sx = X(r.snap.p.camSwitchRpm);
                if (sx >= L && sx <= W - R) s += `<line x1="${sx}" x2="${sx}" y1="${T}" y2="${H - B}" stroke="${r.color}" stroke-width="1.5" stroke-dasharray="2 4" opacity=".8"/><text x="${sx + 3}" y="${T + 10}" style="fill:${r.color};font-size:10px">Nocken</text>`;
            }
            for (const p of pp) {                                  // knock: red dots on the torque curve
                if (p.knock >= 0.1) s += `<circle cx="${X(p.rpm)}" cy="${Yn(p.nm)}" r="${(1.8 + 2.4 * Math.min(p.knock, 1)).toFixed(1)}" fill="#ff3b30" opacity=".55"/>`;
            }
            if (!r.live && pp.length) {
                const pn = peak(pp, 'nm'), pk = pp.reduce((b, p) => (p.kw > b.kw ? p : b), pp[0]);
                s += `<circle cx="${X(pn.rpm)}" cy="${Yn(pn.nm)}" r="4" fill="${r.color}"/><circle cx="${X(pk.rpm)}" cy="${Yp(PS(pk.kw))}" r="4" fill="none" stroke="${r.color}" stroke-width="2"/>`;
            }
            s += `</g>`;
        }
        $('d-chart').innerHTML = s + `</svg>`;
    }

    /* ------------------------------------------------------------ list of runs */

    function knockText(r) {
        const first = r.points.find(p => p.knock >= 0.1);
        return first ? `<small class="chg" style="color:#ff7b72">klopft ab ${Math.round(first.rpm)} U/min</small>` : '';
    }

    function renderRuns() {
        $('d-runs').innerHTML = (runs.length ? '' : `<div class="note">Jeder Messlauf wird hier gespeichert. Ändere etwas und miss erneut, um die Auswirkung zu sehen.</div>`)
            + runs.map((r, i) => {
                const pn = peak(r.points, 'nm'), pk = r.points.reduce((b, p) => (p.kw > b.kw ? p : b), r.points[0]);
                return `<div class="run" data-i="${i}">
                    <span class="sw" data-act="vis" style="background:${r.visible ? r.color : 'transparent'};border:2px solid ${r.color}" title="ein-/ausblenden"></span>
                    <div><input class="nm" data-act="name" value="${r.name.replace(/"/g, '&quot;')}">
                        <small class="info">max ${pn.nm.toFixed(0)} Nm @ ${pn.rpm.toFixed(0)} · ${PS(pk.kw).toFixed(0)} PS @ ${pk.rpm.toFixed(0)}</small>${knockText(r)}
                        ${r.changes && r.changes.length ? `<small class="chg">${r.changes.join('<br>')}</small>` : ''}</div>
                    <button class="x" data-act="del" title="Lauf löschen">✕</button></div>`;
            }).join('')
            + (runs.length ? `<div class="row"><button id="d-csv">CSV exportieren</button><button id="d-clear">Alle löschen</button></div>` : '');
    }

    $('d-runs').addEventListener('click', ev => {
        const row = ev.target.closest('.run'), act = ev.target.dataset && ev.target.dataset.act;
        if (row && act === 'vis') { const r = runs[+row.dataset.i]; r.visible = !r.visible; }
        else if (row && act === 'del') { runs.splice(+row.dataset.i, 1); hoverRun = null; }
        else if (ev.target.id === 'd-clear') { runs = []; hoverRun = null; }
        else if (ev.target.id === 'd-csv') {
            const lines = ['lauf;rpm;Nm;kW;PS;MAP_kPa;zuendwinkel_deg'];
            runs.filter(r => r.visible).forEach(r => r.points.forEach(p =>
                lines.push([r.name, p.rpm, p.nm.toFixed(1), p.kw.toFixed(2), PS(p.kw).toFixed(1), p.map.toFixed(1), p.adv.toFixed(1)].join(';'))));
            const a = document.createElement('a');
            a.href = URL.createObjectURL(new Blob([lines.join('\n')], { type: 'text/csv' })); a.download = 'prueflauf.csv'; a.click();
            return;
        } else return;
        saveRuns(); renderRuns(); drawDyno();
    });

    // while the mouse is over a run, the others fade out
    function setHover(run) { if (hoverRun !== run) { hoverRun = run; drawDyno(); } }
    $('d-runs').addEventListener('mouseover', ev => {
        const row = ev.target.closest('.run');
        const r = row ? runs[+row.dataset.i] : null;
        setHover(r && r.visible ? r : null);
    });
    $('d-runs').addEventListener('mouseleave', () => setHover(null));
    $('d-runs').addEventListener('input', ev => {
        if (ev.target.dataset.act === 'name') { runs[+ev.target.closest('.run').dataset.i].name = ev.target.value; saveRuns(); }
    });

    /* ------------------------------------------------------------ measured runs */

    // a damaged engine burns worse and would spoil the comparison: repair before every measurement (can be switched off)
    try { $('d-repair').checked = localStorage.getItem('engineTunerAutoRepair') !== '0'; } catch (e) { /* ignore */ }
    $('d-repair').addEventListener('change', () => { try { localStorage.setItem('engineTunerAutoRepair', $('d-repair').checked ? '1' : '0'); } catch (e) { /* ignore */ } });
    const repairFirst = async () => { if ($('d-repair').checked) await api.post('/tune', 'repair 1'); };

    const setState = (text, color) => { $('d-state').textContent = text; $('d-state').style.color = color || '#8b95a1'; };

    async function poll() {
        try {
            const st = await api.get('/dyno');
            livePoints = st.points || [];
            $('d-bar').style.display = 'block'; $('d-bar').firstElementChild.style.width = Math.round((st.progress || 0) * 100) + '%';
            if (st.state === 'starting' || st.state === 'running') {
                setState((st.state === 'starting' ? 'Motor wird gestartet …' : `misst bei ${Math.round(st.rpm)} U/min …`)
                    + (st.damage > 0.02 ? ` · Motorschaden ${Math.round(st.damage * 100)} %` : ''), st.damage > 0.5 ? '#ff7b72' : undefined);
                drawDyno(); setTimeout(poll, 400); return;
            }

            running = false; $('d-bar').style.display = 'none';
            if (st.state === 'done' && livePoints.length) {
                const snap = { ...hooks.getSnapshot(), tune: await hooks.getTune() };
                const prev = runs[runs.length - 1];
                const color = RUN_COLORS.find(c => !runs.some(r => r.color === c)) || RUN_COLORS[runs.length % RUN_COLORS.length];
                runs.push({ name: `Lauf ${runs.length + 1} (${runMode}) · ${new Date().toLocaleTimeString().slice(0, 5)}`, color, visible: true, points: livePoints, snap, changes: diffSnapshots(prev && prev.snap, snap) });
                saveRuns();
                const bad = livePoints.filter(p => !p.held).length;
                setState(bad ? `fertig · ${bad} Punkt(e) ohne stabile Drehzahl` : 'fertig', bad ? '#e3b341' : '#7ee787');
            } else setState(st.state === 'aborted' ? `abgebrochen: ${st.message}` : '', '#e3b341');
            livePoints = []; renderRuns(); drawDyno(); updateButtons();
        } catch (e) { running = false; $('d-bar').style.display = 'none'; setState('Verbindung verloren', '#ff7b72'); updateButtons(); }
    }

    $('d-run').addEventListener('click', async () => {
        const throttle = Math.min(100, Math.max(0, parseFloat($('d-thr').value))) / 100;
        const ramp = $('d-mode').value === 'ramp';
        runMode = ramp ? 'Zug' : 'Stufen';
        const body = ramp
            ? `mode=ramp\nstart=${$('d-start').value}\nend=0\nrate=${$('d-rate').value}\nthrottle=${throttle}`     // end=0: up to the rev limiter
            : `mode=steps\nstart=${$('d-start').value}\nend=${$('d-end').value}\nstep=${$('d-step').value}\nthrottle=${throttle}`;
        try {
            await repairFirst();
            const answer = await api.post('/dyno', body);
            if (!answer.ok) { setState(answer.error, '#ff7b72'); return; }
            running = true; livePoints = []; setState('startet …'); updateButtons(); poll();
        } catch (e) { setState('Simulator nicht erreichbar', '#ff7b72'); }
    });
    $('d-stop').addEventListener('click', () => api.post('/dyno/stop', 'x'));

    $('d-mode').addEventListener('change', () => {           // the pull needs no end speed or step, the steps mode no rate
        const ramp = $('d-mode').value === 'ramp';
        $('d-steps-only').style.display = ramp ? 'none' : 'contents';
        $('d-ramp-only').style.display = ramp ? 'contents' : 'none';
    });

    /* ------------------------------------------------------------ speed hold */

    const holdActive = () => !!tele && tele.dyno_mode === 'hold' && (tele.dyno_state === 'starting' || tele.dyno_state === 'running');
    let ema = null;                         // smoothed torque for the readout
    let bins = new Map();                   // advance (0.5 deg) -> { sum, n } at the held speed
    let binsKey = '';

    function updateButtons() {
        const busy = running;
        $('d-run').disabled = busy || holdActive();
        $('d-stop').disabled = !busy;
        $('h-start').disabled = busy;
        $('h-start').textContent = holdActive() ? 'Halten beenden' : 'Halten starten';
        $('h-start').className = holdActive() ? 'primary' : '';
    }

    function holdParams() { return `rpm=${$('h-rpm').value}\nthrottle=${Math.min(100, Math.max(0, +$('h-thr').value)) / 100}`; }
    function resetBins() { bins = new Map(); }

    $('h-start').addEventListener('click', async () => {
        if (holdActive()) { await api.post('/dyno/stop', 'x'); return; }
        resetBins();
        await repairFirst();
        const answer = await api.post('/dyno', `mode=hold\nstart=${$('h-rpm').value}\nthrottle=${Math.min(100, Math.max(0, +$('h-thr').value)) / 100}`);
        $('h-state').textContent = answer.ok ? 'Motor wird gestartet …' : answer.error;
    });

    // speed and throttle can be changed while the hold runs
    const holdChanged = () => { resetBins(); if (holdActive()) soon('hold', () => api.post('/dyno/hold', holdParams()), 150); };
    $('h-rpm').addEventListener('input', () => { $('h-rpm-r').value = $('h-rpm').value; holdChanged(); });
    $('h-rpm-r').addEventListener('input', () => { $('h-rpm').value = $('h-rpm-r').value; holdChanged(); });
    $('h-thr').addEventListener('input', holdChanged);

    $('h-reset').addEventListener('click', resetBins);

    function drawAdvanceChart() {
        const W = 520, H = 230, L = 44, R = 12, T = 12, B = 30;
        const pts = [...bins.entries()].map(([a, b]) => ({ adv: a / 2, nm: b.sum / b.n })).sort((p, q) => p.adv - q.adv);
        let s = `<svg viewBox="0 0 ${W} ${H}" width="100%">`;
        if (pts.length < 2) {
            $('h-chart').innerHTML = s + `<text x="${W / 2}" y="${H / 2}" text-anchor="middle" style="fill:#6f7a86;font-size:12px">Verstelle den Versatz – hier entsteht die Kurve Drehmoment über Zündwinkel</text></svg>`;
            return;
        }

        const a0 = Math.floor(pts[0].adv / 5) * 5 - 5, a1 = Math.ceil(pts[pts.length - 1].adv / 5) * 5 + 5;
        const lo = Math.min(...pts.map(p => p.nm)), hi = Math.max(...pts.map(p => p.nm));
        const n0 = Math.floor((lo - (hi - lo) * 0.15 - 1) / 10) * 10, n1 = Math.ceil((hi + (hi - lo) * 0.15 + 1) / 10) * 10;
        const X = v => L + (v - a0) / (a1 - a0) * (W - L - R), Y = v => T + (1 - (v - n0) / (n1 - n0)) * (H - T - B);

        for (let i = 0; i <= 4; i++) {
            const v = n0 + (n1 - n0) * i / 4;
            s += `<line x1="${L}" x2="${W - R}" y1="${Y(v)}" y2="${Y(v)}" stroke="#262b31"/><text x="${L - 6}" y="${Y(v) + 4}" text-anchor="end">${Math.round(v)}</text>`;
        }
        for (let a = a0; a <= a1; a += 5) s += `<line x1="${X(a)}" x2="${X(a)}" y1="${T}" y2="${H - B}" stroke="#20252a"/><text x="${X(a)}" y="${H - B + 14}" text-anchor="middle">${a}°</text>`;
        s += `<text x="${(W + L) / 2}" y="${H - 2}" text-anchor="middle">Zündwinkel (° vor OT)</text><text x="${L - 6}" y="${T - 2}" text-anchor="end" style="fill:#e5b83d">Nm</text>`;

        s += `<path d="${pts.map((p, i) => (i ? 'L' : 'M') + X(p.adv).toFixed(1) + ' ' + Y(p.nm).toFixed(1)).join(' ')}" fill="none" stroke="#f4802a" stroke-width="2"/>`;
        pts.forEach(p => { s += `<circle cx="${X(p.adv)}" cy="${Y(p.nm)}" r="2.4" fill="#f4802a"/>`; });
        const best = pts.reduce((b, p) => (p.nm > b.nm ? p : b), pts[0]);
        s += `<circle cx="${X(best.adv)}" cy="${Y(best.nm)}" r="6" fill="none" stroke="#bdd869" stroke-width="2"/>`;
        if (tele) s += `<line x1="${X(tele.advance_deg)}" x2="${X(tele.advance_deg)}" y1="${T}" y2="${H - B}" stroke="#fff" stroke-dasharray="3 3"/>`;
        $('h-chart').innerHTML = s + `</svg>`;
        $('h-best').textContent = `Bisher am besten: ${best.nm.toFixed(0)} Nm bei ${best.adv.toFixed(1)}°`;
    }

    let lastDraw = 0, lastText = 0;
    function onTelemetry(d) {
        tele = d;
        if (!d.engine) return;

        const active = holdActive();
        updateButtons();

        if (active && d.dyno_state === 'running') {
            const target = +$('h-rpm').value;
            const steady = Math.abs(d.rpm - target) / target < 0.03;
            ema = ema === null ? d.torque_nm : ema * 0.95 + d.torque_nm * 0.05;
            if (performance.now() - lastText > 250) {                // the numbers refresh ~4 times a second
                lastText = performance.now();
                $('h-nm').textContent = Math.round(ema);
                $('h-ps').textContent = Math.round(ema * d.rpm * Math.PI / 30 / 735.49875);
                $('h-adv').textContent = d.advance_deg.toFixed(1);
            }
            $('h-state').textContent = steady ? `hält ${Math.round(d.rpm)} U/min` : `Drehzahl wird angefahren (${Math.round(d.rpm)}) …`;

            if (steady) {
                const key = Math.round(target) + '|' + $('h-thr').value;
                if (key !== binsKey) { binsKey = key; resetBins(); }
                const bin = Math.round(d.advance_deg * 2);
                const entry = bins.get(bin) || { sum: 0, n: 0 };
                if (entry.n >= 40) { entry.sum *= 0.975; entry.n *= 0.975; }        // old samples fade, the curve follows changes
                entry.sum += d.torque_nm; entry.n += 1;
                bins.set(bin, entry);
            }
        } else if (active) {
            $('h-state').textContent = 'Motor wird gestartet …';
        } else if (!running) {
            ema = null;
            if ($('h-state').textContent.startsWith('hält') || $('h-state').textContent.startsWith('Drehzahl')) $('h-state').textContent = 'Halten beendet.';
        }

        if (performance.now() - lastDraw > 300) { lastDraw = performance.now(); drawAdvanceChart(); }
    }

    renderRuns(); drawDyno(); drawAdvanceChart(); updateButtons();
    return { onTelemetry };
}
