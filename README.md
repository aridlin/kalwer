# Kalwer

A resident native GTK3/Cairo frontend for Elephant, visually paired with
Workspace Field. The complete launcher is rendered to an offscreen ARGB
surface first, uploaded only when its content changes, then exposed through a
GPU fragment-shader halftone coverage mask.
The circles in the mask never carry colors of their own: every visible pixel
comes from the finished launcher below it.

![Kalwer app search revealing through its GPU halftone mask](screenshots/search.png)

![Kalwer interactive command popup](screenshots/terminal.png)

## Features

- System-wide `:` file search through Everything on Windows and plocate on Linux.
- GPU-backed, animated halftone coverage over the fully rendered interface.
- Scrollable Elephant application search with persistent, unlimited favourites
  grouped above normally ranked results.
- Interactive Zsh command sessions, background jobs, completion notifications,
  output copying, and live handoff to Ghostty.
- Firefox Google search mode and a native precedence-aware calculator.
- Warm resident process with three-second query and selection restoration.
- GitHub release updates on Linux and Windows with a post-update banner.
- Nine desktop popup games, a permanent koin wallet, and optional minigame unlocks.

## Build and run

```sh
make
./elephant-field
./elephant-field --daemon
```

The resident process owns `pl.aridlin.ElephantField`; activating that
application ID toggles the window without constructing a new GTK process.

## Controls

- Type to search Elephant's `desktopapplications` provider.
- Up/Down or Ctrl+P/Ctrl+N moves the selection.
- Left/Right, Home/End, Shift-selection, and Ctrl+A/C/X/V edit the search text.
- Enter launches the selected result.
- Ctrl+Shift+Enter toggles a favourite and moves it into the pinned group.
- Shift+Enter opens the multiline editor. Enter submits; Shift+Enter inserts a
  newline. Escape keeps the draft. This also works in a running PTY session.
- Escape closes the launcher.
- Mouse hover and click work on result rows.
- Five on-screen rows are interactive. Mouse wheel, arrows, and Page Up/Down
  scroll later results into those selectable positions; the lower rows are a
  non-interactive halftone preview.
- `> command` runs immediately in an interactive VTE popup. Tab and Shift+Tab
  cycle Zsh-resolved command/path completions.
- Command popups can copy output, continue the same tmux session in Ghostty, or
  detach into the background. The matching shortcuts are Ctrl+Shift+C/G/B.
- `<` lists running background commands; selecting one reattaches its live popup.
- Background completion sends a success or failure desktop notification based
  on the process exit code.
- `? search` opens Google in Firefox after switching to the most recently used
  Firefox workspace.
- Arithmetic is evaluated locally with normal precedence, parentheses, powers,
  percent-of, `sqrt(...)`, and `√`. Decimal is the first result, followed by
  reduced fraction, mixed-number, and percentage forms when applicable.
- Reopening Kalwer within three seconds restores the exact query, result
  selection, and scroll position.
- `/settings` opens persistent controls for prompt retention, the PTY connector
  and vertical-expansion durations, and the finished-command auto-close delay.
- `/exit` closes the resident Kalwer process cleanly.

![Kalwer calculator decimal, fraction, mixed-number and percentage results](screenshots/calculator.png)

GTK/Cairo remains responsible for font shaping, themed icons, and the finished
UI texture. `GtkGLArea` handles the per-frame ordered dot-density field,
radius falloff, selection outline, alpha compositing, and opening reveal.

The backend request is run asynchronously as
`elephant query --json --async=false`, and activation is delegated back to
Elephant with the result's provider, identifier, and first/default action.

## Installation

The [latest GitHub release](https://github.com/aridlin/kalwer/releases/latest)
includes both `kalwer-linux-x86_64` and `kalwer.exe`. The Linux build is the
normal optimized, unstripped executable and uses the GTK3/VTE runtime libraries
listed below.

On Arch Linux, install `kalwer` from the AUR. For a source build, install a C++20
compiler plus GTK3, JSON-GLib, libepoxy, VTE3, pkgconf, and make, then run:

```sh
make
sudo make PREFIX=/usr install
sudo install -Dm644 kalwer.service /usr/lib/systemd/user/kalwer.service
systemctl --user enable --now kalwer.service
```

The standalone Linux build uses `curl` as its background HTTPS update transport;
package-managed installs in non-writable system directories are left to their
package manager. Optional runtime integrations are Elephant, tmux, libnotify,
Firefox, Ghostty, and Hyprland. A typical resident activation binding is:

```ini
bindd = $mainMod, E, App launcher, exec, /usr/bin/gdbus call --session --dest pl.aridlin.ElephantField --object-path /pl/aridlin/ElephantField --method org.gtk.Application.Activate '{}'
windowrule = border_size 0, match:class elephant-field
windowrule = no_shadow on, match:class elephant-field
```

Kalwer is released under the MIT License.

## Android preview

The native [Android port](android/README.md) opens as a fullscreen translucent
launcher, ready for a side-button **Open app** binding. It searches installed apps,
keeps favourites, calculates locally, and defaults to Google on Enter when no
local results match. `>` provides phone actions in place of the desktop PTY.
Build the installable APK with `cd android && ./gradlew :app:assembleDebug`.

## Windows prototype

The native Windows target lives in `windows/`. It uses Win32 for its resident
single-instance process and global `Alt+Space` binding, Direct2D/DirectWrite to
render the complete launcher, and Direct3D 11 plus DirectComposition to apply
the same GPU halftone coverage mask to that finished interface.

Cross-compile it from Linux with MinGW-w64:

```sh
cmake -S windows -B build-windows -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
```

Run `kalwer.exe` once to keep it resident. `Alt+Space` toggles it; a
second invocation also toggles the existing instance. This early port already
supports Start Menu application discovery, fuzzy search, native edit/selection
keys and clipboard shortcuts, scrolling, persistent Shift+Enter favourites,
multi-form calculator queries, and `?` Google queries. `>` runs commands
directly in an embedded interactive ConPTY session: output streams into the
animated side panel, keyboard input is forwarded to the process, output can be
selected/copied, and completed commands retain their real exit code. `BG`
detaches a live command; `<` lists background commands and reopens their panel,
with a success/failure notification on completion. `/settings` adds the shared
timing/retention controls plus a current-user autostart toggle; it writes only
the `HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run` `Kalwer` value and
never requires administrator access. `/exit` terminates the resident instance.
Update checks run off the input/render thread. A newer `kalwer.exe` is staged
beside the running executable and applied automatically once commands finish and
the launcher is hidden; Kalwer relaunches itself. Both platform updaters require the matching release SHA-256 file
and validate the executable format before installation; a failed or incomplete
download leaves the current build intact.

### Antivirus reports

Windows builds are currently unsigned. Version information identifies the product,
but is not an Authenticode signature or a promise of antivirus acceptance.
If Defender blocks a download, leave protection enabled and do not add an exclusion.
Record the exact threat name, release, file SHA-256 and Defender intelligence version.
An on-demand scan on another machine or VirusTotal is not a guarantee that a
download/cloud or runtime check will agree. Suspected false positives should be
[submitted to Microsoft as a software developer](https://www.microsoft.com/en-us/wdsi/filesubmission).

The manual `Windows release Defender scan` GitHub workflow checks exact released
executables against updated Defender intelligence and logs their hashes. A missing
scanner or failed scan is an error, never a pass. It does not change exclusions or
disable protection. Disposable CI runners explicitly enable real-time and cloud
protection, allow safe sample submission to Microsoft, verify cloud connectivity,
and mark samples as Internet downloads. No sample is executed. This still does not
reproduce a particular browser or runtime behavior. The default local on-demand check
does not change these protection settings and can be run on Windows with
`./windows/scan-release.ps1 -ReleaseTag v0.5.1` from an administrator PowerShell.
Since v0.5.1, Kalwer no longer bundles or extracts the Everything installer;
`/index-setup` opens the vendor's download page. This reduces unnecessary executable
bundling, but does not establish the cause of historical detections.

## System-wide file search (Linux and Windows)

Type `:report` or `:projects invoice` to search files and folders across the system. Enter opens the selected result. Names and paths are searched, not file contents. Results prefer exact filenames, then name prefixes/substrings, then path matches. Up to 512 results are shown; narrow broad searches to find a specific file. A bare `:` shows index status. `/index` explains the active backend and `/reindex` requests a refresh.

**Windows uses [Everything](https://www.voidtools.com/)** through the official SDK's Unicode IPC protocol. Queries run off the UI thread with cancellation and bounded waits. An existing running Everything instance is reused; an installed instance is started in the background when needed. If Everything is missing, run `/index-setup`: its official download page opens in your browser. Install it normally and keep its service enabled. Kalwer does not embed or extract its installer. It indexes NTFS/ReFS volumes and maintains live changes; configure Everything's folder indexing for other filesystems and network shares. SDK license provenance is in [`vendor/everything`](vendor/everything/README.md).

**Linux uses [plocate](https://plocate.sesse.net/)**. The first query starts an incremental `updatedb` scan of `/` as your normal user, including home subvolumes and mounted local drives. The old system database can supply initial results while the private index builds. Virtual/network filesystems and `.snapshots` are excluded. Permission-protected directories remain inaccessible. The private database lives in `$XDG_CACHE_HOME/kalwer/system.plocate` (normally `~/.cache/kalwer`) with owner-only access. Refreshes run every 15 minutes while Kalwer is running and reuse unchanged directory metadata. `/index-setup` installs plocate through your distribution's package manager if needed; `/reindex` then starts indexing. Quotes group Linux search terms, and standard locate wildcards are supported.

The former SQLite directory crawler is no longer linked or used. Its old `files-v2.sqlite` database and `file-roots.txt` setting are ignored; they may be removed with Kalwer stopped. No existing Everything configuration or system locate database is overwritten.

Regression checks:

```sh
g++ -O2 -std=c++20 -pthread tests/system_file_index_test.cpp -o /tmp/kalwer-system-index-test
/tmp/kalwer-system-index-test
```

## Commands, completion and reusable popups

Type `/` for the command list. `/help` opens scrollable help in the animated
right-side popup; `/about` opens information. `/files`, `/apps`, `/web`, `/terminal`
and `/jobs` switch modes. `/reindex` requests a background refresh. `/settings`
and `/exit` remain available. Up/Down chooses a command; Enter runs it.

Gray inline ghost text previews a matching slash command, application name or
file-name prefix when the caret is at the end. Tab accepts the suggestion.
Suggestions do not alter the input until accepted. Linux's existing `>` shell
completion continues to handle command arguments.

Both hosts expose `open_popup(kalwer::PopupDocument{title, body})` for plain text,
using the same animated frame and copy/close interactions as terminal output.
Text popups do not create a shell or background job, do not auto-close, and
support selection, copying and scrolling. Terminal-specific buttons appear only
for terminal sessions. The shared `launcher_commands.hpp` catalog generates help.
Linux retains the `Kalwer Command Output` window title for compatibility with
existing Hyprland right-side placement rules; the visible header uses the document title.

Android caches installed-app components and labels on disk as well as in memory.
Cold launches display the saved catalog before PackageManager discovery completes;
package and locale changes trigger background refreshes. Icon loading uses a
separate queue. Corrupt or wrong-language snapshots fall back to discovery, and
failed refreshes preserve a usable cached list.

## Automatic updates and the update banner

Updates download, verify and install automatically. There is no install-confirmation
prompt. Once the new version is running, a small `Updated to v…` box appears above
the search field on the first three launcher openings. The count persists across
restarts; a fresh installation does not show an update banner.

`/updates` shows the running version and latest update status, including failures
and package-manager-owned installations. Linux replaces the verified executable
on disk; Windows stages the verified executable. Both activate the update and
relaunch automatically once the launcher is hidden and active commands finish,
including background commands and administrator PTYs. No manual restart is needed.
Checks run at startup, hourly, and when /updates is opened. Android uses the system APK installer.

### Run with elevated privileges

Press **Ctrl+Enter** on an app or `> command`. Linux runs it through `sudo` in the PTY popup, including the password prompt; shell pipelines and redirections run inside the elevated shell. GUI apps remain subject to their own root and display-session restrictions. Windows uses the standard UAC prompt: apps use Run as administrator, and commands open a dedicated elevated Kalwer PTY window with the same right-side animation. Closing that window ends its elevated session; backgrounding is unavailable there. Packaged Windows apps that cannot run elevated show an explanation. Normal Enter keeps its usual behavior.

This release is delivered by the automatic updater, with the existing three-opening update banner.

`/updates` performs a fresh background check and updates its popup live. Resident desktop launchers also recheck hourly. Concurrent requests share the active check, preventing duplicate downloads.

On Windows, Alt+Space closes an open popup even while another app has focus (same action as its × button, stopping an active command). Clicking Kalwer restores keyboard input so Escape works again.

Popup dismissal reverses its opening timeline: the full contents compress vertically into the horizontal line, which retracts before the launcher fades away from the results toward the search bar. Text is transformed rather than rewrapped into a smaller layout. This also applies to help and other text popups.

## Native popup games (v0.6.0)

Type `/snake`, `/minesweeper`, or `/peggle` and press Enter. The launcher hides
and the animated command-output popup moves to the top right and stays open until you explicitly close it with
Escape or the popup close button. These are custom C++ games drawn with Cairo
on Linux and Direct2D on Windows; no terminal, browser, or external game is launched.

- **Snake:** arrows or WASD to steer, Space to start. Eat the orange food and avoid walls and your body.
- **Minesweeper:** reveal with a left click; flag with a right click. Arrows/WASD move the cursor, Space/Enter reveal, and F flags. Find 10 mines on a 9×9 board; the first reveal and its neighbors are safe.
- **Peggle:** an original peg-and-ball implementation with custom drawn graphics. Aim with the mouse or left/right arrows, then click the board or press Space/Enter to shoot. Clear all orange pegs with 10 balls. Catch a ball in the moving bucket to earn it back.

Press R or click Restart for a new round. Gameplay, elapsed time, balls, and the
bucket stop immediately when the game loses focus, then resume without catching
up missed time. Losing focus and finishing a round never close the popup. The
launcher shortcut brings the existing game back into focus. Automatic updates
can download in the background but wait until the game is closed to restart.
Desktop release assets and their SHA-256 files deliver these games through the
existing automatic updater. Android is unchanged.

Game-rule and focus-pause tests run on Linux and Windows in the Native games
workflow. Locally: `g++ -std=c++20 tests/games_test.cpp -o /tmp/games-test && /tmp/games-test`.

## Peggle, koins, and appearance (v0.7.0)

Peggle now has four rotating boards (Orbit, Cascade, Bloom, Fortress), round pegs
and angled capsule bricks, fixed-step collision solving, and a curved trajectory
preview using the same physics as the ball. Mouse/arrow aiming spans ±85°; Q/E
make fine adjustments. Green `+` pegs blast nearby targets, cyan ring pegs extend
the guide for three shots, green bucket pegs widen the catch for three shots,
and purple bonus pegs score extra. Orange-clear multipliers rise through
1×/2×/3×/5×/10×; a shot's combo increases every five hits up to 5×. A shot earns
free balls at 2,500, 7,500, and 15,000 points, plus one for a bucket catch. Hit
pulses, sparks, score labels, a short ball trail, and a recoiling launcher add
motion. All gameplay and effects pause when the popup is unfocused.

Wins award **koins** once per round: Minesweeper 30, a fully cleared Snake board
75, and Peggle 50 plus 5 per remaining ball. `/koins` shows the permanent device
wallet and win count. The balance appears in game and shop popup titles, never
in the search bar. `/shop` spends koins on optional unlocks. Resetting a game or loading a config
does not reset the wallet. Linux stores it in `$XDG_STATE_HOME/kalwer/koins-v1`
(normally `~/.local/state/kalwer/koins-v1`); Windows stores it in Kalwer's local
application data directory. No account or cross-device synchronization is used.

`/settings` (also `/config`) offers:

- Halftone (default), Atkinson, Floyd–Steinberg, Bayer 4×4, Bayer 8×8, Threshold.
- Forest, Amber, Glacier, Rose, Violet, and Mono color themes.
- Surface opacity (30–95%) and dither dot size (1–8, default 1).
- Independent launcher and popup dither selections, each with a Keep Halftone checkbox.
- Black-and-white backdrop checkbox. Threshold always uses pure black and white.

`/config-save` writes the current appearance preset; `/config-load` restores it.
These are editable `appearance.ini` and `preset.ini` files in Kalwer's config
folder. Wallet data is deliberately separate from presets. Halftone transparency
is applied to all five games without softening their text or game pieces.
The game popup has a defined border and a dark, subtly dithered translucent title.

Other dither modes process the actual **live underlying app windows** on
Hyprland, captured with its toplevel export protocol and composited in stacking
order, excluding Kalwer itself. Updates run off the UI thread at up to roughly
6 Hz. The capture worker only supplies raw frames; OpenGL 4.3 compute on Linux
and Direct3D 11 compute on Windows perform dithering at native resolution (Linux
supersamples for fractional display scaling). Atkinson and Floyd–Steinberg use
dependency-ordered error diffusion, not tiled approximations. Frames are immutable
and uploaded only when changed; rounded stipples are composed by the GPU.
Desktop wallpaper/layer surfaces outside app windows are not exported;
those regions use a neutral backdrop. Other Wayland compositors retain normal
transparency when this optional protocol is unavailable. Windows uses live
capture with Kalwer excluded (Windows 10 2004+); a failed exclusion prevents
capture to avoid visual feedback. This also excludes Kalwer from other screen
captures while a live dither mode is selected. Captured pixels stay in memory;
there are no screenshots written to disk or sent over a network.

The Linux game, popup title and border share one GPU canvas with a reusable glyph atlas.
Peggle caches its aim preview until the aim or board changes; collision checks
reject distant pegs before expensive capsule math. Rendering is capped near 60 Hz
for animated games and stops while unfocused. Static Minesweeper redraws on input
and timer changes. Windows uses Direct2D and cached text formats.

On Hyprland, compositor-wide opacity may otherwise make even controls transparent.
A popup-only rule can keep application alpha authoritative:
```ini
windowrule = opacity 1.0 override 1.0 override, match:title ^Kalwer Command Output$
windowrule = no_blur on, match:title ^Kalwer Command Output$
```
Third-party protocol notices are in `THIRD_PARTY_NOTICES.txt`.

Android v0.3.0 applies the themes, dither selection, and opacity throughout the
app. It uses a one-frame backdrop snapshot through Android's screen-sharing
consent flow, processes it with a GLES 3.1 compute shader, then stops capture.
No dither pixel loop runs on the Android UI thread. Devices without a compatible
compute context retain transparency. Refresh it in settings; choosing
Halftone needs no screen capture. Android settings can export/import a bounded
JSON configuration through the system file picker. Android currently has no
mini-games; its device wallet remains available for future features.


## Garden Defense, chess and the koin shop (v0.8.0)

`/pvz` opens **Garden Defense**, an original Plants vs. Zombies-style lane game
with native vector graphics. Defend five lanes through four waves in Meadow:
Sun plants produce collectible sun, Pea plants fire at approaching zombies,
Walls absorb bites, Frost slows enemies, and Burst clears nearby lanes.
Armored and fast zombies arrive in later waves. Each lane has one emergency
mower. Sun is a round-local planting resource, separate from permanent koins.

Select seeds with **1–5** or their cards, then click a bed to plant. Click suns
or press **Space** to collect them all. Arrows/WASD select a bed and **Enter**
plants there. **X** toggles the shovel; right-click also removes a plant.
Seeds recharge, and a wave break grants 50 sun. Win Meadow for 75 koins;
Moonlit Siege has stone beds, six waves and a 100-koin reward.

`/chess` starts White versus a compact local computer opponent. Click a piece
and a highlighted destination, or use arrows/WASD and Space/Enter. The game
supports check, checkmate, stalemate, castling, en passant, all four promotions,
automatic threefold/50-move draws, and common dead-material draws. Promotion
uses the displayed choices or **Q/R/B/N**; R chooses a rook while that dialog
is open. Otherwise **R** starts a new game. **H** (or the label below the board)
starts a new game in computer/two-player mode. Beat the computer for 60 koins;
local two-player games and draws do not award koins. The computer evaluates
bounded branches across focused ticks and stops thinking while unfocused.

`/shop` uses the same GPU popup and shows five permanent unlocks:

| Unlock | Koins | Effect |
| --- | ---: | --- |
| Snake wraparound | 3,000 | Optional wrap rules; a full board earns 30 koins |
| Moonlit Siege | 7,500 | Six-wave garden with stone planting beds |
| Walnut chess board | 1,000 | Wood colors and ivory pieces |
| Prism Peggle board | 5,000 | Extra peg arrangement and angled brick arcs |
| Aurora celebrations | 1,500 | Pink/blue victory effects across the games |

Click a card or select it with arrows and press Enter to buy. Purchased items
are equipped immediately; selecting an owned item toggles it without spending
again. Gameplay options apply to the next round. All base games and retries
remain free. Purchases, ownership, equipped items and the remaining balance
are committed together in the existing wallet file; old balances migrate on
load. Appearance presets never include or reset purchases.

Koins appear in game/shop popup title bars and `/koins`, not the launcher search
bar. The outer padding remains clear, and backdrop capture/compute waits until
opening animations settle. Games still use the existing animated output-popup
medium without starting a shell, remain open when finished, and pause on focus
loss. Android v0.3.1 removes the wallet from its launcher header and settings
title; minigames and the shop remain desktop-only.

### Windows search and settings (v0.8.2)

App matching and sorting run on a worker with coalesced requests; stale replies
are discarded. Shell icons load on a separate worker, with only bitmap uploads
and drawing on the UI thread. Updated result lists slide in over 160 ms.
`/settings` and `/config` open a separate settings popup with checkboxes,
dropdowns and numeric controls, keyboard navigation and automatic saving.


## Desktop arcade expansion (v0.9.0)

`/games` opens the game catalog. Four additional games can be permanently
unlocked in `/shop` using koins earned inside Kalwer. There are no real-money
purchases. Existing upgrade toggles remain available; purchased games remain
unlocked and retries cost nothing.

| Game | Unlock | Controls and rewards |
| --- | ---: | --- |
| Tetris | 6,000 | Arrows/WASD move and rotate, Z rotates backwards, Space drops. Clear 40 lines; speed increases with level. Runs earn up to 250 koins from lines and score. |
| Breakout | 8,000 | Mouse or arrows move the paddle; click/Space serves. Three brick boards, three lives, up to 250 koins per run. |
| Kar | 12,000 | C chooses a car before starting, Enter/Space starts, arrows/WASD steer and brake, Space/Up activates nitro. Three laps, traffic, drift cash, police pursuits and roadblocks. Podium finishes earn koins. |
| Koom | 25,000 | Freedoom with native Doom-compatible gameplay, music and sound effects. Completed maps earn 100 koins. |

Kar uses native C++ fixed-point movement, course geometry, staged nitro, impact
responses and police fines, with original programmatically drawn artwork.
Race cash is separate from the permanent koin wallet: an arrest takes a quarter
of the current race cash. Game time, police timers and input pause on focus loss.

Koom includes Freedoom: Phase 2 and runs the separate bundled Doomgeneric helper
inside the normal animated corner popup. No terminal window or external game
window opens. Arrow keys move/turn; WASD moves/strafes; Space/Ctrl fires; E uses;
1–7 changes weapons; Q opens the Doom menu. Escape closes the Kalwer popup and
R returns to WAD selection. Music and game simulation stop advancing unfocused.

Use `/wad-import "path/to/game.wad"` to validate and copy a user-provided IWAD or
PWAD into Kalwer's data directory. Left/Right selects an installed WAD; Tab
chooses its base IWAD when loading a PWAD; Enter starts. Commercial game data is
user-provided. Compatibility follows the bundled classic Doom engine, so mods
requiring other source-port extensions are not supported. Third-party licenses
and the freely distributable soundfont are documented in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Normal launcher use also earns small rewards: a successful application launch
adds 2 koins, opening a file result adds 1, and copying a calculator result adds
1. Re-copying the same calculator query during the same launcher session does
not award again. Typing and searching never award koins. Balances stay in game
and shop title bars and `/koins`, outside the search bar.

Calculator results group integer digits with apostrophes, such as
`1'234'567.89`. Large and small values also have a scientific representation;
copying keeps a plain reusable number. Decimal, fraction, mixed-number and
percentage forms remain available.

Mobile minigames follow completion of the desktop release; this desktop update
does not add games to the existing Android app.


### Daily game trials (v0.9.1)

Each locked game—Tetris, Breakout, Kar and Koom—has its own free 10-minute
allowance every local calendar day. Open it from `/games` or its command and
press Enter/Space or click Play Trial. The popup title shows the time remaining.
Only focused play uses time; loading, finished rounds and unfocused popups do
not. Usage survives normal closes and restarts. When time runs out, the game
pauses and its popup stays open. Permanent Koin purchases remove the limit.
