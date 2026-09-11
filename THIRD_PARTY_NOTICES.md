# Bundled Koom runtime and data

Kalwer's native launcher communicates over pipes with a separate Doomgeneric
executable. The Doom engine is not linked into the launcher.

* Doomgeneric, including its Doom and Chocolate Doom code: GPL-2.0-or-later.
  Source and license: `vendor/doomgeneric/`. Upstream revision and build details
  are recorded in `vendor/doomgeneric/README.kalwer.md`. Kalwer's helper adapter
  is `koom/host.c` and `koom/audio.c`, under the same license; `koom/build.py` builds the helper.
* Freedoom Phase 2, version 0.13.0: freely redistributable game data under the
  terms in `assets/koom/COPYING.txt`. Contributor and music credits are in the
  accompanying `CREDITS.txt` and `CREDITS-MUSIC.txt`.

The executable embeds the helper and Freedoom data, extracting them only when
Koom starts. Corresponding helper source and build scripts are included in this
repository and its release source archives. Commercial Doom game data is not
included. Imported WADs remain in the user's local Kalwer data directory.

* miniaudio 0.11.25: MIT license selected from its dual-license terms;
  see `vendor/miniaudio/LICENSE` and `SOURCE`.
* TinySoundFont: MIT, and TinyMidiLoader: zlib license (in `tml.h`).
  Pinned upstream revision and license: `vendor/TinySoundFont/`.
* TimGM6mb General MIDI soundfont, Tim Brechbill and David Bolton: GPL-2.0.
  Editable SoundFont source is `assets/koom/TimGM6mb.sf2`; attribution and
  provenance are in `assets/koom/TimGM6mb.COPYING`. GPL-2.0 text is included
  in `vendor/doomgeneric/LICENSE`.

Kalwer changes `vendor/doomgeneric/i_sound.c` to remove its unused SDL_mixer
include. The audio adapter mixes Doom DMX samples and synthesizes MUS/MIDI music.
Audio libraries are linked only into the separate GPL Koom helper.
