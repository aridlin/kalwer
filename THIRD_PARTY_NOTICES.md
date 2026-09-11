# Bundled Koom runtime and data

Kalwer's native launcher communicates over pipes with a separate Doomgeneric
executable. The Doom engine is not linked into the launcher.

* Doomgeneric, including its Doom and Chocolate Doom code: GPL-2.0-or-later.
  Source and license: `vendor/doomgeneric/`. Upstream revision and build details
  are recorded in `vendor/doomgeneric/README.kalwer.md`. Kalwer's helper adapter
  is `koom/host.c`, under the same license; `koom/build.py` builds the helper.
* Freedoom Phase 2, version 0.13.0: freely redistributable game data under the
  terms in `assets/koom/COPYING.txt`. Contributor and music credits are in the
  accompanying `CREDITS.txt` and `CREDITS-MUSIC.txt`.

The executable embeds the helper and Freedoom data, extracting them only when
Koom starts. Corresponding helper source and build scripts are included in this
repository and its release source archives. Commercial Doom game data is not
included. Imported WADs remain in the user's local Kalwer data directory.
