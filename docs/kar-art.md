# Kar pixel rendering and artwork

Kar rasterizes a 240×320 scene in native C++, including sprites, bitmap text,
road geometry and scene primitives. The desktop GPU presents that frame using
nearest-neighbor sampling. No Java runtime or game executable runs during play.
The popup's animation, focus pause, wallet and daily trial remain host features.

An external art pack can replace the built-in fallback graphics. Run
`/kar-import "path/to/art.karp"` to validate and install a pack and its adjacent
`art.kars` scene on either desktop platform. Reopen Kar after importing.

For manual installation, put `art.karp`
and its optional `art.kars` scene alongside each other in the `kar` directory
next to `koins-v1` in Kalwer's state directory. Reopen Kar after changing a pack.
The pack loads on a worker so file reads do not delay the popup animation.
Invalid or missing packs leave the fallback renderer available.

Artwork has its own licensing; Kalwer's MIT license does not grant rights to
third-party game art. The repository and desktop release do not include the
commercial reference game's artwork or executable.

## Building a pack

`tools/pack_kar_art.py` accepts a JSON list of sprite entries and RGBA PNG files
(Python and Pillow are needed only for packing):

```json
[{"bank":1,"frame":4,"file":"car-straight.png","x":-35,"y":-63}]
```

```sh
python tools/pack_kar_art.py sprites.json art.karp --scene scene.json
```

Offsets specify the sprite's upper-left corner relative to its anchor. Runtime
bank assignments include player body `1`, wheels `2`, HUD `1055`/`1056`, fonts
`2100`–`2102`, sky `2080`, horizon `2084`, and scenery `3000` plus sprite-bank ID.
Font frames use `1000 + ASCII code`; larger rank digits use font bank `2102`.
Banks `2000` plus ID contain vehicles and effects. Palette variants use bank
`10000 + 128 * palette + scenery ID`.

Scene entries contain `kind`, RGB `color`, optional `bank` and `frame`, and nine
integer `vertices` values: three triples of lateral position, course distance
and elevation. Kind `0` is a colored triangle, `1` a filled rectangle, `2` a
sprite rectangle, and `3` a textured triangle. Rectangles use two triples and
three trailing zeroes. Course segments are 1024 units long. Scene records keep
their draw order within each segment.

Both binary formats use explicit little-endian fields, bounded dimensions and
record counts. The loader rejects truncated, oversized and duplicate sprites,
and trailing bytes. Neither format contains executable code.
