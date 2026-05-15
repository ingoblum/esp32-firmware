# WARP-1-Firmware unter Windows bauen

Diese Arbeitskopie enthaelt den Source von `https://github.com/Tinkerforge/esp32-firmware` im Ordner `esp32-firmware`. Das eigentliche PlatformIO-Projekt liegt darunter in `software`, weil dort die `platformio.ini` und die Produktdateien wie `warp.ini`, `warp2.ini` und `warp3.ini` liegen.

## Ergebnis

Der unveraenderte WARP-1-Build wurde erfolgreich geprueft. Danach wurde in `software/warp.ini` das Modul `Day Ahead Prices` analog zu `warp2.ini` aufgenommen und die geaenderte Firmware erfolgreich gebaut.

Die aktuell gebauten Dateien liegen hier:

```powershell
software\build\warp_firmware_latest.elf
software\build\warp_firmware_latest_merged.bin
software\build\firmware_latest.elf
software\build\firmware_latest_merged.bin
software\build_latest\warp_firmware-UNSIGNED_2_10_2_6a063512_1507a65224f5a04_merged.bin
```

Die `*_merged.bin` ist die zusammengefuehrte Firmware, die laut Build-Ausgabe ab Flash-Offset `0x1000` geflasht werden kann.

## Wichtige Windows-Besonderheiten

Der normale Benutzer hatte eine defekte alte npm-Installation unter:

```text
C:\Data\INGORALFBLUM\ingo\Roaming\npm\node_modules\npm
```

Darum wurde fuer den Build `NPM_CONFIG_PREFIX=C:\Program Files\nodejs` gesetzt. Damit verwendet npm die intakte Node.js-Installation aus `C:\Program Files\nodejs`; `npm -v` liefert dann `11.12.1`.

Ausserdem musste PlatformIO ueber den 8.3-Kurzpfad gebaut werden. Der lange Projektpfad enthaelt Leerzeichen (`Warp Wallbox`), und ein Bootloader-Schritt in der Espressif/PlatformIO-Toolchain quotet diesen Pfad nicht korrekt. Der funktionierende Kurzpfad ist:

```text
C:\Data\INGORA~1\ingo\Files\Basteln\WARPWA~1\Firmware\Eigene\ESP32-~1\software
```

Die alte globale PlatformIO-Installation unter `C:\Users\ingo\.platformio` enthielt zudem ein inkompatibles `littlefs`-Pythonpaket. Deshalb wird ein projektlokales PlatformIO-Core-Verzeichnis verwendet:

```text
C:\Data\INGORA~1\ingo\Files\Basteln\WARPWA~1\Firmware\Eigene\ESP32-~1\.platformio-core
```

## Verwendete Software

Vorhanden waren:

```powershell
git --version
python --version
node -v
```

Gefunden wurde:

```text
git version 2.50.1.windows.1
Python 3.14.5
Node.js v24.15.0
```

PlatformIO Core war in der VSCode-Erweiterung vorhanden, aber zu alt (`6.1.9`). Das Repository fordert ueber `pyproject.toml` Python `3.12.*` und aktuelle Python-Abhaengigkeiten. Deshalb wurde `uv` im Benutzerkontext installiert und mit `uv sync` eine lokale `.venv` erzeugt.

## Wesentliche Shellkommandos

Source holen:

```powershell
git clone --recursive https://github.com/Tinkerforge/esp32-firmware.git esp32-firmware
```

`git clone` kopiert das Repository in den Zielordner. `--recursive` wuerde Submodules direkt mit auschecken; das Tinkerforge-Repo hatte aktuell keine `.gitmodules`, der Parameter ist hier also vorsorglich und unschaedlich.

`uv` installieren:

```powershell
python -m pip install --user uv
```

`--user` installiert in den Benutzerkontext, nicht systemweit und ohne Administratorrechte. Danach lag `uv.exe` unter:

```text
C:\Data\INGORALFBLUM\ingo\Roaming\Python\Python314\Scripts\uv.exe
```

Projekt-venv erzeugen:

```powershell
$env:NPM_CONFIG_PREFIX='C:\Program Files\nodejs'
& 'C:\Data\INGORALFBLUM\ingo\Roaming\Python\Python314\Scripts\uv.exe' sync
```

`uv sync` liest `pyproject.toml` und `uv.lock`, laedt Python `3.12.13` in den Benutzerkontext und erzeugt `software\.venv` mit PlatformIO `6.1.19`.

Build-Umgebung setzen:

```powershell
$env:PATH='C:\Data\INGORALFBLUM\ingo\Roaming\Python\Python314\Scripts;' + $env:PATH
$env:NPM_CONFIG_PREFIX='C:\Program Files\nodejs'
$env:PLATFORMIO_CORE_DIR='C:\Data\INGORA~1\ingo\Files\Basteln\WARPWA~1\Firmware\Eigene\ESP32-~1\.platformio-core'
```

`PATH` sorgt dafuer, dass interne Hook-Aufrufe wie `uv run ...` gefunden werden. `NPM_CONFIG_PREFIX` umgeht die defekte Benutzer-npm-Installation. `PLATFORMIO_CORE_DIR` lenkt PlatformIO in das projektlokale Core-Verzeichnis und verhindert die Kollision mit der alten globalen PlatformIO-Umgebung.

Build ausfuehren:

```powershell
cd 'C:\Data\INGORA~1\ingo\Files\Basteln\WARPWA~1\Firmware\Eigene\ESP32-~1\software'
uv run pio run -e warp
```

`uv run` startet den Befehl in der lokalen `.venv`. `pio run` baut das PlatformIO-Projekt. `-e warp` waehlt die Umgebung `[env:warp]` fuer die WARP Wallbox der ersten Generation.

## VSCode

Im Repo-Root wurde `.vscode/settings.json` angelegt. Dort ist PlatformIO so eingestellt, dass die lokale `software\.venv\Scripts` verwendet wird und nicht die alte eingebaute bzw. globale PlatformIO-Core-Installation.

Zusaetzlich gibt es `.vscode/tasks.json` mit dem Task `Build WARP Firmware`. Der Task setzt dieselben Umgebungsvariablen wie oben und startet den Build ueber den leerzeichenfreien Kurzpfad.

In VSCode kann der Build daher ueber `Terminal -> Run Build Task... -> Build WARP Firmware` gestartet werden. Alternativ kann in einem neuen VSCode-Terminal im Ordner `software` derselbe Buildbefehl verwendet werden, sofern die Terminal-Umgebung aus `.vscode/settings.json` aktiv ist:

```powershell
uv run pio run -e warp
```

## Aenderung fuer Day Ahead Prices

In `software/warp.ini` wurde `Day Ahead Prices` an drei Stellen ergaenzt:

```ini
custom_backend_modules
custom_frontend_modules
custom_frontend_components
```

Damit wird das Backend-Modul `src/modules/day_ahead_prices` gebaut und das Frontend zeigt die Konfiguration im Energy-Management-Bereich an. Beim erfolgreichen Build war sichtbar, dass `src\modules\day_ahead_prices\day_ahead_prices.cpp` und die generierten Day-Ahead-Prices-Dateien kompiliert wurden.

## Erfolgreiche Build-Pruefung

Der finale Build endete mit:

```text
Environment    Status    Duration
-------------  --------  ------------
warp           SUCCESS   00:02:45.055
```

Die Firmware-Info aus dem finalen Build:

```text
Firmware info: warp, WARP Charger, 2.10.2, 6A063512
```

Groessen laut PlatformIO:

```text
RAM:   19.8% (64992 bytes)
Flash: 41.7% (2733359 bytes)
```
