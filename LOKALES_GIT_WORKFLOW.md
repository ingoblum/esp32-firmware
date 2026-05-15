# Lokaler Git-Workflow fuer WARP-1-Anpassungen

## Lokales Tracking-Repo und Updates

Der geklonte Ordner `esp32-firmware` ist bereits ein Git-Repository. Die Tinkerforge-Quelle sollte als `upstream` gefuehrt werden. Die lokalen Anpassungen laufen auf einem eigenen Branch, zum Beispiel:

```powershell
git remote rename origin upstream
git switch -c warp1-day-ahead-prices
```

Sinnvolle Commit-Aufteilung:

```text
1. Windows build setup for local WARP firmware builds
2. Enable Day Ahead Prices for WARP 1
3. Optional spaeter: dynamic price accounting in charge tracker
```

Neue Tinkerforge-Releases koennen spaeter so eingearbeitet werden:

```powershell
git fetch upstream
git switch warp1-day-ahead-prices
git rebase upstream/master
```

`rebase` wendet die lokalen Commits neu auf den aktuellen Tinkerforge-Stand an. Das haelt die lokale Patch-Serie uebersichtlich. Falls Konflikte auftreten, sind sie wahrscheinlich in `warp.ini`, `platformio.ini`, `charge_tracker` oder den Windows-Build-Dateien.

Nach jedem Update sollte der WARP-Build geprueft werden:

```powershell
uv run pio run -e warp
```

## `origin` spaeter ergaenzen

Nach `git remote rename origin upstream` muss `origin` nicht sofort existieren. Es ist vollkommen in Ordnung, erstmal nur `upstream` zu haben:

```text
upstream -> https://github.com/Tinkerforge/esp32-firmware.git
```

Spaeter kann jederzeit ein eigenes Remote ergaenzt werden:

```powershell
git remote add origin <dein-spaeterer-fork-oder-server>
```

Wenn sofort ein lokales `origin` gewuenscht ist, kann ein lokales Bare-Repo verwendet werden:

```powershell
git init --bare ..\esp32-firmware-local.git
git remote add origin ..\esp32-firmware-local.git
git push -u origin warp1-day-ahead-prices
```

Der einfachste Start ist aber: Tinkerforge zu `upstream` umbenennen, lokal auf dem Feature-Branch committen, und erst dann ein `origin` hinzufuegen, wenn wirklich irgendwohin gepusht werden soll.
