QUAHOG 0.4.4 - AUTOMATIC PYTHON SETUP
Author: Randyadr

This edition uses installed 64-bit Python 3.10 or newer. If none is detected,
it automatically downloads and installs official Python 3.14.10 for the
current Windows user, then continues preparing the maps. A progress window
is shown; no manual Python download, setup choices or Enter key is needed.

The ZIP still contains no Python runtime, EXE files, or Python extensions.
The native quahog_native.dll is required by the game. Internet access is
needed only for missing-Python installation; existing compatible Python
and prepared maps work offline. Preparation needs no pip packages.

QUAHOG TRAVEL
F6 enters Quahog from Plaza; F8 returns; F10 rescues at the Quahog entrance.
Walking into the cyan portal enters; the cyan portal by the Quahog spawn
returns to Plaza. Return keeps the Plaza episode and places you outside the
entry portal. If automatic placement is unavailable, F6 still works and
returns to where you entered from.

MARKET WALL
Preparation looks for the gap between the coconut and papaya stalls in the player's own
Plaza scene, then checks the large wall and floor collision for a location
slightly in front of that wall. This targets the marked market-wall area.
The screenshot alone does not provide exact world coordinates, and actual
retail/Eclipse placement has not been verified here. If detection fails, the
HUD says to place the portal; if alignment is off, use the precise placement
button below. No arbitrary coordinate is silently used as a market portal.
Quahog overrides ONLY dolpic_ex0 (an existing secret-stage slot). It does not
modify any Plaza archive.
The ordinary dolpic_ex0 entrance also reaches Quahog while enabled.


AUTOMATIC PYTHON SETUP
1. Enable Quahog and press Play as usual.
2. If Python is missing, the mod shows download/verification progress and
   runs the official installer with visible progress for your Windows user.
3. After installation, map preparation continues automatically in the same
   game process. Later launches reuse Python and do not reinstall it.

Registered installations are checked first, followed by real interpreter
files on PATH. Their x64 architecture and version resource are checked so
an old Python or empty Store alias does not prevent automatic installation.
The installer is downloaded directly from python.org over HTTPS. Both the
pinned official SHA-256 and Windows Authenticode validation must pass before
it is run. An invalid download/signature stops setup; it is never bypassed.
The mod waits if another Quahog launch is already installing Python.

Installation is per-user, without a Python launcher, PATH/file-association
changes, Start menu shortcuts or pip packages. Python remains installed
outside the mod folder and can be removed via Windows Installed apps.
This is a normal installed Python, not an embedded runtime in the mod ZIP.
The downloaded installer is removed from the temporary directory afterward.

Close the download window to cancel. During installation, use the official
installer's Cancel control if available. Closing the game/setup status window
stops Quahog startup after the installer finishes; it never forcibly kills
an installation. An interrupted download is discarded. Relaunch to retry.
Network/certificate failures show an error; keep Windows' security checks on.
If Windows reports a restart is required and Python cannot yet be found,
the mod asks you to restart Windows. It never restarts Windows itself.

CUSTOM PYTHON / OFFLINE SETUP
You can still install a compatible Python yourself from:
https://www.python.org/downloads/windows/
Close and reopen SMS Launcher afterward. For a portable/custom interpreter,
create python-path.txt beside mod.ini containing just its full python.exe
path, without arguments. Example (use your actual path):
C:\Users\YourName\AppData\Local\Programs\Python\Python314\python.exe
Save as UTF-8; optional surrounding quotes work. The executable must be
outside the mod folder. This explicit selection takes priority over automatic
setup. A stale/invalid custom path reports an error instead of installing a
different Python; fix or remove python-path.txt to resume automatic detection.
The preparation script also checks the interpreter's actual version/bitness.

STARTUP
The v0.4.1 startup cleanup is retained: no game self-relaunch, hidden game
window, suspended subprocess, process job or forced process termination.
No antivirus settings or exclusions are changed. The native DLL is unsigned;
this package has not been scanned with Defender here.

FIRST LAUNCH / MEMORY
If the game has less than 512 MB of map memory, this version saves
SMS_MEM_MB = 1024 in the active game settings.txt and stops loading Quahog for
that launch. Close the game and press Play again once. The original settings
are backed up in this mod's data folder; the message and startup.log give both
paths. Other settings are preserved. If a sufficient value already exists,
it is left unchanged and the message identifies a possible environment
override; close/reopen SMS Launcher and check that override if this repeats.
If no active settings file can be identified, no arbitrary file is written;
the dialog explains the manual setting instead. The game's EXE and ISO are
never changed. The memory setting remains until you change or remove it.

INSTALL / UPDATE / SHARE
Close the game. Open SMS Launcher > Settings > Mods > Open mods folder.
For a new install, extract quahog-native from this ZIP into that Mods folder.
For an existing Quahog install, remove ONLY the old quahog-native/runtime
folder, then copy this ZIP's quahog-native into Mods, merge folders and
replace files. Simply overwriting does not remove the old bundled Python
files. Keep your mod data folder and any custom python-path.txt separately.
Disable/remove duplicate copies of older Quahog mods.

Use the full ZIP for installs and updates. Share it as supplied,
not a ZIP of an old folder that might still contain runtime executables.
Use either this edition or another Quahog edition, not both; they share
the same quahog-native identity. No game recompilation or manual ISO selection
is needed. Saved portal placement is retained. Updated setup tools rebuild prepared
maps automatically on the next launch.

VISIBLE PORTALS
Cyan Quahog portals use animated filled rings and moving swirls drawn
at the very same world coordinates used by the walking trigger. Drawing is
separate from optional HUD text. A missing required drawing API produces an
explicit startup error instead of silently leaving invisible triggers.
The SDK draws these graphics over the scene, so they can show through walls
when nearby. They are visible from both sides. They are not depth-buffered
native BMD meshes. Preview.svg uses the actual portal drawing code with an
offline camera; it is not a screenshot from the game.

PRECISE PLACEMENT
In Delfino Plaza, stand at the desired spot, face the wall, open F1 > Mods >
this mod > Place portal here. The portal is put at your feet, facing outward,
and saved immediately. Close F1 and step away; the trigger has an eight-second
unpaused grace period. This works without a separate hotkey or coordinates.
A manually saved position takes priority over automatic detection.
Travel buttons in F1 are queued until the menu closes. Walking through the
portal does not require a keyboard, and return has a cooldown to prevent loops.

UNCHANGED REQUIREMENTS
Windows x64 native API 1.x with the checked base functions and named disc/memory
symbols; official compatible Sunshine GMSE01 or Eclipse GMSE04 disc, single
player. Older API 1.3/1.4 host layouts are checked. Enable before launching.
The old full Skin Selector with built-in world hooks still conflicts. The
separate-world Skin Selector automatic mod is compatible; F2-F5 remain free.
The first launch may update the single memory setting described above and
require one manual game restart. The game executable and source ISO are unchanged.
Restart after enabling/disabling a map; a loaded stage cannot be removed live.

DIAGNOSTICS
python-bootstrap.log records the automatic install; python-install.log and
its companion installer logs contain Python setup details.
startup.log and automatic-setup.log are in this mod's data folder, as reported
by the startup dialog. Cache: prepared-v4. Damaged/changed cache is rebuilt.
portal-layout.txt in the prepared Quahog cache records any automatic market
position. preparation-report.json includes the placement method or null when
unresolved. Saved custom placement is quahog-portal.txt in this mod's data
folder. Delete that placement file to reset it.

VALIDATION / LIMITS
The Quahog Windows x64 DLL was compiled with warnings treated as errors. Offline
checks cover independent stage overrides, isolated asset sets, archive and
collision parsing, market wall/floor detection on authored geometry, cache
reuse, actual drawing callbacks, both portal directions, pause and cooldown,
saved placement/reload, fall rescue, rollback and real API 1.3/1.4 struct sizes.
The automatic-install state machine is tested with fake success, error,
cancellation and concurrent-install outcomes. The official installer URL and pinned hash are unchanged from v0.4.3;
the installer was not downloaded again for this release. Windows download/signature/install execution,
Python discovery, game execution, actual market alignment and
in-game rendering were not available here. External-interpreter preparation
and cache reuse were exercised with authored miniature test discs on Linux. This is a test build. The native maps still do not include
the old custom missions, NPCs, dialogue or Shine rewards.

CREDITS
Converted world assets originate from the supplied Skin Selector 2.14.165.
Third-party asset ownership is not claimed. SDK and compiler-library licenses are kept
under licenses/. Python itself is installed separately. Mod work and integration: Randyadr.
