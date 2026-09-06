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
- Shift+Enter persistently favourites or unfavourites an Elephant result and
  immediately moves the pinned group to the top.
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
