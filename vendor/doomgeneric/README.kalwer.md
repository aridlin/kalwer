Doomgeneric from https://github.com/ozkl/doomgeneric at dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284. GPL-2.0-or-later; see LICENSE and source headers. Built as a separate framebuffer runtime using ../../koom/host.c.

Kalwer removes the unused SDL_mixer include from i_sound.c. The native audio
adapter in ../../koom/audio.c supplies Doom sound and music modules using
miniaudio and TinySoundFont, with the bundled TimGM6mb soundfont. See the root
THIRD_PARTY_NOTICES.md for source versions and licenses.
