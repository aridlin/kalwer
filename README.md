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

- GPU-backed, animated halftone coverage over the fully rendered interface.
- Scrollable Elephant application search with persistent, unlimited favourites
  grouped above normally ranked results.
- Interactive Zsh command sessions, background jobs, completion notifications,
  output copying, and live handoff to Ghostty.
- Firefox Google search mode and a native precedence-aware calculator.
- Warm resident process with three-second query and selection restoration.

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

![Kalwer calculator decimal, fraction, mixed-number and percentage results](screenshots/calculator.png)

GTK/Cairo remains responsible for font shaping, themed icons, and the finished
UI texture. `GtkGLArea` handles the per-frame ordered dot-density field,
radius falloff, selection outline, alpha compositing, and opening reveal.

The backend request is run asynchronously as
`elephant query --json --async=false`, and activation is delegated back to
Elephant with the result's provider, identifier, and first/default action.

## Installation

On Arch Linux, install `kalwer` from the AUR. For a source build, install a C++20
compiler plus GTK3, JSON-GLib, libepoxy, VTE3, pkgconf, and make, then run:

```sh
make
sudo make PREFIX=/usr install
sudo install -Dm644 kalwer.service /usr/lib/systemd/user/kalwer.service
systemctl --user enable --now kalwer.service
```

Optional runtime integrations are Elephant, tmux, libnotify, Firefox, Ghostty,
and Hyprland. A typical resident activation binding is:

```ini
bindd = $mainMod, E, App launcher, exec, /usr/bin/gdbus call --session --dest pl.aridlin.ElephantField --object-path /pl/aridlin/ElephantField --method org.gtk.Application.Activate '{}'
windowrule = border_size 0, match:class elephant-field
windowrule = no_shadow on, match:class elephant-field
```

Kalwer is released under the MIT License.
