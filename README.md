# Sweepbar 0.5.0

Beat-synced vocal ear candy. Like Kickstart, a curve fires on the beat, but instead of ducking
volume it drives an effect on the vocal. You pick which beats fire, which effect each beat uses,
and how long the pattern is, and you can draw your own curves.

**New in 0.5.0:** rebuilt as photorealistic studio hardware. The chassis, plates, recessed
wells, glass displays, switch caps and machined knobs are modelled in 3D and lit per pixel
(key and rim lights, ambient occlusion, cast shadows, anodized, brushed and spun-metal materials).
Waveforms are layered light: spectral fills, bloom, crisp cores. The input/output meters now show
the real signal level. Finishes: Graphite, Gunmetal, Obsidian, Titanium, Bronze.

**0.4.0:** studio-hardware look: realistic black knobs with printed 0-10 scales,
a 3D glass screen, neon glow, candy-paint chassis with metal flake (five finishes: click
**FINISH** at the top right to switch).

**0.3.0:** a settings panel for every effect (the gear on each effect tile), a new
hardware-style look, a Smooth knob, and input/output gain.

**0.2.0:** a different effect on every beat, and hand-drawn curves.

## Install in FL Studio (Windows 64-bit)

**With the installer (easiest):**

1. Close FL Studio and run `SweepbarSetup.exe`. Click **Yes** when Windows asks for permission.
   The installer isn't code-signed yet, so Windows may first show "Windows protected your PC":
   click **More info**, then **Run anyway**.
2. Click **Install**. Sweepbar goes into `C:\Program Files\Common Files\VST3`.
3. In FL Studio: **Options > Manage plugins**, click **Find more plugins** (or **Start scan**).
   Sweepbar shows up under Effects.
4. Open the Mixer, select your vocal's insert, click an empty slot and pick **Sweepbar**.
5. Press play. The effect only fires while FL Studio's transport is running.

To remove it: **Settings > Apps > Sweepbar > Uninstall**. Running the installer again
replaces an existing copy, which is how you update to a new version.

**Manually:** unzip `Sweepbar-0.5.0-win64-VST3.zip`, copy the whole `Sweepbar.vst3` folder into
`C:\Program Files\Common Files\VST3`, then continue from step 3.

## Install on Mac (FL Studio, Logic, Ableton, any AU/VST3 host)

1. Download `SweepBar-<version>-macOS-Installer.pkg` from the repository's **Releases** page.
2. The plugin isn't notarized by Apple yet, so the first time **right-click the .pkg > Open**, then click
   **Open** (double-clicking shows "can't be opened"). On recent macOS: System Settings > Privacy &
   Security > **Open Anyway**.
3. Click through the installer. It puts `Sweepbar.vst3` in `/Library/Audio/Plug-Ins/VST3` and
   `Sweepbar.component` (Audio Unit) in `/Library/Audio/Plug-Ins/Components`.
4. FL Studio: **Options > Manage plugins > Find more plugins**. Logic: it appears under Audio Units.
   Works on Apple Silicon and Intel Macs.

If a host still refuses to load it, run this once in Terminal and rescan:

```
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Sweepbar.vst3 /Library/Audio/Plug-Ins/Components/Sweepbar.component
```

## Controls

| Where | Control | What it does |
|---|---|---|
| Effect tiles (top) | Tile | Pick the effect to paint beats with: Delay throw, Reverb throw, Filter sweep, Stutter, Pitch dip, Bitcrush. Each has its own color |
| | Gear | Open that effect's settings (see below). Click the gear again, click outside the card, or press Esc to close |
| Screen | Beat steps | Click an off beat to add it with the selected effect. Click a beat with a different effect to repaint it. Click a beat that already has the selected effect to remove it. Right-click a beat to pick its effect, turn it on or off, or reset its drawn curve |
| | Curve | Drag to draw each beat's curve by hand; right-drag to erase. Drawing on an off beat turns it on with the selected effect |
| Left cells | pattern | 1 bar, 6 beats, 2 bars or 4 bars. 6 beats drifts against the 4/4 bar and lines up again every 3 bars |
| | shape | Preset curve for beats you haven't drawn on: Ramp down, Ramp up, Pulse, Swell |
| | length | How long each preset-shape hit lasts: 1/16 to 1/4 note. Drawn curves span the whole beat |
| Right cells | steps | All beats on, clear all, paint every beat with the selected effect, or copy bar 1 to every bar |
| | drawing | Shows how many beats are drawn; reset all drawing |
| | sync | The host tempo Sweepbar is following |
| Deck | DEPTH | How far the curve pushes the effect, drawn or preset |
| | MIX | Overall effect amount |
| | SMOOTH | How quickly the effect follows the curve. Low is snappy; high softens sharp drawn curves and pulses |
| Footer | INPUT / OUTPUT | Live level meters, and faders for gain into and out of Sweepbar (-24 to +12 dB). Double-click a fader to reset |

The status light next to the name is lit while FL Studio is playing. The selector at the top
right switches the chassis metal: Graphite, Gunmetal, Obsidian, Titanium or Bronze. The finish is
saved with the project. INPUT and OUTPUT meters show the signal level going in and out (-48 to +3 dB,
with a held peak); the faders next to them set input and output gain.

## Effect settings (gear)

| Effect | Settings |
|---|---|
| Delay throw | Time (1/16 to 1/2, incl. dotted), Ping-pong on/off, Feedback, Tone (how bright the echoes are) |
| Reverb throw | Size (tail length), Damping (darker tail), Width |
| Filter sweep | Mode (Telephone band, Low-pass, High-pass), Resonance, Range (how far the sweep goes) |
| Stutter | Slice (1/32 to 1/4), Smoothing (soft or hard repeat edges) |
| Pitch dip | Direction (down or up), Range (1 to 24 semitones), Grain (smoother or grittier shifting) |
| Bitcrush | Bits (bit depth at full hit), Rate (sample-rate reduction at full hit) |

Settings apply to every beat that uses that effect.

A small dot on a beat means it has a drawn curve.

Every control is a normal plugin parameter, including each beat's on/off state and effect, so
FL Studio saves it with the project and you can automate it (right-click a control in FL's plugin
wrapper > Create automation clip). Drawn curves are saved with the project too. Projects made
with 0.1.0 open with their single effect applied to every beat, and projects from 0.1.0 or
0.2.0 open with default effect settings.

How each effect uses the curve:

- **Delay throw**: the curve opens the send into the delay. Echoes keep ringing after the hit.
- **Reverb throw**: the curve opens the send into the reverb. The tail keeps ringing.
- **Filter sweep**: sweeps the filter further the higher the curve goes.
- **Stutter**: grabs a slice at the start of the hit and repeats it.
- **Pitch dip**: shifts the pitch, further the higher the curve goes.
- **Bitcrush**: lowers bit depth and sample rate, more the higher the curve goes.

Between hits the vocal passes through untouched, except for delay and reverb tails still ringing out.
When neighboring beats use different effects, each effect fades on its own, so the handover is
click-free and a delay or reverb tail keeps ringing under the next beat's effect.

## Building from source

Every push to `main` builds Mac (AU + VST3, universal) and Windows (VST3) automatically
(`.github/workflows/build.yml`); pushing a `v*` tag publishes a release with the Mac installer.


The plugin uses JUCE 7.0.12 (C++17).

**On Windows with Visual Studio 2022** (easiest for further development):

```
git clone --branch 7.0.12 https://github.com/juce-framework/JUCE.git
cmake -B build -DJUCE_DIR=JUCE
cmake --build build --config Release
```

The plugin lands in `build/Sweepbar_artefacts/Release/VST3/`.

**Cross-compiling from Linux** (how the releases are built): `build_windows.sh` uses
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and compiles the JUCE modules directly.

**Installer:** `installer/build_installer.sh` builds `SweepbarSetup.exe` (a small Win32 program
with the plugin embedded as a resource) after `build_windows.sh` has built the plugin.
`SweepbarSetup.exe /S` installs silently.

**Tests:** `tests/engine_test.cpp` checks the audio engine on its own (g++ only).
`tests/plugin_harness.cpp` runs the full processor and editor headless on Linux, including
state save/restore and screenshots of the window (`tests/build_native_harness.sh`).

## Layout

- `Source/SweepEngine.h`: timing, curves and all six effects (plain C++, no JUCE)
- `Source/PluginProcessor.*`: parameters, host transport, state
- `Source/PluginEditor.*`: the plugin window
- `Source/Skin.*`: the hardware renderer (height-field geometry, per-pixel lighting, materials, knobs, light effects)
- `Source/FontData.*`: embedded Barlow typeface (SIL Open Font License, `Resources/fonts/OFL.txt`), generated by `Resources/embed_fonts.py`
- `installer/`: the Windows installer (`installer.cpp`, resources, icon)

## License note

JUCE 7 is dual-licensed (GPLv3 or a commercial JUCE license). Free or personal use is fine;
the "Made with JUCE" splash appears briefly when the window opens. If you plan to sell this,
check JUCE's license terms first.
