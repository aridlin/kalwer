# Fred native popup (Linux)

`/fred` opens a 550 × 486 logical-pixel popup, with Fred's original 256 × 192
framebuffer displayed at 2× using nearest-neighbour scaling. It ejects from the
search bar, unfolds, then eases into the monitor corner. PTY popups stay beside
the search bar. Focus loss pauses Fred and clears held keys.

Enter starts; arrows or Q/W/R/E move; Space or T fires; Escape closes.
The simulation uses the upstream native app's 180 ms logical tick. This is the
GitHub native core, not an emulator. Upstream's interactive backend has no sound;
this adapter likewise does not add sound or change gameplay.

The adapter pins [aridlin/fred-native-port](https://github.com/aridlin/fred-native-port)
at `4a7c0d86832cd087dff0c37b6f9520c8cb757e84`.

Build with CMake, a C++ compiler, Python/pip and a **user-provided** original
reference matching the native port's manifest:

```sh
python3 fred/build.py --reference '/path/to/Fred (Quicksilva).tzx' --install
make
```

The script verifies the reference SHA-256, generates data only in ignored build
directories, and installs a local module under
`$XDG_DATA_HOME/kalwer/fred` (default `~/.local/share/kalwer/fred`). A clean existing
checkout can be supplied with `--source`. It handles one stale public-metadata
hash in that revision without relaxing checks on original/generated game data.

**Do not distribute the generated module, build directories, or original assets.**
Normal Kalwer builds and releases contain only the adapter interface. Without a
locally installed module `/fred` explains how to set it up.
