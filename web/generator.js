// Engine generator of the Engine-Sim-Tuner: parameters + arrangement -> engine script (.mr).
// Extracted from tools/engine-builder.html. Pure functions, no DOM.
//
//   generate(p, cfg, tuning?)   p: parameter object (see SECTIONS), cfg: { banks, cyls } (see autoLayout)
//                               tuning: values read from GET /tune, they replace the derived live header lines

/* ============================================================
   Parameter fields (everything except the cylinder arrangement)
   [key, label, hint, default, type]   type: n = number, t = text, array = select
   ============================================================ */
export const SECTIONS = [
    ["Allgemein", [
        ["name", "Name", "", "My Engine", "t"],
    ]],
    ["Hubraum", [
        ["bore", "Bohrung (mm)", "", 86, "n"],
        ["stroke", "Hub (mm)", "", 86, "n"],
        ["rodLength", "Pleuellänge (mm)", "", 142, "n"],
        ["compHeight", "Kompressionshöhe (mm)", "Kolbenbolzen bis Kolbenboden", 32.8, "n"],
        ["chamber", "Brennraumvolumen (cc)", "Restvolumen im oberen Totpunkt", 50, "n"],
        ["compression", "Verdichtung (:1)", "(Hubraum/Zyl. + Brennraum) / Brennraum - Eingabe ändert das Brennraumvolumen", 11, "n"],
    ]],
    ["Massen", [
        ["pistonMass", "Kolben + Bolzen (g)", "", 250, "n"],
        ["rodMass", "Pleuel (g)", "", 500, "n"],
        ["crankMass", "Kurbelwelle (kg)", "", 15, "n"],
        ["flywheelMass", "Schwungrad (kg)", "", 10, "n"],
        ["flywheelRadius", "Schwungrad-Radius (mm)", "", 178, "n"],
        ["friction", "Reibmoment (Nm)", "", 8, "n"],
    ]],
    ["Nockenwelle", [
        ["intakeDuration", "Einlass-Dauer @0.05\" (°)", "", 222, "n"],
        ["exhaustDuration", "Auslass-Dauer @0.05\" (°)", "", 226, "n"],
        ["intakeLift", "Einlass-Hub (mm)", "", 9.8, "n"],
        ["exhaustLift", "Auslass-Hub (mm)", "", 9.6, "n"],
        ["intakeCenter", "Einlass-Mittellinie (°)", "", 116, "n"],
        ["exhaustCenter", "Auslass-Mittellinie (°)", "", 112, "n"],
    ]],
    ["Nockenumschaltung (VTEC)", [
        ["camSwitch", "Zweites Nockenprofil", "wie VTEC: ab der Umschaltdrehzahl wirkt das hohe Profil", "off", ["off:aus", "on:an"]],
        ["camSwitchRpm", "Umschaltdrehzahl (U/min)", "hohes Profil ab dieser Drehzahl", 5000, "n"],
        ["camHysteresis", "Hysterese (U/min)", "zurück erst unterhalb Umschaltdrehzahl minus Hysterese", 300, "n"],
        ["camMinThrottle", "Mindest-Gaspedal (%)", "darunter bleibt das niedrige Profil", 30, "n"],
        ["camMinMap", "Mindest-Saugrohrdruck (kPa)", "darunter bleibt das niedrige Profil", 60, "n"],
        ["highIntakeLift", "Einlass-Hub hoch (mm)", "", 11.5, "n"],
        ["highExhaustLift", "Auslass-Hub hoch (mm)", "", 10.5, "n"],
        ["highIntakeDuration", "Einlass-Dauer hoch @0.05\" (°)", "", 240, "n"],
        ["highExhaustDuration", "Auslass-Dauer hoch @0.05\" (°)", "", 232, "n"],
        ["highIntakeCenter", "Einlass-Mittellinie hoch (°)", "", 110, "n"],
        ["highExhaustCenter", "Auslass-Mittellinie hoch (°)", "", 110, "n"],
    ]],
    ["Ansaugung / Abgas", [
        ["carbCfm", "Drosselklappe (CFM)", "", 500, "n"],
        ["runnerLength", "Saugrohr-Länge (mm)", "", 150, "n"],
        ["idlePlate", "Leerlauf-Drosselstellung", "0..1, näher an 1 = weniger Gas", 0.994, "n"],
        ["exhaustMode", "Abgasanlagen", "", "bank", ["bank:eine pro Bank", "one:eine für alle", "cyl:eine pro Zylinder"]],
        ["primaryLength", "Fächerkrümmer-Länge (mm)", "", 500, "n"],
        ["exhaustLength", "Auspuff-Länge (mm)", "", 2500, "n"],
        ["exhaustIr", "Klang", "", "mild_exhaust_0_reverb", ["mild_exhaust_0_reverb:Mild", "mild_exhaust_0:Mild (trocken)", "sharp_0:Scharf", "minimal_muffling_01:Offen 1", "minimal_muffling_02:Offen 2", "minimal_muffling_03:Offen 3", "real_engine_0:Real 0", "real_engine_1:Real 1", "real_engine_2:Real 2"]],
        ["audioVolume", "Lautstärke", "", 0.5, "n"],
    ]],
    ["Kraftstoff / Klopfen", [
        ["fuel", "Kraftstoff", "setzt Oktanzahl und Ethanol-Anteil", "super95", ["super95:Super 95", "super98:Super Plus 98", "super100:Super 100", "e10:E10", "e20:E20", "e85:E85", "custom:Eigener Wert"]],
        ["octane", "Oktanzahl (ROZ)", "bestimmt, wie früh das Restgas klopft", 95, "n"],
        ["ethanol", "Ethanol-Anteil (%)", "ändert Brennwert und Luftbedarf", 0, "n"],
        ["knockModel", "Klopfen simulieren", "", "on", ["on:an", "off:aus"]],
        ["knockDamage", "Motorschaden durch Klopfen", "", "on", ["on:an", "off:aus"]],
    ]],
    ["Zündung / Drehzahl", [
        ["redline", "Redline (U/min)", "", 6500, "n"],
        ["revLimit", "Drehzahlbegrenzer (U/min)", "", 7000, "n"],
        ["advanceIdle", "Zündwinkel Leerlauf (°)", "", 12, "n"],
        ["advanceMax", "Zündwinkel max (°)", "", 34, "n"],
        ["starterTorque", "Anlasser-Moment (Nm)", "", 200, "n"],
    ]],
    ["Fahrzeug", [
        ["carMass", "Masse (kg)", "", 1500, "n"],
        ["drag", "cw-Wert", "", 0.32, "n"],
        ["frontalArea", "Stirnfläche (m²)", "", 2.1, "n"],
        ["diff", "Hinterachs-Übersetzung", "", 3.4, "n"],
        ["tire", "Reifenradius (mm)", "", 310, "n"],
        ["gears", "Gänge", "Komma-getrennt", "3.6, 2.2, 1.5, 1.15, 0.9, 0.75", "t"],
        ["clutch", "Max. Kupplungsmoment (Nm)", "", 500, "n"],
    ]],
];

// Parameters the simulator re-reads every step: changing them in live mode patches the running engine
// without a restart (see EngineSimApplication::applyLiveTuning). Everything else needs a full reload.
export const LIVE_KEYS = ["idlePlate", "revLimit", "advanceIdle", "advanceMax", "starterTorque", "friction", "octane", "knockModel", "knockDamage",
    "intakeDuration", "exhaustDuration", "intakeLift", "exhaustLift",
    "highIntakeLift", "highExhaustLift", "highIntakeDuration", "highExhaustDuration",
    "camSwitchRpm", "camHysteresis", "camMinThrottle", "camMinMap"];

// Fuels: typical research octane numbers (RON) of the market grades and the ethanol share by volume.
// E10/E20/E85 values are approximations, they can be changed in the form ("Eigener Wert").
export const FUELS = {
    super95: { ron: 95, ethanol: 0 },
    super98: { ron: 98, ethanol: 0 },
    super100: { ron: 100, ethanol: 0 },
    e10: { ron: 95, ethanol: 10 },
    e20: { ron: 98, ethanol: 20 },
    e85: { ron: 105, ethanol: 85 },
};

// Energy content and fuel mass of a gasoline/ethanol blend (ethanol: 0.789 kg/L, 62 % of the energy per kg,
// 60 % of the oxygen demand per kg). The combustion model always burns with the stoichiometry of octane
// (12.5 mol O2 per mol fuel), so the blend is expressed as a pseudo fuel with that oxygen demand:
// the "mole" is heavier by the factor by which the blend needs more fuel mass per oxygen, and the energy
// density is that of the blend. Result: energy per air about the same as gasoline, fuel consumption higher.
export function fuelProps(ethanolPercent) {
    const v = Math.min(Math.max(ethanolPercent || 0, 0), 100) / 100;
    const rhoE = 0.789, rhoG = 0.755;
    const massE = v > 0 ? rhoE * v / (rhoE * v + rhoG * (1 - v)) : 0, massG = 1 - massE;
    const oxygenPerKg = massG + massE * 0.595;                 // oxygen demand per kg of fuel, relative to gasoline
    return { energy: 48.1 * (massG + massE * 0.616), density: rhoG * (1 - v) + rhoE * v, molarMass: 100 / oxygenPerKg };
}


// Fields that drive the automatic arrangement generator
export const AUTO_DEFAULTS = { layout: "inline", cylinders: 6, angle: 60, firing: "1-5-3-6-2-4", singlePin: false };

export const PRESETS = {
    "Reihen-6 (Supra-ähnlich)": {},
    "Reihen-4": { name: "Inline 4", layout: "inline", cylinders: 4, firing: "1-3-4-2", bore: 86, stroke: 86, rodLength: 143, redline: 7000, revLimit: 7500, carbCfm: 400, advanceMax: 32 },
    "Reihen-3": { name: "Inline 3", layout: "inline", cylinders: 3, firing: "1-2-3", bore: 74, stroke: 76, rodLength: 130, compHeight: 30, chamber: 40, pistonMass: 300, rodMass: 380, crankMass: 8, redline: 7000, revLimit: 7500, carbCfm: 300 },
    "V6 60°": { name: "V6 60", layout: "v", angle: 60, cylinders: 6, firing: "1-2-3-4-5-6", bore: 88.9, stroke: 88.4, rodLength: 130.6, compHeight: 25.4, chamber: 67, redline: 5500, revLimit: 5600, carbCfm: 400 },
    "V8 90° (LS-ähnlich)": { name: "V8 90", layout: "v", angle: 90, cylinders: 8, firing: "1-8-7-2-6-5-4-3", bore: 99, stroke: 92, rodLength: 155, compHeight: 33, chamber: 60, pistonMass: 480, rodMass: 600, crankMass: 20, redline: 6500, revLimit: 6800, carbCfm: 650, intakeDuration: 230, exhaustDuration: 236 },
    "Reihen-4 VTEC (Honda-ähnlich)": { name: "VTEC I4", layout: "inline", cylinders: 4, firing: "1-3-4-2", bore: 81, stroke: 87.2, rodLength: 138, compHeight: 25.4, chamber: 41.6, redline: 8400, revLimit: 9000, carbCfm: 650, runnerLength: 178, primaryLength: 254, intakeLift: 6.9, exhaustLift: 6.5, intakeDuration: 210, exhaustDuration: 190, intakeCenter: 114, exhaustCenter: 114, camSwitch: "on", camSwitchRpm: 6200, camHysteresis: 300, advanceMax: 24 },
    "Boxer-4 (EJ-ähnlich)": { name: "Flat 4", layout: "flat", cylinders: 4, firing: "1-3-2-4", bore: 99.5, stroke: 79, rodLength: 130.5, compHeight: 33, chamber: 55, redline: 6500, revLimit: 6700, carbCfm: 400, crankMass: 12 },
    "W12 (3 Bänke)": { name: "W12", layout: "w", angle: 20, cylinders: 12, firing: "1-2-3-4-5-6-7-8-9-10-11-12", bore: 84, stroke: 90, rodLength: 154, chamber: 50, redline: 6500, revLimit: 6800, carbCfm: 800, exhaustMode: "bank" },
    "V-Twin 45° (Einzelzapfen)": { name: "V-Twin", layout: "v", angle: 45, cylinders: 2, firing: "1-2", singlePin: true, bore: 98, stroke: 100, rodLength: 160, compHeight: 35, chamber: 70, pistonMass: 400, rodMass: 500, crankMass: 10, flywheelMass: 8, redline: 6500, revLimit: 6800, carbCfm: 250, starterTorque: 80 },
    "Sternmotor 5": { name: "Radial 5", layout: "radial", cylinders: 5, firing: "1-3-5-2-4", singlePin: true, bore: 129, stroke: 140, rodLength: 260, compHeight: 50, chamber: 280, pistonMass: 1500, rodMass: 1000, crankMass: 20, flywheelMass: 10, flywheelRadius: 200, redline: 2800, revLimit: 3000, carbCfm: 600, intakeDuration: 240, exhaustDuration: 240, intakeCenter: 114, exhaustCenter: 114, exhaustMode: "one", starterTorque: 300 },
};

/* ============================================================
   Model: cfg = { banks: [angle°, ...], cyls: [{ bank, pin, rev }, ...] }
     bank  - index into banks
     pin   - crank pin angle (0..360), same sense as bank angles
     rev   - 0/1: fires in the first / second crank revolution of the 720° cycle
   Firing angle of a cylinder: mod(pin - (bankAngle - bank0Angle), 360) + 360 * rev
   (this reproduces the pin/bank relations of the existing engines in assets/engines)
   ============================================================ */
export const mod = (a, m) => ((a % m) + m) % m;
export const r4 = x => Math.round(x * 10000) / 10000;
export const f = x => { const s = String(r4(x)); return /[.e]/.test(s) ? s : s + ".0"; };
export const circ = (a, b) => { const d = Math.abs(a - b) % 720; return Math.min(d, 720 - d); };

export function bankDelta(cfg, cyl) { return cfg.banks[cyl.bank] - cfg.banks[0]; }
export function fireOf(cfg, cyl) {
    let x = mod(cyl.pin - bankDelta(cfg, cyl), 360);
    if (x > 360 - 1e-4) x = 0;
    return x + 360 * (cyl.rev ? 1 : 0);
}

export function parseFiring(text, n) {
    const order = String(text).split(/[^0-9]+/).filter(Boolean).map(Number);
    const errors = [];
    if (order.length !== n) errors.push(`Zündfolge hat ${order.length} Einträge, erwartet ${n}.`);
    const seen = new Set(order);
    for (let c = 1; c <= n; c++) if (!seen.has(c)) errors.push(`Zylinder ${c} fehlt in der Zündfolge.`);
    if (seen.size !== order.length) errors.push("Zündfolge enthält doppelte Zylinder.");
    if (order.some(c => c < 1 || c > n)) errors.push(`Zündfolge enthält Zylinder außerhalb 1..${n}.`);
    return { order, errors };
}

export function defaultFiring(layout, n) {
    const known = { inline: { 3: "1-2-3", 4: "1-3-4-2", 5: "1-2-4-5-3", 6: "1-5-3-6-2-4" },
        v: { 2: "1-2", 6: "1-2-3-4-5-6", 8: "1-8-7-2-6-5-4-3" }, flat: { 2: "1-2", 4: "1-3-2-4" } };
    if (known[layout] && known[layout][n]) return known[layout][n];
    if (layout === "radial" && n % 2) return Array.from({ length: n }, (_, i) => 1 + (2 * i) % n).join("-");
    return Array.from({ length: Math.max(0, n) }, (_, i) => i + 1).join("-");
}

// Build banks + cylinders from a layout, a cylinder count and a firing order (even-fire spacing)
export function autoLayout(o) {
    const n = Math.round(o.cylinders);
    const errors = [];
    if (!(n >= 1 && n <= 24)) return { cfg: { banks: [0], cyls: [] }, errors: ["Zylinderzahl muss zwischen 1 und 24 liegen."] };
    const banks = o.layout === "v" ? [o.angle / 2, -o.angle / 2]
        : o.layout === "flat" ? [90, -90]
        : o.layout === "w" ? [o.angle, 0, -o.angle]
        : o.layout === "radial" ? Array.from({ length: n }, (_, i) => -360 * i / n)
        : [0];
    const { order, errors: fe } = parseFiring(o.firing, n);
    errors.push(...fe);
    const idx = {};
    (fe.length ? Array.from({ length: n }, (_, i) => i + 1) : order).forEach((c, i) => { idx[c] = i; });
    const cyls = [];
    for (let c = 1; c <= n; c++) {
        const bank = o.layout === "radial" ? c - 1 : (c - 1) % banks.length;
        const T = idx[c] * 720 / n;
        const d = banks[bank] - banks[0];
        let pin, rev;
        if (o.singlePin) {                                   // all rods on one pin, timing follows from the bank geometry
            const f0 = mod(-d, 360), f1 = f0 + 360;
            pin = 0; rev = circ(f1, T) < circ(f0, T) ? 1 : 0;
        } else {
            pin = r4(mod(T + d, 360)); if (pin >= 360) pin = 0;
            rev = T >= 360 - 1e-9 ? 1 : 0;
        }
        cyls.push({ bank, pin, rev });
    }
    return { cfg: { banks, cyls }, errors };
}

export function analyze(p, cfg) {
    const n = cfg.cyls.length;
    const errors = [], warnings = [];
    if (n < 1) errors.push("Mindestens ein Zylinder nötig.");
    if (n > 24) errors.push("Maximal 24 Zylinder.");
    cfg.banks.forEach((_, b) => { if (!cfg.cyls.some(c => c.bank === b)) errors.push(`Bank ${b + 1} hat keine Zylinder.`); });
    for (const k of ["bore", "stroke", "rodLength", "chamber", "compHeight"]) if (!(p[k] > 0)) errors.push(`${k} muss > 0 sein.`);
    if (p.rodLength < p.stroke / 2 + 10) warnings.push("Pleuel sehr kurz im Verhältnis zum Hub.");
    if (p.revLimit < p.redline) warnings.push("Drehzahlbegrenzer liegt unter der Redline.");
    const camOk = (lift, dur) => lift >= 3 && lift <= 22 && dur >= 150 && dur <= 330;
    if (!camOk(p.intakeLift, p.intakeDuration) || !camOk(p.exhaustLift, p.exhaustDuration)) errors.push("Nockenwelle: Hub 3…22 mm und Dauer 150…330° erlaubt.");
    if (p.camSwitch === "on") {
        if (!camOk(p.highIntakeLift, p.highIntakeDuration) || !camOk(p.highExhaustLift, p.highExhaustDuration)) errors.push("Hohes Nockenprofil: Hub 3…22 mm und Dauer 150…330° erlaubt.");
        if (!(p.camSwitchRpm >= 500 && p.camSwitchRpm <= 20000) || !(p.camHysteresis >= 0)) errors.push("Umschaltdrehzahl und Hysterese ungültig.");
        if (p.camSwitchRpm > p.redline) warnings.push("Die Umschaltdrehzahl liegt über der Redline - das hohe Profil wird nie genutzt.");
        if (p.highIntakeLift < p.intakeLift) warnings.push("Das hohe Profil hat weniger Einlass-Hub als das niedrige.");
    }
    if (p.knockModel === "on" && !(p.octane >= 60 && p.octane <= 130)) errors.push("Oktanzahl muss zwischen 60 und 130 liegen.");
    if (!(p.ethanol >= 0 && p.ethanol <= 100)) errors.push("Ethanol-Anteil muss zwischen 0 und 100 % liegen.");
    const igm = p.ignMap;
    if (igm && igm.on) {
        const ascending = arr => arr.length > 0 && arr.every((v, i) => Number.isFinite(v) && (i === 0 || v > arr[i - 1]));
        if (!ascending(igm.rpm) || !ascending(igm.load)) errors.push("Zündkennfeld: Drehzahl- und Lastachse müssen aufsteigend sein.");
        else if (igm.adv.some(row => row.length !== igm.load.length || row.some(v => !Number.isFinite(v)))) errors.push("Zündkennfeld enthält ungültige Werte.");
        else if (igm.adv.flat().some(v => v < -10 || v > 90)) warnings.push("Zündkennfeld: Werte außerhalb von -10…90° vor OT.");
    }
    const vdCyl = Math.PI / 4 * p.bore * p.bore * p.stroke / 1000;      // cc
    const cr = (vdCyl + p.chamber) / p.chamber;
    if (cr > 14) warnings.push("Verdichtung über 14:1 - für Benziner unrealistisch.");
    if (cr < 6) warnings.push("Verdichtung unter 6:1 - Motor läuft evtl. nicht.");

    const fires = cfg.cyls.map(c => fireOf(cfg, c));
    const sorted = fires.map((a, i) => ({ a, c: i + 1 })).sort((x, y) => x.a - y.a || x.c - y.c);
    const intervals = sorted.map((s, i) => (i + 1 < sorted.length ? sorted[i + 1].a : sorted[0].a + 720) - s.a);
    const even = intervals.length > 0 && Math.max(...intervals) - Math.min(...intervals) < 0.5;
    if (sorted.length > 1 && !even) warnings.push("Ungleichmäßige Zündabstände (gewollt? - klingt dann „wild“).");
    return { n, errors, warnings, fires, order: sorted.map(s => s.c), intervals, even,
        vdCyl, displacement: vdCyl * n / 1000, cr };
}

export function ident(name) {
    const s = String(name).toLowerCase().replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
    return "custom_" + (s || "engine");
}

export function fnv1a(str) {
    let x = 0x811c9dc5;
    for (let i = 0; i < str.length; i++) { x ^= str.charCodeAt(i); x = Math.imul(x, 0x01000193) >>> 0; }
    return x.toString(16).padStart(8, "0");
}

// Live values that are edited in the tuning views. When the simulator already holds a value, it is kept.
const TUNE_SCALARS = ["timing_curve", "rev_limit_rpm", "idle_throttle_plate_position", "cam_center"];
const TUNE_MAP = ["map_rpm", "map_load", "map_adv"];

function applyTuning(header, tuning) {
    if (!tuning) return header;
    const keyOf = line => (line.match(/^\/\/ builder:live (\S+)/) || [])[1];
    const out = header.filter(line => !TUNE_MAP.includes(keyOf(line)));
    TUNE_SCALARS.forEach(key => {
        if (tuning[key] === undefined) return;
        const i = out.findIndex(line => keyOf(line) === key);
        const text = `// builder:live ${key} ${tuning[key]}`;
        if (i >= 0) out[i] = text; else out.push(text);
    });
    TUNE_MAP.forEach(key => { if (tuning[key] !== undefined) out.push(`// builder:live ${key} ${tuning[key]}`); });
    return out;
}

export function generate(p, cfg, tuning = null) {
    const body = generateBody(p, cfg);
    const neutral = { ...p };
    LIVE_KEYS.forEach(k => { neutral[k] = 0; });
    const structure = fnv1a(generateBody(neutral, cfg).text);

    const curve = [];
    for (let r = 0; r <= Math.max(p.redline, 1000); r += 1000) {
        curve.push(`${r}:${f(p.advanceIdle + (p.advanceMax - p.advanceIdle) * Math.min(1, r / 4000))}`);
    }
    const header = [
        `// builder:structure ${structure}`,
        `// builder:live idle_throttle_plate_position ${f(p.idlePlate)}`,
        `// builder:live rev_limit_rpm ${f(p.revLimit)}`,
        `// builder:live starter_torque_nm ${f(p.starterTorque)}`,
        `// builder:live friction_torque_nm ${f(p.friction)}`,
        `// builder:live timing_curve ${curve.join(" ")}`,
        `// builder:live fuel_octane ${f(p.knockModel === "on" ? p.octane : 0)}`,
        `// builder:live knock_damage ${p.knockDamage === "on" ? 1 : 0}`,
        `// builder:live engine_repair ${p.repairCounter || 0}`,
        `// builder:live cam_low ${f(p.intakeLift)} ${f(p.intakeDuration)} ${f(p.exhaustLift)} ${f(p.exhaustDuration)}`,
    ];
    if (p.camSwitch === "on") {
        header.push(`// builder:live cam_high ${f(p.highIntakeLift)} ${f(p.highIntakeDuration)} ${f(p.highExhaustLift)} ${f(p.highExhaustDuration)}`);
        header.push(`// builder:live cam_switch ${f(p.camSwitchRpm)} ${f(p.camHysteresis)} ${f(p.camMinThrottle)} ${f(p.camMinMap)}`);
    }
    const igmap = p.ignMap;
    if (igmap && igmap.on && igmap.adv.length === igmap.rpm.length) {
        header.push(`// builder:live map_rpm ${igmap.rpm.join(" ")}`);
        header.push(`// builder:live map_load ${igmap.load.join(" ")}`);
        header.push(`// builder:live map_adv ${igmap.adv.flat().join(" ")}`);
    }
    return { ...body, text: applyTuning(header, tuning).join("\n") + "\n" + body.text, structure };
}

export function generateBody(p, cfg) {
    const a = analyze(p, cfg);
    const vtec = p.camSwitch === "on";
    const fp = fuelProps(p.ethanol);
    const octaneEff = p.knockModel === "on" ? p.octane : 0;      // 0 switches the knock model off
    const n = cfg.cyls.length;
    const id = ident(p.name);
    const nb = cfg.banks.length;
    const bankAngles = cfg.banks.map(x => { const m = mod(x + 180, 360) - 180; return m === -180 ? 180 : m; });
    const cyls = cfg.cyls.map((c, i) => ({ c: i + 1, bank: c.bank, pin: mod(c.pin, 360), fire: a.fires[i] }));
    const banks = bankAngles.map((angle, b) => ({ angle, cyls: cyls.filter(x => x.bank === b) }));
    banks.forEach(b => b.cyls.forEach((x, i) => { x.pos = i; x.count = b.cyls.length; }));

    // rod journals: cylinders with the same pin angle share one journal
    const journals = [];
    for (const x of cyls) {
        const key = r4(x.pin);
        let j = journals.find(q => q.key === key);
        if (!j) { j = { key, users: [] }; journals.push(j); }
        j.users.push(x.c);
        x.j = journals.indexOf(j);
    }

    const s = Math.pow(p.bore / 88.9, 2);          // scales ports with bore area
    const L = [];
    const w = (t = "") => L.push(t);

    w(`// Generated by tools/engine-builder.html`);
    w(`// ${n} cyl, ${nb} bank(s), ${a.displacement.toFixed(2)} L, ${a.cr.toFixed(1)}:1, firing order ${a.order.join("-")}${a.even ? "" : " (uneven)"}`);
    w(`import "engine_sim.mr"`);
    w();
    w(`units units()`);
    w(`constants constants()`);
    w(`impulse_response_library ir_lib()`);
    w(`label cycle(2 * 360 * units.deg)`);
    w();

    w(`private node ${id}_wires {`);
    for (let c = 1; c <= n; c++) w(`    output wire${c}: ignition_wire();`);
    w(`}`);
    w();

    const intakeFlow = [[0, 0], [50, 58], [100, 103], [150, 156], [200, 214], [250, 249], [300, 268], [350, 280], [400, 280], [450, 281]];
    const exhaustFlow = [[0, 0], [50, 37], [100, 72], [150, 113], [200, 160], [250, 196], [300, 222], [350, 235], [400, 245], [450, 246]];
    const flowFn = (name, tbl) => [`    function ${name}(50 * units.thou)`, `    ${name}`,
        ...tbl.map(([l, q]) => `        .add_flow_sample(${l} * lift_scale, ${q} * flow_attenuation)`), ``];
    w(`private node ${id}_head {`);
    w(`    input intake_camshaft;`);
    w(`    input exhaust_camshaft;`);
    if (vtec) {
        w(`    input vtec_intake_camshaft;`);
        w(`    input vtec_exhaust_camshaft;`);
        w(`    input switch_rpm: ${f(p.camSwitchRpm)} * units.rpm;`);
        w(`    input switch_hysteresis: ${f(p.camHysteresis)} * units.rpm;`);
        w(`    input switch_min_throttle: ${f(p.camMinThrottle / 100)};`);
        w(`    input switch_min_manifold_pressure: ${f(p.camMinMap * 1000)};`);
    }
    w(`    input chamber_volume: ${f(p.chamber)} * units.cc;`);
    w(`    input intake_runner_volume: ${f(149.6 * s)} * units.cc;`);
    w(`    input intake_runner_cross_section_area: ${f(11.76 * s)} * units.cm2;`);
    w(`    input exhaust_runner_volume: ${f(50 * s)} * units.cc;`);
    w(`    input exhaust_runner_cross_section_area: ${f(25.8 * s)} * units.cm2;`);
    w();
    w(`    input flow_attenuation: ${f(s)};   // scaled with bore area`);
    w(`    input lift_scale: 1.0;`);
    w(`    input flip_display: false;`);
    w(`    alias output __out: head;`);
    w();
    L.push(...flowFn("intake_flow", intakeFlow));
    L.push(...flowFn("exhaust_flow", exhaustFlow));
    w(`    generic_cylinder_head head(`);
    w(`        chamber_volume: chamber_volume,`);
    w(`        intake_runner_volume: intake_runner_volume,`);
    w(`        intake_runner_cross_section_area: intake_runner_cross_section_area,`);
    w(`        exhaust_runner_volume: exhaust_runner_volume,`);
    w(`        exhaust_runner_cross_section_area: exhaust_runner_cross_section_area,`);
    w();
    w(`        intake_port_flow: intake_flow,`);
    w(`        exhaust_port_flow: exhaust_flow,`);
    if (vtec) {
        w(`        valvetrain: vtec_valvetrain(`);
        w(`            vtec_intake_camshaft: vtec_intake_camshaft,`);
        w(`            vtec_exhaust_camshaft: vtec_exhaust_camshaft,`);
        w(`            intake_camshaft: intake_camshaft,`);
        w(`            exhaust_camshaft: exhaust_camshaft,`);
        w(`            min_rpm: switch_rpm,`);
        w(`            hysteresis: switch_hysteresis,`);
        w(`            min_throttle_position: switch_min_throttle,`);
        w(`            manifold_vacuum: switch_min_manifold_pressure`);
        w(`        ),`);
    } else {
        w(`        valvetrain: standard_valvetrain(`);
        w(`            intake_camshaft: intake_camshaft,`);
        w(`            exhaust_camshaft: exhaust_camshaft`);
        w(`        ),`);
    }
    w(`        flip_display: flip_display`);
    w(`    )`);
    w(`}`);
    w();

    // camshaft(s): one intake/exhaust pair per bank, lobes in bank cylinder order
    w(`private node ${id}_camshaft {`);
    w(`    input intake_lobe_profile;`);
    w(`    input exhaust_lobe_profile;`);
    if (vtec) { w(`    input vtec_intake_lobe_profile;`); w(`    input vtec_exhaust_lobe_profile;`); }
    w(`    input intake_lobe_center: ${f(p.intakeCenter)} * units.deg;`);
    w(`    input exhaust_lobe_center: ${f(p.exhaustCenter)} * units.deg;`);
    if (vtec) {
        w(`    input vtec_intake_lobe_center: ${f(p.highIntakeCenter)} * units.deg;`);
        w(`    input vtec_exhaust_lobe_center: ${f(p.highExhaustCenter)} * units.deg;`);
    }
    w(`    input advance: 0 * units.deg;`);
    w(`    input base_radius: 0.75 * units.inch;`);
    w();
    for (let b = 0; b < nb; b++) {
        w(`    output intake_cam_${b}: _intake_cam_${b};`);
        w(`    output exhaust_cam_${b}: _exhaust_cam_${b};`);
        if (vtec) {
            w(`    output vtec_intake_cam_${b}: _vtec_intake_cam_${b};`);
            w(`    output vtec_exhaust_cam_${b}: _vtec_exhaust_cam_${b};`);
        }
    }
    w();
    w(`    camshaft_parameters params(`);
    w(`        advance: advance,`);
    w(`        base_radius: base_radius`);
    w(`    )`);
    w();
    for (let b = 0; b < nb; b++) {
        w(`    camshaft _intake_cam_${b}(params, lobe_profile: intake_lobe_profile)`);
        w(`    camshaft _exhaust_cam_${b}(params, lobe_profile: exhaust_lobe_profile)`);
        if (vtec) {
            w(`    camshaft _vtec_intake_cam_${b}(params, lobe_profile: vtec_intake_lobe_profile)`);
            w(`    camshaft _vtec_exhaust_cam_${b}(params, lobe_profile: vtec_exhaust_lobe_profile)`);
        }
    }
    w();
    w(`    label rot360(360 * units.deg)`);
    w();
    for (let b = 0; b < nb; b++) {
        const lobes = (sign, centre) => banks[b].cyls.map(x =>
            `        .add_lobe(rot360 ${sign} ${centre} + ${f(x.fire)} * units.deg) // cyl ${x.c}`).join("\n");
        w(`    _exhaust_cam_${b}`);
        w(lobes("-", "exhaust_lobe_center"));
        w(`    _intake_cam_${b}`);
        w(lobes("+", "intake_lobe_center"));
        if (vtec) {
            w(`    _vtec_exhaust_cam_${b}`);
            w(lobes("-", "vtec_exhaust_lobe_center"));
            w(`    _vtec_intake_cam_${b}`);
            w(lobes("+", "vtec_intake_lobe_center"));
        }
        w();
    }
    w(`}`);
    w();

    // engine
    const moment = (m, r) => `disk_moment_of_inertia(mass: ${m}, radius: ${r})`;
    w(`public node ${id} {`);
    w(`    alias output __out: engine;`);
    w();
    w(`    engine engine(`);
    w(`        name: "${String(p.name).replace(/"/g, "'")}",`);
    w(`        starter_torque: ${f(p.starterTorque)} * units.Nm,`);
    w(`        starter_speed: 500 * units.rpm,`);
    w(`        redline: ${f(p.redline)} * units.rpm,`);
    w(`        throttle_gamma: 2.0,`);
    w(`        fuel: fuel(`);
    w(`            octane: ${f(octaneEff)},`);
    w(`            molecular_mass: ${f(fp.molarMass)} * units.g,`);
    w(`            energy_density: ${f(fp.energy)} * units.kJ / units.g,`);
    w(`            density: ${f(fp.density)} * units.kg / units.L`);
    w(`        )`);
    w(`    )`);
    w();
    w(`    ${id}_wires wires()`);
    w();
    w(`    label stroke(${f(p.stroke)} * units.mm)`);
    w(`    label bore(${f(p.bore)} * units.mm)`);
    w(`    label rod_length(${f(p.rodLength)} * units.mm)`);
    w(`    label rod_mass(${f(p.rodMass)} * units.g)`);
    w(`    label compression_height(${f(p.compHeight)} * units.mm)`);
    w(`    label crank_mass(${f(p.crankMass)} * units.kg)`);
    w(`    label flywheel_mass(${f(p.flywheelMass)} * units.kg)`);
    w(`    label flywheel_radius(${f(p.flywheelRadius)} * units.mm)`);
    w();
    w(`    label crank_moment(`);
    w(`        ${moment("crank_mass", "stroke / 2")}`);
    w(`    )`);
    w(`    label flywheel_moment(`);
    w(`        ${moment("flywheel_mass", "flywheel_radius")}`);
    w(`    )`);
    w(`    label other_moment( // Moment from cams, pulleys, etc [estimated]`);
    w(`        ${moment("2 * units.kg", "5 * units.cm")}`);
    w(`    )`);
    w();
    w(`    crankshaft c0(`);
    w(`        throw: stroke / 2,`);
    w(`        flywheel_mass: flywheel_mass,`);
    w(`        mass: crank_mass,`);
    w(`        friction_torque: ${f(p.friction)} * units.Nm,`);
    w(`        moment_of_inertia:`);
    w(`            crank_moment + flywheel_moment + other_moment,`);
    w(`        position_x: 0.0,`);
    w(`        position_y: 0.0,`);
    w(`        tdc: (90 + ${f(bankAngles[0])}) * units.deg`);
    w(`    )`);
    w();
    w(`    // Rod journals: cylinders with the same pin angle share a journal`);
    journals.forEach((j, i) => w(`    rod_journal rj${i}(angle: ${f(j.key)} * units.deg) // cyl ${j.users.join(", ")}`));
    w(`    c0`);
    journals.forEach((_, i) => w(`        .add_rod_journal(rj${i})`));
    w();
    w(`    piston_parameters piston_params(`);
    w(`        mass: ${f(p.pistonMass)} * units.g,`);
    w(`        compression_height: compression_height,`);
    w(`        wrist_pin_position: 0.0,`);
    w(`        displacement: 0.0`);
    w(`    )`);
    w();
    w(`    connecting_rod_parameters cr_params(`);
    w(`        mass: rod_mass,`);
    w(`        moment_of_inertia: rod_moment_of_inertia(`);
    w(`            mass: rod_mass,`);
    w(`            length: rod_length`);
    w(`        ),`);
    w(`        center_of_mass: 0.0,`);
    w(`        length: rod_length`);
    w(`    )`);
    w();
    w(`    intake intake(`);
    w(`        plenum_volume: ${f(a.displacement * 0.35)} * units.L,`);
    w(`        plenum_cross_section_area: ${f(20 * s)} * units.cm2,`);
    w(`        intake_flow_rate: k_carb(${f(p.carbCfm)}),`);
    w(`        runner_flow_rate: k_carb(${f(p.carbCfm * 0.5)}),`);
    w(`        runner_length: ${f(p.runnerLength)} * units.mm,`);
    w(`        idle_flow_rate: k_carb(0.0),`);
    w(`        idle_throttle_plate_position: ${f(p.idlePlate)},`);
    w(`        velocity_decay: 0.5`);
    w(`    )`);
    w();
    w(`    exhaust_system_parameters es_params(`);
    w(`        outlet_flow_rate: k_carb(1000.0),`);
    w(`        primary_tube_length: ${f(p.primaryLength)} * units.mm,`);
    w(`        primary_flow_rate: k_carb(500.0),`);
    w(`        velocity_decay: 1.0,`);
    w(`        length: ${f(p.exhaustLength)} * units.mm`);
    w(`    )`);
    w();
    const nex = p.exhaustMode === "one" ? 1 : p.exhaustMode === "cyl" ? n : nb;
    const exOf = x => p.exhaustMode === "one" ? 0 : p.exhaustMode === "cyl" ? x.c - 1 : x.bank;
    for (let e = 0; e < nex; e++) {
        w(`    exhaust_system exhaust${e}(`);
        w(`        es_params,`);
        w(`        audio_volume: ${f(p.audioVolume)},`);
        w(`        impulse_response: ir_lib.${p.exhaustIr}`);
        w(`    )`);
    }
    w();
    w(`    cylinder_bank_parameters bank_params(`);
    w(`        bore: bore,`);
    w(`        deck_height: stroke / 2 + rod_length + compression_height`);
    w(`    )`);
    w();
    w(`    label spacing(${f(p.bore + 10)} * units.mm)`);
    w();
    for (let b = 0; b < nb; b++) w(`    cylinder_bank b${b}(bank_params, angle: ${f(bankAngles[b])} * units.deg)`);
    w();
    for (let b = 0; b < nb; b++) {
        w(`    b${b}`);
        for (const x of banks[b].cyls) {
            w(`        .add_cylinder(`);
            w(`            piston: piston(piston_params, blowby: k_28inH2O(0.1)),`);
            w(`            connecting_rod: connecting_rod(cr_params),`);
            w(`            rod_journal: rj${x.j},`);
            w(`            intake: intake,`);
            w(`            exhaust_system: exhaust${exOf(x)},`);
            w(`            ignition_wire: wires.wire${x.c},`);
            w(`            primary_length: spacing * ${x.count - 1 - x.pos}`);
            w(`        )`);
        }
        w(`        .set_cylinder_head(`);
        w(`            ${id}_head(`);
        w(`                intake_camshaft: camshaft.intake_cam_${b},`);
        w(`                exhaust_camshaft: camshaft.exhaust_cam_${b},`);
        if (vtec) {
            w(`                vtec_intake_camshaft: camshaft.vtec_intake_cam_${b},`);
            w(`                vtec_exhaust_camshaft: camshaft.vtec_exhaust_cam_${b},`);
        }
        w(`                flip_display: ${nb > 1 && bankAngles[b] > 0 ? "true" : "false"})`);
        w(`        )`);
    }
    w();
    for (let b = 0; b < nb; b++) w(`    engine.add_cylinder_bank(b${b})`);
    w(`    engine.add_crankshaft(c0)`);
    w();
    w(`    harmonic_cam_lobe intake_lobe(`);
    w(`        duration_at_50_thou: ${f(p.intakeDuration)} * units.deg,`);
    w(`        gamma: 1.0,`);
    w(`        lift: ${f(p.intakeLift)} * units.mm,`);
    w(`        steps: 100`);
    w(`    )`);
    w();
    w(`    harmonic_cam_lobe exhaust_lobe(`);
    w(`        duration_at_50_thou: ${f(p.exhaustDuration)} * units.deg,`);
    w(`        gamma: 1.0,`);
    w(`        lift: ${f(p.exhaustLift)} * units.mm,`);
    w(`        steps: 100`);
    w(`    )`);
    w();
    if (vtec) {
        w(`    harmonic_cam_lobe vtec_intake_lobe(`);
        w(`        duration_at_50_thou: ${f(p.highIntakeDuration)} * units.deg,`);
        w(`        gamma: 1.0,`);
        w(`        lift: ${f(p.highIntakeLift)} * units.mm,`);
        w(`        steps: 100`);
        w(`    )`);
        w();
        w(`    harmonic_cam_lobe vtec_exhaust_lobe(`);
        w(`        duration_at_50_thou: ${f(p.highExhaustDuration)} * units.deg,`);
        w(`        gamma: 1.0,`);
        w(`        lift: ${f(p.highExhaustLift)} * units.mm,`);
        w(`        steps: 100`);
        w(`    )`);
        w();
    }
    w(`    ${id}_camshaft camshaft(`);
    w(`        intake_lobe_profile: intake_lobe,`);
    if (vtec) {
        w(`        vtec_intake_lobe_profile: vtec_intake_lobe,`);
        w(`        vtec_exhaust_lobe_profile: vtec_exhaust_lobe,`);
    }
    w(`        exhaust_lobe_profile: exhaust_lobe`);
    w(`    )`);
    w();
    w(`    function timing_curve(1000 * units.rpm)`);
    w(`    timing_curve`);
    for (let r = 0; r <= Math.max(p.redline, 1000); r += 1000) {
        const t = Math.min(1, r / 4000);
        w(`        .add_sample(${String(r).padStart(4, "0")} * units.rpm, ${f(p.advanceIdle + (p.advanceMax - p.advanceIdle) * t)} * units.deg)`);
    }
    w();
    w(`    ignition_module ignition_module(`);
    w(`        timing_curve: timing_curve,`);
    w(`        rev_limit: ${f(p.revLimit)} * units.rpm,`);
    w(`        limiter_duration: 0.1)`);
    w(`    ignition_module`);
    a.order.forEach(c => w(`        .connect_wire(wires.wire${c}, ${f(a.fires[c - 1])} * units.deg)`));
    w();
    w(`    engine.add_ignition_module(ignition_module)`);
    w(`}`);
    w();

    const gears = String(p.gears).split(/[,\s]+/).map(Number).filter(g => g > 0);
    w(`private node ${id}_vehicle {`);
    w(`    alias output __out:`);
    w(`        vehicle(`);
    w(`            mass: ${f(p.carMass)} * units.kg,`);
    w(`            drag_coefficient: ${f(p.drag)},`);
    w(`            cross_sectional_area: ${f(p.frontalArea)} * units.m * units.m,`);
    w(`            diff_ratio: ${f(p.diff)},`);
    w(`            tire_radius: ${f(p.tire)} * units.mm,`);
    w(`            rolling_resistance: 0.015 * ${f(p.carMass)} * units.kg * 9.81`);
    w(`        );`);
    w(`}`);
    w();
    w(`private node ${id}_transmission {`);
    w(`    alias output __out:`);
    w(`        transmission(`);
    w(`            max_clutch_torque: ${f(p.clutch)} * units.Nm`);
    w(`        )`);
    gears.forEach(g => w(`        .add_gear(${f(g)})`));
    w(`        ;`);
    w(`}`);
    w();
    w(`public node main {`);
    w(`    set_engine(${id}())`);
    w(`    set_vehicle(${id}_vehicle())`);
    w(`    set_transmission(${id}_transmission())`);
    w(`}`);
    w();
    return { text: L.join("\n"), analysis: a, id };
}
