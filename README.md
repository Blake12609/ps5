# EdgePad

DualSense and DualSense Edge remapper for PC: Edge-style profiles, back buttons, stick curves and
trigger stops, plus **anti-dead zone** and **GameSir-style RC filters**. It's written in C++, ships
as a single portable executable, and updates itself from this repository's releases.

![EdgePad](docs/screenshot.png)

## Features

| Area | What you get |
| --- | --- |
| **Sticks** | **Exactly 1:1 by default**: every stick step reaches the game as the controller sent it, diagonals included. Inner / outer dead zone, **anti-dead zone**, radial or axial dead zone, invert X/Y, swap sticks, one-click **drift calibration** |
| **Response curves** | DualSense Edge presets (Default, Quick, Precise, Steady, Digital, Dynamic) with adjustable strength, plus a **Custom** curve you drag with the mouse |
| **RC filter** | GameSir style, active only while you move the stick. Positive = *stabilizer* (RC low-pass that removes micro-jitter). Negative = *jitter* that wobbles the aim direction while keeping your aim speed exactly the same |
| **Triggers** | Dead zone, **trigger stop** (short trigger range), anti-dead zone, **rapid hair trigger** (full press the moment you pull, releases as soon as you ease off, fires again without letting go), **turbo** (1–100 ms between presses), adaptive-trigger **resistance wall** that makes the stop something you can feel |
| **Gyro aiming** | Turn or tilt the controller to fine-aim on top of the right stick. Always on, while a button is held (e.g. L2 for aim down sights) or toggled. Sensitivity, vertical speed, yaw or roll, dead zone, smoothing for slow movements only, anti-dead zone, drift calibration |
| **Buttons** | Map any button, including the Edge **back buttons**, to a controller button, full L2/R2 press, **keyboard key** or **mouse button**, or disable it. Per-button **toggle** and **turbo** with its own 1–100 ms interval |
| **Shift layer** | Hold a shift button (e.g. a back button) and every button switches to a second set of bindings |
| **Touchpad zones** | Split the touchpad into 2 or 4 extra buttons, fired on click or on touch, which is great on a regular DualSense |
| **Profiles** | Up to 16 profiles. **Fn + Cross/Circle/Square/Triangle** switches profile from the controller, like the Edge. Lightbar colour and player LEDs show the active profile |
| **Controller** | DualSense and DualSense Edge over USB or Bluetooth, battery level, game rumble forwarded back to the controller |
| **Output** | A virtual **PS5 controller (DualSense)** with no ViGEmBus. Games get a genuine wired DualSense whose adaptive triggers, rumble and lightbar reach your real controller. Or a virtual **Xbox 360** / **PlayStation 4 (DualShock 4)** controller via ViGEmBus. On Linux: UHID / uinput |
| **Hide controller** | **Hide the real controller from games** so they only see the virtual one: no double input. Uses HidHide when it's installed (EdgePad sets it up and undoes it by itself), otherwise exclusive access. On Linux it grabs the controller's input devices |
| **Portable** | One exe. Settings live in `EdgePad-data/` next to it, so the folder can sit on a USB stick |
| **Auto update** | Checks GitHub Releases on start, verifies the SHA-256 checksum and swaps in the new exe |

A regular DualSense works too. It has no back buttons, so the **Mute** button acts as Fn.

### Performance

- Controller input runs on its own thread, driven by the controller's HID reports (no polling sleeps).
- A report is turned into virtual controller output in microseconds.
- Every controller report goes straight to the virtual controller. ViGEmBus silently drops an update
  when the game side has no read waiting, so EdgePad never relies on a single update getting through.
  The next report (1–4 ms later) always carries the current state.
- Lightbar, LED, adaptive-trigger and rumble writes are capped at one per 10 ms. These writes can block for a
  few ms, especially over Bluetooth, and fast-changing game rumble could otherwise hold up input.
- On Windows the input thread runs at high priority, so a busy game can't delay a controller report.
- The UI renders at full rate only while focused; in the background it drops to about 15 fps, and it stops rendering while minimized.

## Getting started (Windows)

1. Download `EdgePad-windows-x64.exe` from the [latest release](../../releases/latest) and put it in any folder.
2. Install the driver for the virtual controller games will see, once:
   - **PS5 controller (recommended):** [usbip-win2](https://github.com/vadimgrn/usbip-win2/releases/latest),
     then choose **Settings → Virtual controller → PlayStation 5 (DualSense)**. No ViGEmBus needed. See
     [Virtual PS5 controller](#virtual-ps5-controller).
   - **Xbox 360 / PS4 controller:** the [ViGEmBus driver](https://github.com/nefarius/ViGEmBus/releases/latest).
3. Recommended: install [HidHide](https://github.com/nefarius/HidHide/releases/latest).
4. Start EdgePad and connect the controller over USB or Bluetooth. It is picked up automatically.
5. Turn on **Settings → Hide the real controller from games**. Games then only see the virtual controller, so
   they never get double input from the real one (see [Hiding the real controller](#hiding-the-real-controller)).

If Steam is running, disable Steam Input's PlayStation support for the virtual controller so you only have one remapping layer.

With ViGEmBus, **PlayStation 4 (DualShock 4)** output also gives PlayStation button prompts. EdgePad passes the
DualSense's gyro, accelerometer and touchpad through to it, so motion aiming works in games that support it.
For a PS5 controller, use the [virtual PS5 controller](#virtual-ps5-controller) instead.

## Virtual PS5 controller

**Settings → Virtual controller → PlayStation 5 (DualSense)** gives games a genuine wired DualSense:

- **Windows:** EdgePad acts as a USB DualSense and [usbip-win2](https://github.com/vadimgrn/usbip-win2)
  (a Microsoft-signed USB/IP driver) plugs it into Windows. Windows then handles it with its own USB
  and HID drivers, exactly like a real DualSense on a cable. There's no ViGEmBus and no translation to an
  Xbox or PS4 controller. EdgePad attaches it when it starts and unplugs it cleanly when it closes; it
  doesn't need admin rights.
- **Linux:** the kernel's UHID creates the device and its own DualSense driver (`hid-playstation`) takes
  it over. Install `70-edgepad.rules` for access to `/dev/uhid`.

The virtual controller uses the real DualSense's USB descriptors and HID report descriptor, byte for byte.
Every input report is your controller's own report with EdgePad's sticks, triggers and buttons written
in. The sequence number, gyro, accelerometer, touchpad, battery and any undocumented bytes reach the game
unchanged. With untouched settings the whole report is identical to what the controller sent (unit
tested). Calibration, MAC address and firmware info come from your controller too, so motion is 1:1.

Games drive the real controller through it, like on a PS5: **adaptive triggers**, rumble and player LEDs
(and the lightbar if "Let games set the lightbar colour" is on). A profile's trigger resistance keeps
priority over the game's trigger effects. Turn on **Hide the real controller from games** as well, so
games see only the virtual DualSense.

## Hiding the real controller

If a game sees both the real DualSense and EdgePad's virtual controller, it gets every input twice: once
remapped and once raw. Aim then fights itself, and the controller "feels off". **Settings → Hide the real
controller from games** fixes that:

- **Windows with HidHide:** EdgePad adds itself to HidHide's allowed applications (it does this on every
  start, so it can always open the controller, even one you hid in HidHide by hand), hides the controller from
  every other program and switches HidHide on. With HidHide 2 the hiding belongs to EdgePad's process, so
  the driver ends it the moment EdgePad closes, even after a crash. With older versions EdgePad removes its
  entry when it closes, or on its next start. You don't need to touch HidHide's own settings.
- **Windows without HidHide:** EdgePad opens the controller exclusively, so no other program can open it
  while EdgePad has it. That only works if nothing else (Steam, DS4Windows, a game) already has it open. If
  something does, EdgePad says so; close it and reconnect the controller, or install HidHide.
- **Linux:** EdgePad grabs the controller's input devices, so games and the desktop get none of their
  events. Programs that read the controller through hidraw (Steam Input, some SDL games) aren't affected;
  turn off their PlayStation controller support. Install `70-edgepad.rules` so EdgePad can grab all of them.

Programs that already had the controller open keep it until it reconnects. Start games after EdgePad, or
unplug and replug the controller.

### Why not EdgePad's own driver?

A virtual controller needs a kernel driver, and Windows 10/11 only load drivers that Microsoft has
signed. Getting that signature takes an EV code-signing certificate and Microsoft's attestation
process. The other route, test-signing mode, is blocked by the anti-cheats of most online games.
That's why the virtual PS5 controller uses usbip-win2, a signed USB/IP driver: with it Windows itself
treats EdgePad's DualSense as a real USB device. For Xbox 360 / PS4 output, ViGEmBus is the signed
driver and adds well under a millisecond. ViGEmBus copies EdgePad's
DualShock 4 report into the virtual controller byte for byte, and passes the Xbox 360 report on as
is. With untouched settings, the game gets:

- every stick and trigger value exactly as the controller sent it (all 256 steps, both ends,
  diagonals);
- the controller's own L2/R2 "pressed" bits, every button and the d-pad;
- motion in the same degrees per second and g as from the real DualSense. The virtual DualShock 4
  reports a fixed calibration, and EdgePad converts the gyro and accelerometer with both
  calibrations, the way SDL reads them (the library many PC games use for PlayStation controllers).

The reports are built in platform-independent code and unit tested byte by byte. What usually makes it
feel different is the real controller being visible next to the virtual one (above), or extra
processing (dead zones, curves) on top of the game's own.

## Getting started (Linux)

```sh
chmod +x EdgePad-linux-x64
sudo cp packaging/linux/70-edgepad.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
./EdgePad-linux-x64            # or --headless to run without a window
```

On Linux the virtual controller is either a PS5 controller through UHID (games' rumble, adaptive triggers
and lightbar reach the real controller) or an Xbox 360-style uinput device.

## Controller shortcuts

| Shortcut | Action |
| --- | --- |
| Fn + Cross / Circle / Square / Triangle | Switch to the profile with that hotkey |
| Fn + Options | Toggle remapping on/off (raw passthrough) |

Fn is either Edge Fn button, or Mute on a regular DualSense. You can change this under **Settings → Fn button**.
To use an Edge Fn button as a normal button (for example bound to a keyboard key), set the Fn button to
the other Fn button or to Mute. It then appears as bindable in the Buttons tab.
Buttons pressed as part of an Fn combo are never sent to the game.

## RC filter

Modelled on the RC filter in GameSir's controller software. Both modes only work **while you move
the stick**, meaning while it's pushed past its dead zone (at least 3%). With your thumb off the stick
nothing is added, even with a 0% dead zone or an anti-dead zone, and letting go stops instantly with no
smoothing tail.

- **Positive values (stabilizer):** an RC low-pass on the raw stick, with a time constant of up to 40 ms. It lags slightly behind your thumb, which removes micro-stutter so aim feels heavier and more consistent.
- **Negative values (jitter):** while you aim, the aim keeps swinging a tiny amount to alternating sides. A new swing starts every 5 ms, with a random size of 50–100% of the setting (up to 6% of stick travel). It moves through an RC stage, so it looks like an analog signal rather than a hard on/off switch. It only rotates the stick direction, so the stick length (your aim speed) stays exactly what your thumb is doing. It also averages out, so your aim never drifts. Some games keep aim assist engaged with this. **Some online games treat it as aim-assist abuse, so check the rules of the game you play.**

The filter is time-based, so it feels the same at 250 Hz or 1000 Hz.

## Gyro aiming

1. Put the controller on a flat surface and click **Gyro → Calibrate gyro**. This removes sensor drift.
2. Set **Gyro** to *While a button is held* with **L2**, so the gyro only aims while you aim down sights.
   You can also leave it *Always on*, or use *Toggle*.
3. Adjust **Sensitivity** until small wrist movements move the crosshair the right amount. If the direction
   is reversed for you, flip **Invert X** or **Invert Y**.
4. Set **Anti-dead zone** to about the game's own stick dead zone so tiny movements register.

The gyro adds to the right stick, so the stick still handles big turns.

## Keyboard / mouse binds, toggle and turbo

Every button in the **Buttons** tab can send a controller button, a keyboard key or a mouse button:
- **Toggle:** tap once to hold the bind, tap again to release it.
- **Turbo:** repeats the press, with a *ms between presses* slider from 1 to 100 ms. The triggers have their own turbo in the Triggers tab.

Keyboard and mouse binds use Windows `SendInput` with hardware scan codes, which games read. Some
anti-cheat systems ignore injected keyboard/mouse input, and turbo or rapid fire is banned in many
ranked modes.

## Automatic updates

Every push to the default branch runs [`release.yml`](.github/workflows/release.yml). It builds and
tests Windows and Linux binaries, then publishes a GitHub release `v<VERSION>.<run number>` with
a `SHA256SUMS.txt`.

On start, EdgePad asks the GitHub API for the latest release. If it's newer, EdgePad downloads the
build for your platform, checks it against the published SHA-256, and swaps it in place of the
running exe. The new version runs after a restart (the app offers a *Restart now* button).

- Turn it off with **Settings → Install updates automatically**, or start with `--no-update`.
- Bump the `VERSION` file (e.g. `0.2`) for a new minor or major version. The patch number comes from CI.
- Local development builds never replace themselves.
- Dependabot keeps the GitHub Actions up to date. Each merge ships a new release, which reaches every user through the updater.

## Building from source

Requirements: CMake 3.21+, a C++20 compiler (Visual Studio 2022, GCC 11+ or Clang 14+) and git.
All libraries are fetched and linked statically by CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

On Linux, first install the build dependencies:
`sudo apt install libudev-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev`.

Try the UI without a controller with `EdgePad --demo`, which simulates one.

### Command line

```
--headless          run without a window (Ctrl+C to quit)
--no-update         skip the automatic update check
--data-dir <path>   store settings somewhere else
--demo              simulate a controller (try settings without one)
--list              list connected controllers and exit
--version           print the version and exit
```

### Project layout

```
src/core/       platform independent: stick/trigger processing, RC filter, remapping pipeline,
                DualSense HID protocol, virtual DualSense (descriptors, reports, USB/IP protocol),
                virtual controller reports and motion calibration, config JSON, versions, SHA-256
                (unit tested)
src/platform/   hidapi / exclusive HID device, controller hiding (HidHide, input grab), virtual controllers
                (USB/IP server + usbip-win2 or UHID for the DualSense, ViGEmBus, uinput), WinHTTP / curl,
                portable paths, self-update
src/app/        engine thread, updater, config store, Dear ImGui interface, main
tests/          doctest unit tests
```

## Notes

EdgePad is not affiliated with Sony Interactive Entertainment or GameSir. DualSense and DualSense Edge
are trademarks of Sony Interactive Entertainment. Third-party licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
