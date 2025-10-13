# WB50 KiCad Projektstruktur

Dieses Verzeichnis enthält das komplette KiCad-Projekt inkl. lokaler Bibliotheken, Teilschaltpläne, PCB-Hilfsdateien und Fertigungs-/Montageordner.

## Struktur
- libraries/
  - symbols/: Projekt‑Symbolbibliothek `wb50_symbols.kicad_sym`
  - footprints/: Projekt‑Footprints in `wb50_footprints.pretty/`
  - 3dmodels/: 3D‑Modelle (optional)
- schematics/: Teilschaltpläne, die später als Hierarchieblätter in `wb50.kicad_sch` eingebunden werden
- pcb/: DXF‑Hilfsdateien (Board‑Outline, Keepouts, Front/Rear‑Panel‑Guides)
- fabrication/: Gerber/Bohrdaten, Fab‑Notizen, Stückliste und Pick&Place
- simulation/spice/: SPICE‑Testbench und Model‑Hinweise

## Erste Schritte (Front‑/Rear‑Panel zuerst)
1) Symbole & Footprints erstellen/zuweisen
   - Externe Bedienelemente (Potenziometer, Schalter, Klinken, Netz‑/Standby‑Schalter, LED) als Symbole in `wb50_symbols.kicad_sym` pflegen.
   - Passende Footprints in `wb50_footprints.pretty/` ablegen oder Standardbibliotheken verwenden.
2) Teilschaltpläne befüllen
   - In `schematics/` liegen Platzhalter für Preamp, Voicing‑Relays, Tone‑Stack, FX‑Loop, Masters, PI, Power‑Amp, NFB‑Shaping, Power‑Supply, Control‑Power.
   - Externe Bedienelemente zunächst sauber im Schaltplan anlegen (mit eindeutigen Referenzen und Netz‑Namen).
3) PCB‑Vormontage der Front/Rear‑Elemente
   - Im PCB‑Editor die Footprints der externen Bedienelemente zuerst entsprechend der Front/Rear‑Panel‑Mechanik platzieren.
   - DXF‑Hilfsgeometrie in `pcb/` importieren (Panel‑Bohrungen/Abstände), um Positionen zu verifizieren.
4) Verdrahtung/Netze nachziehen
   - Danach restliche Baugruppen platzieren, sensible Netze kurz halten (Grids, CF→Tonestack), sternförmige Masseführung beachten.

Tipp: In der `sym-lib-table` und `fp-lib-table` sind die lokalen Bibliotheken bereits auf `${KIPRJMOD}` referenziert.
