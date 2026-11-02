QUAHOG 0.5.1 - NATIVE C
Author: Randyadr

Python is completely removed. The map preparation, cache, archive handling,
portal placement and game plugin are compiled C11 inside quahog_native.dll.
No Python installation, Python download, bundled interpreter, pip package,
setup EXE, internet connection or game rebuild is required.

INSTALL / UPDATE
1. Close the game.
2. Open SMS Launcher > Settings > Mods > Open mods folder.
3. Copy this ZIP's quahog-native folder into Mods. For an existing install,
   merge folders and replace files. Keep your existing data folder and saved
   quahog-portal.txt placement if present.
4. Enable Quahog and press Play. Do not enable another Quahog copy alongside it.

Old tools/, runtime/, src/*.hpp, src/*.cpp, python-path.txt and
python-installer-source.json files from previous versions are no longer used.
You may remove those obsolete files from this mod's folder. Do not delete
shared Python installations: this update simply stops using Python.
The supplied ZIP itself contains no Python or C++ source/runtime files.

LARGE SKIN SELECTOR BUILD FIX (0.5.1)
Version 0.5.0 accidentally applied the 256 MB map-asset cap to sms.exe during
symbol lookup, which could report "Invalid file size" for a rebuilt Skin
Selector game. This version inspects the executable through a read-only,
64-bit file mapping. Large executable/debug files are not copied into the
map preparation heap. The 256 MB safety cap still applies to individual
map assets, where it belongs. No EXE or game build is modified.

STARTUP
The DLL reads stage data through the active game's file reader and prepares
only the existing dolpic_ex0 stage slot. It does not modify the ISO, game EXE,
source code, stage table or Plaza archive. Map models and collision are the
same assets as v0.4.3. This is a code/dependency conversion, not a map redesign.

The first launch prepares a new prepared-c-v5 cache inside the game process.
The old Python-era prepared-v4 cache is ignored and may be removed later.
A brief startup pause may occur while preparing/validating the archive.
Later launches reuse the verified archive. There are no helper processes,
background scans or periodic preparation/file reads during ordinary gameplay.
Files are written only during startup or when you explicitly save a portal.

If map memory is below 512 MB, the mod preserves a settings backup, writes
SMS_MEM_MB = 1024 to the active game settings.txt and asks you to close the
game and press Play once more. Other settings are preserved. If enough
memory is already available, no settings change or extra restart is needed.
An existing sufficient setting with insufficient actual memory reports a
possible launcher/environment override instead of repeatedly rewriting it.

CONTROLS
F6: enter Quahog from Delfino Plaza.
F8: return to the Plaza episode you entered from.
F10: rescue to the Quahog entrance.
Walk through the cyan portal to travel without a keyboard.
F1 > Mods > Quahog also has Enter, Return, Place and Rescue actions.
Travel selected in F1 takes effect when you close that menu.

PORTAL PLACEMENT
Your existing saved quahog-portal.txt takes priority. Otherwise, the native C
locator searches your game's Plaza scene for the coconut/papaya market gap
and checks nearby large-wall and floor collision. If it cannot resolve the
spot, F6 still works and the HUD offers manual placement.

For precise placement, stand at the desired location facing the wall, then
choose F1 > Mods > Quahog > Place portal here. The portal faces outward.
Close F1 and step away; an eight-second unpaused delay prevents instant entry.
The placement is saved immediately. Return places you outside that portal.
The cyan rings and swirls use the same position as the walking trigger.
They are projected native API graphics, so they can show through nearby walls.
Preview.png is the original offline portal rendering, not an in-game screenshot.

COMPATIBILITY / LIMITS
Windows x64 Sunshine PC port with native mod API 1.x and the existing named
port_dvd_read_file, port_dvd_override and port_mem1_size host symbols.
Official compatible Sunshine/Eclipse layouts are detected from the active
stage table. Single-player only. Game memory must be at least 512 MB.
Old combined Skin Selector world hooks conflict; the separate-world edition
is supported. F2-F5 remain free. No functions are detoured or patched.
Enable before loading the target stage. If the ordinary secret stage is
already loaded, restart with Quahog enabled so it can load the replacement.
Disabling restores the original file override; a stage already in memory
still needs a stage transition/game restart to change its loaded geometry.
The original custom missions, NPCs, dialogue and Shine rewards are not added.

DIAGNOSTICS
startup.log and automatic-setup.log are in the mod's game-provided data folder.
Startup errors name the log path. The compiled DLL checks shipped assets
against pinned SHA-256 hashes when building, validates scene/archive framing,
and verifies every assembled resource before publishing an archive.
Cache files are keyed to the original stage, stage table, common scene and
assembler version. Corrupt/missing cache files are rebuilt. A bad build never
activates a partial map. Original bytes are restored on unload/load failure.
Process-owned byte buffers remain valid across F1 disable/re-enable and are
reclaimed by Windows when the game exits.

VALIDATION
Windows x64 DLL compiled as C11 with warnings treated as errors.
Sparse executable tests passed at 300 MiB and 3 GiB, including COFF symbols
located beyond 2 GiB. Each mapped file used less than 4 KiB of preparation
heap. Mapping transfer and cleanup after parser/open failures were checked.
C preparation matched the old Python assembler byte for byte on an authored
stage fixture with the real Quahog assets. Hashes, scene/RARC/Yaz0 framing,
stage indices, portal geometry, cache reuse and corruption recovery passed.
The actual plugin's callbacks passed simulated API 1.3, 1.4 and 1.5 hosts:
portal draw, travel, correct return episode, pause/cooldown, saved placement,
fall rescue, unload rollback and no gameplay background file reads.
GCC address/undefined-behavior checks passed on the authored fixtures and
800 mutated parser inputs. See tests/last-checks.log and VALIDATION.json.
Windows game execution, Windows file operations and Defender were not tested
here. The DLL is unsigned. This is a test release pending your in-game check.

CREDITS
Converted assets originate from the supplied Skin Selector 2.14.165 package;
the geometry and textures are unchanged from the supplied Quahog v0.4.3.
Third-party asset ownership is not claimed. The game SDK and compiler-library
licenses are retained under licenses/. Mod work and integration: Randyadr.
