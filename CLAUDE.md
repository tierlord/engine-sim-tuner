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

## Bauen und Starten

- Toolchain: MSVC (VS 2026), CMake/Ninja, vcpkg in `C:\Users\joey7\vcpkg` (Triplet `x64-windows-static-md`), WinFlexBison.
- Build-Ordner `build/` (gitignored). Exe **aus `build\` starten** (relative Pfade `../assets`).
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
- Neuer Code (Live-Server usw.) **ohne Windows-APIs** schreiben; `live_server.cpp` nutzt vermutlich Winsock und braucht eine kleine Anpassung.
- Offene Frage: Wer gibt die Zeit vor (Uhr oder Audiotakt) und wie ersetzen wir die Tasten (Gas, Gang, Starter) im Web.

## Nächste Ideen (nur auf Zuruf des Nutzers)

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
