## ⚠️ Vorabversion – nicht für belastbare Ergebnisse

**Dies ist ein Test-Build einer überarbeiteten Benutzeroberfläche für FEMM 5.0 (femm-qt).
Die Ergebnisse sind noch nicht validiert und dürfen nicht als Grundlage für
Auslegungs- oder Sicherheitsentscheidungen verwendet werden.** Wichtige Werte bitte
mit dem offiziellen FEMM-Release ([dcm3c/femm-qt](https://github.com/dcm3c/femm-qt))
gegenprüfen. Die App zeigt diese Warnung auch auf der Startseite und im Fenstertitel.

> **Pre-production release – results are not yet validated.** This is a test build of
> a reworked user interface for FEMM 5.0 (femm-qt). Do not rely on its results for
> design or safety decisions; check important values against the official release.

### Neu gegenüber FEMM 5.0
- **Zeichnen wie im CAD:** Linie, Rechteck, Kreis, Bogen mit Vorschau, Objektfang und Maßeingabe; Auswahl per Fenster; Trimmen, Verlängern, Offset, Bemaßung, Muster
- **Bereiche & Materialien:** Flächen nach Material eingefärbt, Farbeimer mit Materialsuche, fehlende Labels markiert, Luftraum mit einem Klick
- **Panels:** Modell-Panel mit Inspector, Ergebnis-Panel mit Induktivität/Widerstand und Kräften, X-Y-Plot als Panel
- **Ablauf:** Startseite mit Beispielen, Neu-Assistent, Prüfung vor dem Lösen, Parameterstudie, PDF-Bericht, Befehlspalette (⌘K / Strg+K), Autosave, Dark Mode, deutsche Oberfläche
- **KI-Anbindung:** MCP-Server (`mcp/femm_mcp.py`) für Claude Code / Claude Desktop

Die Solver (fkn, belasolv, hsolv, csolv) sind gegenüber FEMM 5.0 unverändert. Jeder
Build löst vor dem Paketieren ein Referenzmodell (Spule mit Eisenkern, 2,02 mH).

### Installation
| System | Datei | Hinweis |
|---|---|---|
| macOS (Apple Silicon) | `…-macos-arm64.dmg` | Nicht signiert: nach dem Kopieren Rechtsklick auf die App → **Öffnen**, oder `xattr -dr com.apple.quarantine /Applications/femm_gui.app` |
| Windows 10/11 (x64) | `…-windows-x64.zip` | Entpacken, `femm_gui.exe` starten. SmartScreen-Warnung: **Weitere Informationen → Trotzdem ausführen** |
| Linux (x64) | `…-linux-x64.tar.gz` | Entpacken; benötigt Qt 6 (z. B. `sudo apt install libqt6widgets6`), dann `bin/femm_gui` |

Gebaut von Branch [`feature/ux-overhaul`](https://github.com/nohai123/femm-qt/tree/feature/ux-overhaul).
Lizenz wie FEMM 5.0: Oberfläche MIT, Solver GPLv3.
