# Kalwer for Android

A native, fullscreen, translucent launcher for Android 8.0 or newer. Opening
the app focuses its search field and shows the keyboard. The green palette,
monospaced type, rounded result cards and halftone field follow desktop Kalwer;
the Android UI uses native views and a cached repeating alpha tile. The background
and translucent panels alternate between more and less transparent areas in a
halftone pattern; text and app icons stay crisp. There is no background service or
continuous animation loop.

## Performance

App labels/package names are normalized once in a process-level catalog. A query
is prepared once, each candidate is scored once, and sorting compares cached keys.
The catalog is reused on reopening and invalidated by package/locale changes, with
a 60-second metadata refresh fallback. Only visible icons are decoded on a worker;
a 2 MiB cache holds their display-sized bitmaps. Result rows and selection backgrounds
are recycled. Keyboard opening has no artificial delay.

Halftone fills use cached 8 dp tiles and one shader-backed draw per fill, without
screen-sized bitmaps, offscreen compositing layers, or per-frame circle loops.
The opacity slider changes average coverage; transparent gaps expose the prior app.

On the development machine, `SearchBenchmark` (56 queries over 1,000 synthetic app
names, second JVM pass) dropped from **1570.99 ms to 10.61 ms** after caching keys
and scores. Both runs produced the same match-count checksum. This is a local
microbenchmark; handset launch latency and frame rate depend on the device.

## Install and launch

Install `app/build/outputs/apk/debug/app-debug.apk` on your phone. This is a
debug-signed preview; Android may ask you to allow installation from the browser
or file manager you use. It contains Java bytecode and supports ARM and x86 phones
without separate architecture builds.

Assign **Kalwer** to your phone's side-button **Open app** action. Opening its
normal app icon does the same thing. The hardware binding belongs to the phone's
settings or button mapper; availability and the exact menu depend on the device.
The app also accepts `android.intent.action.ASSIST` for launchers/mappers that
send that intent, but is not a voice-assistant service.

Kalwer uses a translucent activity, so the previous app can remain visible
behind it. It does not need permission to draw over other apps, does not intercept
power-button presses itself, and does not unlock or bypass a lock screen.
Back or the top-right × closes it. System bars can be revealed with an edge swipe.

## Controls

- Type to search installed, enabled launcher apps in the current Android profile.
  Exact and prefix matches rank before partial/fuzzy matches; pinned matches lead
  the app group. Apps in a separate work profile are not included in this preview.
- Tap a result or press the keyboard's Go/Enter button to open it. Hardware
  Up/Down selects results; Shift+Enter, a long press, or the star toggles a favourite.
- If there are no app or calculator results, **Search Google** becomes the result.
  Enter opens the complete query in the default browser. Query parameters are
  encoded, including spaces, `&`, `+` and Unicode. No search is sent while typing.
  An initial `?` forces this behavior even if an app matches. A lone `?` does nothing.
- Arithmetic is local: `2+3*4`, `(2+3)*4`, `2^3`, `sqrt(16)`, `√49`, `200*5%`.
  Tap the answer to copy it. This preview shows decimal results only.
- `>` opens mobile actions, provisionally replacing the desktop PTY:
  `> wifi`, `> bluetooth`, `> sound`, `> display`, `> battery`, `> settings`,
  `> camera`, `> alarm`, `> timer 5m`, `> timer 1h30m`, `> dial NUMBER`,
  `> maps coffee near me`, `> share TEXT`, and `> copy TEXT`.
  The dialer lets you review before calling; timers open the Clock UI. These
  actions require an installed app that handles the corresponding Android intent.
  Kalwer keeps the query and shows a message if an action cannot be opened.
- The gear or `/settings` controls background opacity (30–95%, initially 72%)
  and query retention (0–30 seconds, initially 3 seconds). Favourites persist.

The app requests only the normal `SET_ALARM` permission and declares visibility
of launcher activities. It needs no Internet, contacts, calling, accessibility,
overlay, or all-packages permission. Google/maps/sharing are handed to other apps.
Android references: [package visibility](https://developer.android.com/training/package-visibility/declaring),
[immersive windows](https://developer.android.com/develop/ui/views/layout/immersive),
and [clock intents](https://developer.android.com/reference/android/provider/AlarmClock).

## Build and verify

Requirements: JDK 17 or 21, Android SDK platform 35 and build tools 34.0.0.
Set `ANDROID_HOME` to the SDK directory (or set `sdk.dir` in an untracked
`local.properties` file). The checked-in wrapper uses Gradle 8.10.2 / AGP 8.7.3.

```sh
cd android
./gradlew :app:assembleDebug :app:lintDebug
./test-search.sh
# With an emulator/device connected:
./gradlew :app:connectedDebugAndroidTest
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n pl.aridlin.kalwer/.MainActivity
```

The host regression suite checks ranking, Google-query normalization, arithmetic
precedence/errors and timer validation. Instrumentation tests exercise installed
app discovery, favourites, explicit/implicit Google mode, actual Enter-to-browser
intent encoding, calculator/actions, and the translucent window theme.
They also check row recycling and the halftone's actual alpha values, including
that both the dots and their gaps remain translucent.

Release builds are deliberately unsigned; configure your own release signing
before distributing a production release. Keep the signing key outside Git.

Halftone dot opacity follows the background opacity setting instead of being
boosted to full opacity. Dots are capped at 95% opacity even for opaque panel
colors; gaps use 40% of the dot alpha. Text and icons retain their own opacity.
