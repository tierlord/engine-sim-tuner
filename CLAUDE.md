# Engine Sim – Tuning-Fork

Fork von [ange-yaghi/engine-sim](https://github.com/ange-yaghi/engine-sim) (C++17, Delta Studio, Piranha-`.mr`-Skripte).
Ziel des Nutzers: ein **Tuning-/Vergleichswerkzeug**, das zeigt, welche Änderung (Nocken, Zündung, Kraftstoff …) was am Motorlauf bewirkt. Realismus ist „nice to have“.

**Sprache:** Mit dem Nutzer auf **Deutsch** und per **du** schreiben.

## Stand (noch nichts committet)

Alles unten ist im Arbeitsverzeichnis, aber **nicht in Git**. Auch die Submodule `delta-studio` und `piranha` haben lokale Änderungen.

| Bereich | Inhalt |
|---|---|
| Engine-Builder | `tools/engine-builder.html`: Ein-Datei-Generator mit grafischer Anordnung (Reihe/V/Boxer/W/Stern), Vorlagen, Verdichtung ↔ Brennraum, Hubraum gesamt, „● live“-Felder |
| Live-Modus | **F5** im Simulator. HTTP-Server `127.0.0.1:8765` (`src/live_server.cpp`), Builder pusht Konfig. Live-Werte ohne Neustart, Strukturänderung lädt neu |
| Prüfstand | `src/dyno_sweep.cpp`: Stufen und Beschleunigungslauf (Rampe, trägheitskorrigiert) am **laufenden** Simulator. Diagramm, Lauf-Vergleich, CSV im Builder |
| Zündkennfeld | Drehzahl × Saugrohrdruck (`ignition_module`), nur im Live-Modus. Standardmäßig **aus** (dann gilt die Zündkurve) |
| Klopfmodell | `CombustionChamber::updateKnock` (Livengood-Wu), Leistungsverlust, Klopfgeräusch, Motorschaden. Kraftstoffe: Super 95/98, 100, E10, E20, E85 |
| Nocken-Umschaltung | VTEC-artiges zweites Profil mit Umschaltdrehzahl, Hysterese, Mindestlast. Hub und Dauer live änderbar (`src/cam_lobe.cpp`) |
| Oberfläche | Gauges: Zündwinkel, Drossel, MAP, Klopfen, Schaden, Oktan; Nm und PS |
| Nockenfelder | Schieberegler neben den Zahlenfeldern (Hub 3–22 mm, Dauer 150–330°) – **im Browser noch nicht visuell geprüft** |

## Auf einem anderen Rechner einrichten (Windows)

1. Visual Studio mit der C++-Arbeitslast (bringt CMake, Ninja und `vswhere` mit).
2. vcpkg klonen und bootstrappen, dann `vcpkg install sdl2 sdl2-image boost-filesystem --triplet x64-windows-static-md`. Umgebungsvariable **`VCPKG_ROOT`** auf den vcpkg-Ordner setzen.
3. `winget install WinFlexBison.win_flex_bison` (danach eine neue Konsole öffnen).
4. Repository holen (Branch `tuner`) und `git submodule update --init --recursive`.
5. **Lokale Submodul-Fixes anwenden** (die Änderungen liegen nicht in den Submodul-Repos): `git -C dependencies/submodules/piranha apply ../../../patches/piranha-local.patch` und `git -C dependencies/submodules/delta-studio apply ../../../patches/delta-studio-local.patch`.
6. `tools\windows-build\build.bat` im Projektordner (baut `engine-sim-server` und `engine-sim-app` nach `build\`; frische Konfiguration getestet).
7. `build\engine-sim-server.exe` starten: öffnet die Oberfläche. Ohne Motor beginnt sie im Bauen. Der Ordner `assets/engines/custom/` wird beim ersten „Motor laden“ angelegt.

**Nicht in Git:** die eigene Motordatei `assets/engines/custom/live_engine.mr`, der Zustand im Browser (Bauen-Einstellungen, Messläufe) – im Bauen mit „Konfig speichern“ als JSON sichern, „Konfig laden“ holt sie zurück –, die Test-Programme im Scratchpad.
## Bauen und Starten

- Toolchain: MSVC (VS 2026), CMake/Ninja, vcpkg in `C:\Users\joey7\vcpkg` (Triplet `x64-windows-static-md`), WinFlexBison.
- Build-Ordner `build/` (gitignored); `tools\windows-build\build.bat` baut alles (`BUILD_DIR` ändert den Ordner). Exe **aus `build\` starten** (relative Pfade `../assets`).
- Lokale Submodul-Fixes: `piranha/src/path.cpp` (`is_complete` → `is_absolute`), `delta-studio/CMakeLists.txt` (d3dx10.lib vor d3dx10d.lib).
- `src/engine_sim_application.cpp` hat **CRLF**-Zeilenenden.
- Läuft der Simulator noch, ist die Exe gesperrt: alte Exe umbenennen (`engine-sim-app.prev.exe`), dann neu linken. **Den laufenden Prozess des Nutzers nie beenden.**

## Live-Protokoll (Builder → Simulator)

Die erzeugte `.mr` beginnt mit Kommentarzeilen:

- `// builder:structure <hash>` – FNV-1a des Körpers mit neutralisierten Live-Werten. Gleicher Hash → nur `applyLiveTuning`, sonst `reloadScript`.
- `// builder:live <key> <werte>` – Schlüssel: `idle_throttle_plate_position, rev_limit_rpm, starter_torque_nm, friction_torque_nm, timing_curve, map_rpm/map_load/map_adv, fuel_octane, knock_damage, engine_repair, cam_low, cam_high, cam_switch`.
- Neues Live-Feld = Eintrag in `LIVE_KEYS` (Builder), Header-Zeile im Generator **und** Auswertung in `applyLiveTuning`.
- Routen: `GET /status`, `POST /engine`, `POST /dyno`, `POST /dyno/stop`, `GET /dyno`. Es sind nur `null`/localhost/file://-Ursprünge erlaubt.
- Test-Umgebungsvariablen: `ENGINE_SIM_LIVE_PORT`, `ENGINE_SIM_LIVE_ENTRY`, `ENGINE_SIM_LIVE_FILE`.

## Stolpersteine

- **Neue Skript-Node-Inputs** müssen zusätzlich in `es/objects/objects.mr` deklariert werden, sonst Absturz in `Node::readAllInputs`.
- Die Eigene Motordatei des Nutzers `assets/engines/custom/live_engine.mr` bei Tests **nicht überschreiben** (vorher sichern, danach zurück).
- Der Browser-Tab des Nutzers verbindet sich automatisch mit Port 8765 und pusht seine Konfig. Tests deshalb auf **anderem Port und anderen Dateien** (z. B. 8799, `?port=8799`).
- Test-Hooks (`ENGINE_SIM_AUTOTEST`) nur temporär einbauen, vorher `engine_sim_application.cpp` sichern, danach zurückkopieren.
- Heredocs und Einzeiler mit Anführungszeichen und Backslashes gehen leicht schief. **Skripte per Write-Tool in Dateien schreiben** und dann ausführen.
- **Keine Bildschirmfotos vom Desktop des Nutzers.** Nur das Simulatorfenster (`PrintWindow`) oder den eigenen Chrome-Tab.

## Modell-Eigenheiten (wichtig für Kalibrierung)

- Kolbenkraft = Fläche × (p_Zylinder − p_Kurbelgehäuse). Die Flamme breitet sich geometrisch aus, `percentageLit` bleibt bis zum nächsten Funken 1.0.
- Die Verbrennung rechnet immer mit Oktan-Stöchiometrie. Ethanol läuft deshalb über einen Pseudo-Kraftstoff (`fuelProps`).
- Es gibt kaum Wandwärmeverlust (Ansaugtemperatur zu niedrig, Abgas zu heiß). Die Flamme ist in Kurbelgrad zu langsam, daher kommt das Zündoptimum zu spät. Drehmoment und IMEP sind zu hoch.
- Klopfkonstanten sind **meine Schätzungen**, nicht an echten Motoren validiert.
- Nockenumschaltung schaltet sofort, auch bei geöffnetem Ventil. Das Hochprofil lohnt sich nur gegen ein mildes Basisprofil (Strömungstabelle sättigt bei ~11,4 mm).
- Ein Leerlaufregler fehlt. Die Leerlaufdrehzahl ergibt sich aus Drosselstellung und Reibung.

## Plan: neue Oberfläche und Linux (beschlossen, noch nicht gebaut)

Der Nutzer will **langfristig hauptsächlich unter Linux** arbeiten. Der Browser-Builder wird zu einem Tuning-Cockpit, WebView2 (nur Windows) ist **keine** Voraussetzung.

- **Befund:** Delta Studio hat nur Windows-Fenster, -Eingabe und DirectSound-Audio (`yds_windows_*`, `yds_ds8_*`). OpenGL/Vulkan-Zweige hängen an Windows-Kontexten. Die Community-Edition ist nur ein Verteil-Repo ohne Quellcode und nicht mehr gepflegt. Es gibt einen inoffiziellen SDL-Fork (AUR `engine-sim-git`, nicht verifiziert).
- **Zielarchitektur:** Simulatorkern **ohne Fenster** (Physik, Synthese, Ton, HTTP/SSE-Server) + Web-Oberfläche im Browser, auch für die Motoransicht (Canvas/WebGL aus einem Datenstrom). Kein OpenGL, kein Fenster in C++.
- **Ton:** `readAudioOutput()` liefert fertige 16-Bit-Samples bei 44,1 kHz. Heute schreibt `engine_sim_application.cpp` (~Zeile 260–320) sie in einen DirectSound-Ringpuffer. Ersatz: Audio-Callback (SDL2 ist schon im Build, sonst miniaudio). Der Simulator-Prozess spielt den Ton selbst ab.
- **Lebensdauer koppeln:** Dauerhafte SSE-Verbindung als Lebenszeichen. Letzte Verbindung weg → 5–10 s Karenz → Fade-out → beenden. Start der Oberfläche durch den Simulator (Windows: Edge/Chrome `--app`, Linux: Browser). Keine Verbindung nach ~15 s → beenden. `--keep-alive` für Tests.
- **Reihenfolge:** (1) Test-Programm ohne Fenster mit eigener Hauptschleife und Audio-Callback, (2) Verbindungsstrom und Lebensdauer, (3) Web-Oberfläche mit Seitenleiste (Aufbau · Ventiltrieb · Zündung · Kraftstoff · Prüfstand/Vergleich) und Live-Leiste.
- **Schritt 1 erledigt (Spike):** `tools/headless-spike/audio_spike.cpp` rechnet den Motor ohne Fenster und spielt den Ton über SDL2-Audio (Callback + Ringpuffer). 15 s, 0 Aussetzer, Klang laut Nutzer **normal**. Die Simulation regelt ihren Takt selbst über den Füllstand des Synthesizers (Ziel 0,1 s), keine eigene Uhr nötig. Die Datei ist ein Beispiel und noch nicht im CMake; Baufile `build_audio.bat.example` zeigt die Bibliotheken (SDL2-static). Ersetzt hat sie `ysWindowsAudioWaveFile` durch einen 30-Zeilen-WAV-Leser. **Achtung:** Der Impulsantwort-Pfad aus dem Skript ist gemischt (`../es\sound-library\archive/…wav`), unter Linux müssen Pfade vor dem Laden vereinheitlicht werden. Spitzenwert 32768 (Anschlag) – nicht gegen den normalen Simulator verglichen.
- **Schritt 2 erledigt:** `SimulationSession` (`include/simulation_session.h`, `src/simulation_session.cpp`) enthält alles ohne Fenster (Skript, Simulator, Live-Tuning, Prüfstand, Fahrereingaben, Routen). `EngineSimApplication` steuert sie über Callbacks. Neues Programm **`engine-sim-server`** (`src/server_main.cpp`, CMake-Ziel `engine-sim-server`): Simulation ohne Fenster, Ton über SDL2-Audio, HTTP auf 127.0.0.1 (Port aus `--port` oder `ENGINE_SIM_LIVE_PORT`, Standard 8765). Neue Routen: `GET /telemetry` (JSON mit Drehzahl, Last, Zündwinkel, Klopfen, Schaden …) und `POST /control` (Zeilen `key=value`: throttle, clutch, starter, ignition, shift, gear, dyno, dyno_hold, dyno_rpm). Der Server schaltet die Zündung beim Laden automatisch ein (`setAutoIgnition`). **Noch offen:** Lebensdauer-Kopplung (SSE, Karenz, Fade-out), Linux-Sockets in `live_server.cpp` (Winsock), `engine-sim`-Bibliothek linkt noch `delta-basic`, Web-Oberfläche.
- **Schritt 3 erledigt (Lebensdauer):** `GET /events` (SSE) im Live-Server, `LiveServer::publish()` / `streamCount()`; die Session sendet alle 50 ms Telemetrie an offene Streams. `engine-sim-server --exit-when-idle [--grace S (8)] [--startup-timeout S (20)] [--open URL]`: beendet sich mit 0,4 s Fade-out, wenn der letzte Strom `grace` Sekunden weg ist oder nie einer kam. Ohne `--exit-when-idle` läuft er bis Strg+C (wegen des bisherigen Builder-Ablaufs noch kein Standard). Der Builder öffnet den Strom und zeigt Drehzahl, MAP, Zündwinkel. Test: `lifetime_test.js` (Scratchpad, nicht im Repo). Beobachtet: 47 Audio-Aussetzer in einem 30-s-Lauf mit Motorwechsel (Neuladen unterbricht die Tonausgabe) – nicht untersucht.
- **Entwurf der Oberfläche (beschlossen, `tools/ui-mockup.html`):** Zwei Ansichten, Umschalter **[Bauen] [Tunen]** oben. *Bauen* = alles, was den Motor neu lädt (Anordnung, Hubraum und Verdichtung, Kurbeltrieb, Ansaugung/Abgas, Option VTEC an/aus), mit Kennzahlenzeile und Knopf „Motor laden und tunen“; der alte Motor läuft bis zum Laden weiter. *Tunen* = Live-Leiste (Drehzahl, Last, Zündwinkel, Moment, Klopfen, Schaden, Nocken), Fahrerbedienung (Gas, Starter, Zündung) und Abschnitte Zündung · Gemisch & Kraftstoff · **Ventiltrieb** · Prüfstand & Vergleich; rechts Motoransicht, Zündfolge, Abgasströmung. **Ventiltrieb live:** Hub, Dauer und **Lage (Mittellinie) von Ein- und Auslass** sind im Betrieb verstellbar (VVT-artig), plus Steuerzeiten als Zahl; VTEC-Felder nur bei VTEC. Hub und Dauer sind im C++ schon live. Für die Lage fehlt noch ein Setter (`Camshaft::m_advance`, `setLobeCenterline` gibt es): Header `cam_center`, Session merkt sich die Werte des geladenen Skripts als Basis und dreht nur um die Differenz.
- **Schritt 4 begonnen (Gerüst der Weboberfläche):** Ordner `web/` (`index.html`, `app.css`, `app.js`, ES-Modul ohne Build) wird vom Server unter `/` ausgeliefert (`ENGINE_SIM_WEB`, Standard `../web` relativ zum Arbeitsordner `build/`). Neue Routen: `GET/POST /tune` (Live-Werte als Zeilen `key wert…`, nur genannte Schlüssel ändern sich; `map_off 1` schaltet das Kennfeld aus; `repair 1` repariert; GET liefert den Stand, der Server ist die Wahrheit). Fertig: Umschalter, Live-Leiste mit echten Daten, Gas/Starter/Zündung, **Zündung-Ansicht** (Kennfeld mit Betriebspunkt, Kurve, Begrenzer, Leerlauf-Drossel). **Offen:** Bauen (Generator aus `engine-builder.html` als Modul herauslösen), Ventiltrieb live inkl. Lage, Gemisch & Kraftstoff, Prüfstand & Vergleich, Motoransicht. Nach „Motor laden und tunen“ steht der Motor ruhig mit Zündung an (Entscheidung des Nutzers, `setAutoIgnition`). Der alte `tools/engine-builder.html` bleibt, bis die neue Oberfläche alles kann.
- **Server-Standard (geändert):** `engine-sim-server` öffnet die Oberfläche von selbst (Edge im App-Modus, sonst Standardbrowser) und beendet sich mit Fade-out, wenn kein Fenster mehr verbunden ist (`--grace 8`, `--startup-timeout 30`). `--no-open` und `--keep-alive` schalten das ab (für Tests: `lifetime_test.js` im Scratchpad). Ist der Port belegt, öffnet er die Oberfläche der laufenden Instanz und beendet sich (Exit-Code 4). Der Live-Server bindet mit `SO_EXCLUSIVEADDRUSE` – `SO_REUSEADDR` ließ unter Windows zwei Programme denselben Port teilen. **Nie Prozesse nach Namen beenden** (der Nutzer kann eigene Server/Simulatoren laufen haben), nur die selbst gestarteten PIDs.
- **Bauen (neu, `web/build.js` + `web/generator.js`):** Der Generator ist als ES-Modul aus `tools/engine-builder.html` herausgelöst (`generate(p, cfg, tuning)`, für alle Vorlagen byte-gleich zum alten Builder geprüft). Seiten: Anordnung (Stirnansicht mit Ziehen, Zündabstände, Bänke/Zylinder-Tabellen), Hubraum & Verdichtung, Kurbeltrieb, Ansaugung & Abgas, Ventiltrieb (Aufbau, vorerst mit den Startwerten der Nocken), Motor & Kraftstoff, Fahrzeug. „Motor laden und tunen“ holt `GET /tune`, setzt dessen Werte (Zündkurve, Begrenzer, Leerlauf-Drossel, Kennfeld) in den Kopf der Motordatei (`applyTuning` im Generator) und sendet `POST /engine`; danach Wechsel nach Tunen. So bleibt Eingestelltes beim Neuladen erhalten. Vom Bauen versteckt (gehören zum Tunen): `idlePlate, revLimit, advanceIdle, advanceMax`. Zustand in `localStorage` (`engineTunerBuild`, liest ersatzweise den Stand des alten Builders). **Noch offen:** Nockenfelder aus dem Bauen entfernen, sobald „Tunen → Ventiltrieb“ existiert; Reparieren-Knopf; Prüfstand.- **Prüfstand (neu, `web/dyno.js`):** Messlauf (Beschleunigung/Stufen) mit Diagramm, Lauf-Liste, Vergleich der Änderungen (Bauen-Parameter + Tuningwerte aus `/tune`), Hover blendet andere Läufe aus, CSV. Läufe in `localStorage` (`engineTunerRuns`, liest ersatzweise die des alten Builders). Der Snapshot eines Laufs nimmt den **zuletzt geladenen** Motor (`build.snapshot()`), nicht den Formularstand. **Drehzahl halten (Volllast):** `DynoSweep` hat einen dritten Modus `mode=hold` (Start wie bei den anderen Läufen mit Anlasser, danach konstante Drehzahl und Gas bis Abbruch; `POST /dyno/hold` mit `rpm=`, `throttle=` ändert beides live; **aus dem Stand ohne Anlasserstart dreht der Prüfstand den Motor rückwärts** – deshalb der gemeinsame Startpfad). Dazu neu: `advance_offset` (Live-Wert über `/tune`, `IgnitionModule::setAdvanceOffset`, addiert sich zu Kurve oder Kennfeld), Telemetrie `dyno_state`, `dyno_mode`, `advance_offset`. UI: Versatz-Regler, Kurve „Drehmoment über Zündwinkel“ (0,5°-Klassen, alte Werte verblassen), „Versatz übernehmen“ addiert ihn zu den vier Kennfeldzellen um den Betriebspunkt (oder zu Leerlauf und Maximum der Kurve). Hinweis: Mit ausgeschaltetem Klopfmodell steigt das Moment über den realistischen Bereich hinaus weiter (Flammenmodell unkalibriert, siehe unten).- **Lambda gibt es noch nicht:** Das Gemisch ist fest. Eigener Modellschritt (Kraftstoffmenge je Luftmasse), Voraussetzung für Pops und Bangs (unverbrannter Kraftstoff im Abgas, Nachverbrennung als echter Teil der Gasrechnung).- Neuer Code (Live-Server usw.) **ohne Windows-APIs** schreiben; `live_server.cpp` nutzt vermutlich Winsock und braucht eine kleine Anpassung.
- Offene Frage: Wer gibt die Zeit vor (Uhr oder Audiotakt) und wie ersetzen wir die Tasten (Gas, Gang, Starter) im Web.

## Nächste Ideen (nur auf Zuruf des Nutzers)

0. **Bekannte Eigenheit:** Nach dem Schließen des Fensters läuft der Motor noch `--grace` (8 s) weiter, das fühlt sich laut Nutzer komisch an („erstmal lassen“). Möglichkeit: Karenz auf ~3 s kürzen oder beim echten Schließen sofort beenden (pagehide + Neuladen unterscheiden).
1. **Leerlaufregler / Leerlauf-Kennfeld** (zuletzt angeboten).
2. Variable Nockenwellenverstellung (VVT, Mittellinien live).
3. Umschaltpunkt nach Last statt fester Drehzahl.
4. Flammenmodell kalibrieren (realistisches MBT), Wandwärme.
5. Aufladung (Lader/Turbo), Gemisch/AFR, E85-Ladungskühlung.
6. Zündkennfeld und Nockenkennfelder auch im exportierten `.mr` (Skript hat keinen Map-Node).
7. Zylinderdruckmessung in Prüfstand und Builder integrieren.
8. Schieberegler auch für andere Felder (Zündwinkel, Rev-Limit).
9. Aufräumen und Committen (nichts ist committet, auf Nachfrage Branch anlegen).

## Konventionen

- Dateien klein halten, nichts Unnötiges umbauen. Der C++-Kern bleibt (keine Rust-Neuentwicklung).
- Nach jeder Änderung: bauen, dann mit echtem Compiler gegen alle Vorlagen prüfen. Dazu die Harness-Tests aus dem Scratchpad nutzen (`check.exe`, `dyno.exe`, …), die nicht im Repo liegen.
- Kein Commit oder Push ohne Aufforderung.
