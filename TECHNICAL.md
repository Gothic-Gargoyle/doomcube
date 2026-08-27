# DoomCube Technical Notes

This document describes DoomCube's implementation details and engineering
decisions.

For environment setup and day-to-day build commands, see
[DEVELOPMENT.md](DEVELOPMENT.md).

# Runtime architecture

DoomCube's canonical runtime is a native GameCube disc image.

The runtime path is roughly:

```text
Doom / DoomGeneric
        |
GameCube-specific backends
        |
libogc2 / SDL2 / SDL2_mixer
        |
Nintendo GameCube
```

The canonical disc paths are:

```text
dvd:/data/wad/
dvd:/data/pwad/
dvd:/data/deh/
dvd:/data/timidity/
dvd:/launcher/
```

Bare-DOL SD-card execution is not currently the canonical runtime model.

# Native GameCube image and FST

DoomCube builds a native GameCube image rather than an ISO9660 filesystem.

The image contains:

```text
GameCube boot header
DoomCube apploader
main DOL
native GameCube FST
runtime file data
```

The low-level image builder is:

```text
tools/native-gcm/mkdoomcube.py
```

The native runtime filesystem backend is implemented in:

```text
source/gc_dvd_fst.c
source/gc_dvd_fst.h
```

The backend resolves files through the GameCube FST and exposes the paths used
by the rest of DoomCube.

The native DVD/FST path does not provide ordinary directory enumeration.
Because of this, PWAD discovery is performed at image-build time rather than
at runtime.

# PWAD manifest discovery

During image construction, `mkdoomcube.py` scans:

```text
data/pwad/
```

and writes:

```text
data/pwad/doomcube.lst
```

The launcher reads that manifest and checks the corresponding files through
the normal disc backend.

This keeps runtime discovery deterministic without requiring a synthetic
directory-enumeration layer in the GameCube filesystem backend.

# WAD merge path

Optional PWADs are launched with merge semantics rather than ordinary
`-file` semantics.

The merge implementation lives in:

```text
source/w_merge.c
```

and is enabled through:

```text
FEATURE_WAD_MERGE
```

The GameCube launcher passes the selected PWAD using `-merge`.

This matters for PWADs that replace sprites or flats. Loading such PWADs as
independent WAD namespaces can leave the renderer with an invalid flat
namespace.

One failure observed during SIGIL II development reduced `numflats` to only
three entries while sectors still referenced normal flat indices. The bad
indices then wrote outside the flat-presence table during precaching and
corrupted unrelated engine memory.

Merging the namespaces before normal WAD initialisation fixes the root cause.

`R_PrecacheLevel()` also validates floor and ceiling flat indices before using
them as array indices, turning this class of failure into a controlled fatal
error instead of memory corruption.

# GameCube WAD reads

The GameCube WAD backend is:

```text
source/w_file_gamecube.c
```

Large direct reads are performed in bounded chunks instead of relying on one
large `fread()`.

This was added after a large SIGIL II lump returned a short read when requested
as one operation.

The chunked path continues until the requested range has been consumed or the
underlying read makes no further progress.

The physical DVD fast path must also respect GameCube alignment constraints.
In particular, fast direct access is only safe when the effective DVD offset,
destination, and transfer size satisfy the required alignment conditions.

# SIGIL Episode 5 and Episode 6

DoomCube extends the original episode handling for:

```text
Episode 5 -> SIGIL
Episode 6 -> SIGIL II
```

Support spans several original engine systems rather than being isolated to
the launcher.

It includes:

- dynamic episode-menu population
- E5/E6 music lookup
- secret-level entry routing
- secret-level return routing
- intermission fallback handling
- finale art handling

Secret progression currently includes:

```text
SIGIL:
E5M6 -> E5M9
E5M9 -> E5M7

SIGIL II:
E6M3 -> E6M9
E6M9 -> E6M4
```

For E5/E6 intermissions, missing `WIMAP` artwork falls back to `INTERPIC`.

E5M8 and E6M8 enter the art-screen finale path directly and draw `SIGILEND`.

The Episode 5/6 menu entries currently use a text fallback rather than
dedicated menu graphics.

# Renderer limits and overflow hardening

Some modern PWADs exceed the original static renderer limits.

DoomCube currently uses:

```text
MAXDRAWSEGS     2048
MAXVISPLANES    1024
MAXVISSPRITES   1024
MAXOPENINGS     SCREENWIDTH * 64
```

The opening capacity remains the original `SCREENWIDTH * 64`, but allocation
now goes through:

```text
R_AllocOpening()
```

This checks capacity before advancing the opening pointer or performing the
associated copy.

The original renderer could advance `lastopening` and only notice the overflow
later under range checking, after memory had already been overwritten.

DoomCube also reports renderer high-water marks on GameCube debug builds.

Current instrumentation covers:

- visplanes
- openings
- drawsegs
- visible sprites

Warnings and fatal limit-exhaustion reports remain available where
appropriate.

This telemetry is intentionally retained because it is useful when testing
large or pathological PWADs.

# Regression architecture

DoomCube's automated regression framework follows one invariant:

```text
one compile -> one DOL -> one GameCube image -> every test case
```

The host runner is:

```text
tools/regression.sh
```

The guest-side controller is:

```text
source/gc_regression.c
source/gc_regression.h
```

The suite performs one clean build, creates one image, hashes it, and then runs
each case in a fresh Dolphin process against that exact same artifact.

The image hash is checked again at the end of the suite.

# RTC case transport

The regression image must select different test cases without rebuilding or
modifying the image.

DoomCube uses Dolphin's custom emulated RTC as a tiny host-to-guest transport.

The host launches Dolphin with:

```text
EnableCustomRTC=True
CustomRTCValue=<case timestamp>
```

The guest reads the value through:

```c
time(NULL)
```

The current transport constants are:

```text
GC_REGRESSION_RTC_BASE       = 1704067200
GC_REGRESSION_RTC_SLOT       = 300 seconds
GC_REGRESSION_RTC_TOLERANCE  = 60 seconds
```

Case `n` is selected from:

```text
RTC_BASE + n * RTC_SLOT
```

The slot is deliberately much larger than the accepted tolerance because
emulated time continues to advance while the GameCube image boots.

This keeps the test transport independent of the systems under test:

- no special file needs to be written into the image
- no Memory Card slot is reserved for regression bookkeeping
- no per-case DOL is built
- no per-case ISO is built

That is important because the test harness should not consume or modify
resources that DoomCube itself is trying to test.

# Regression cases

The current runtime cases are:

```text
0  sigil-e5m6-secret
1  sigil-e5m9-return
2  sigil2-e6m3-secret
3  sigil2-e6m9-return
4  sigil-e5m8-finale
5  sigil2-e6m8-finale
```

The guest-side progression hooks wait until the initial map has been running
for roughly two seconds before invoking the configured normal or secret exit.

Progression markers look like:

```text
DoomCube: TEST PROGRESSION: E5M6 -> E5M9 (secret exit)
```

Finale tests do not pass merely because `F_StartFinale()` returns. The marker
is emitted only after the normal game loop has repeatedly ticked and drawn the
finale art-screen state.

# Launcher regression selection

Regression cases still resolve their requested IWAD and PWAD against the
launcher's normal scan results.

This means the automated suite exercises the same availability checks and
metadata used by an interactive launch instead of bypassing the launcher with
raw unchecked paths.

The separate developer test harness can also compile in IWAD/PWAD
autoselection and a warp target, but that mechanism is intended for arbitrary
manual development testing rather than the fixed single-artifact suite.

# Logging

DoomCube centralises GameCube logging through `gc_debug.h`.

The intended levels are:

```text
DC_ERROR  always available
DC_WARN   always available
DC_INFO   always available
DC_DEBUG  debug builds
DC_TRACE  trace builds
```

Renderer high-water telemetry uses debug logging, while exhaustion and other
important runtime failures use warning/error paths that remain visible in
normal builds.

# Memory Card notes

DoomCube currently has GameCube Memory Card save support, but original hardware
validation is still pending.

The current save container uses approximately 116 blocks.

Future save-system work is expected to address:

- smaller/right-sized allocation
- stronger IWAD/PWAD identity protection
- GameCube icon/banner metadata
- broader failure-path testing
- real-hardware validation

Regression infrastructure should not reserve Memory Card B or otherwise
consume a card slot for harness bookkeeping, because Memory Card behaviour is
itself something the project needs to test.

# Real-hardware validation

Important GameCube acceptance areas include:

- video output
- audio
- MIDI playback
- controller input
- rumble
- Memory Card creation/read/write/delete/remount behaviour
- disc/FST access
- long-running stability
