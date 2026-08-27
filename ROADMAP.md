# DoomCube Roadmap

This file is the living project plan for DoomCube.

Keep it updated when a feature lands, a priority changes, or a new branch is
started. The goal is to avoid relying on chat history to remember what DoomCube
was supposed to do next.

# Current baseline

The current master baseline includes:

- Native GameCube DOL and disc-image builds
- Native GameCube FST runtime
- GameCube controller support and rumble
- SDL2 sound and SDL2_mixer/TiMidity music
- Memory Card saves
- Multi-IWAD launcher
- PWAD manifest discovery and launcher selection
- PWAD merge support
- Native SIGIL Episode 5 support
- Native SIGIL II Episode 6 support
- Renderer-limit and overflow hardening
- Developer IWAD/PWAD warp harness
- Single-artifact Dolphin regression suite
- Player, development, and technical documentation split

The PWAD/SIGIL merge has been regression-tested with all current automated
cases passing against one unchanged GameCube image.

# v1.0 priorities

These are the things to concentrate on before calling DoomCube 1.0.

## 1. Real GameCube hardware validation

Highest priority.

Test the normal release image through the intended GameCube loading path.

Validate:

- [ ] Video output
- [ ] Audio
- [ ] MIDI/TiMidity playback
- [ ] Controller input
- [ ] Controller rumble
- [ ] Launcher navigation
- [ ] IWAD loading
- [ ] PWAD loading
- [ ] SIGIL Episode 5
- [ ] SIGIL II Episode 6
- [ ] Memory Card creation
- [ ] Memory Card save
- [ ] Memory Card load
- [ ] Memory Card delete/failure paths
- [ ] Repeated map transitions
- [ ] Long-running stability

Do not redesign systems merely because they have not yet been tested on
hardware. Test the current implementation first and fix actual failures.

## 2. Broaden regression coverage

The current suite proves the SIGIL progression/finale paths and the
single-artifact test architecture.

Add representative coverage for:

- [ ] DOOM Shareware
- [ ] Ultimate DOOM
- [ ] DOOM II
- [ ] TNT
- [ ] Plutonia
- [ ] Vanilla launch with no PWAD
- [ ] Ordinary map-only PWAD
- [ ] PWAD requiring merge semantics
- [ ] Launcher PWAD availability failures
- [ ] Large WAD/lump read path
- [ ] Memory Card happy path where practical

Keep the invariant:

```text
one compile -> one DOL -> one GameCube image -> every regression case
```

## 3. Save system v3

This is a **v1.0 release blocker**, not a post-1.0 feature.

Suggested branch:

```text
feature-save-system-v3
```

Goals:

- [ ] Reduce the current ~116-block save container
- [ ] Right-size/dynamically size Memory Card allocation
- [ ] Store IWAD/PWAD identity with saves
- [ ] Prevent accidental cross-PWAD save loading
- [ ] Add stronger save metadata
- [ ] Add GameCube icon/banner metadata
- [ ] Improve graceful handling of missing/full/corrupt cards
- [ ] Add disposable-card regression tests where possible
- [ ] Validate persistence across remount/restart on hardware

Possible first-run flow when no DoomCube save container exists:

```text
CREATE SAVE DATA?

DoomCube can create save data on
Memory Card A for game saves and settings.

> CREATE SAVE DATA
  CONTINUE WITHOUT SAVING
```

## 4. Release/fresh-clone sanity

Before 1.0:

- [ ] Build from a fresh clone
- [ ] Verify documented dependencies are sufficient
- [ ] Verify `make`
- [ ] Verify `make iso`
- [ ] Verify `make test`
- [ ] Verify `make regression`
- [ ] Verify `make test-rc`
- [ ] Verify `make rc`
- [ ] Verify `make release`
- [ ] Verify `make release-debug`
- [ ] Verify release ZIP contains no commercial IWADs
- [ ] Verify player `build.sh`
- [ ] Verify player `build.bat`
- [ ] Verify optional shareware acquisition and hash verification
- [ ] Verify generated player image boots

## 5. Memory and stress testing

Use actual measurements rather than increasing limits blindly.

- [ ] Record coarse memory headroom during normal play
- [ ] Stress SIGIL/SIGIL II further
- [ ] Test at least one very large/complex PWAD
- [ ] Watch renderer high-water telemetry
- [ ] Confirm drawseg/visplane/vissprite/opening limits remain sensible
- [ ] Exercise repeated WAD/music load/free paths
- [ ] Exercise repeated map transitions

Renderer high-water telemetry is intentional production/debugging
infrastructure and should remain available.

## 6. Small v1.0 polish

Only low-risk polish that does not destabilise the port:

- [ ] Replace tiny Episode 5/6 text fallback with better menu presentation
- [ ] Review user-facing fatal errors
- [ ] Review launcher wording
- [ ] Review README screenshots
- [ ] Final documentation pass

Avoid pulling major new features into the 1.0 stabilisation cycle.

# Post-1.0 branches

These are worthwhile, but should stay out of the 1.0 stabilisation cycle
unless they become necessary to fix a release blocker.

## True widescreen

Suggested branch:

```text
feature-widescreen
```

Goals:

- [ ] Implement true renderer widescreen rather than merely stretching output
- [ ] Add an in-game menu option
- [ ] Preserve normal 4:3 behaviour
- [ ] Audit HUD/status-bar behaviour
- [ ] Audit automap and finale/intermission presentation
- [ ] Regression-test both display modes

Core Doom renderer changes are acceptable where required; this is a GameCube
port, not a requirement to preserve upstream files untouched.

## SD runtime support

Possible branch:

```text
feature-sd-runtime
```

Investigate:

- [ ] SD Gecko
- [ ] SD2SP2
- [ ] Whether SD should supplement or replace any DVD/FST paths
- [ ] Bare-DOL workflows
- [ ] Keeping the canonical release/runtime path simple

Do not destabilise the native GameCube disc/FST path merely to add another
runtime source.

# Longer-term ideas

These are not current release blockers.

- [ ] Game Boy Advance cable automap/display experiments
- [ ] Local four-player split-screen
- [ ] Network multiplayer
- [ ] Further disc I/O/cache optimisation
- [ ] More PWAD compatibility work as real incompatibilities are found

# FrameCube

Long-term infrastructure project:

**FrameCube — boringly reliable GameCube infrastructure.**

The idea is to extract reusable GameCube platform services learned from
DoomCube so later ports do not have to rediscover the same console-specific
problems.

Potential FrameCube services:

- DVD/FST access
- stdio/path handling
- video
- audio
- controller input
- rumble
- Memory Cards
- build/image tooling
- diagnostics
- regression infrastructure

Keep FrameCube lower-level than a game engine.

Target shape:

```text
game / generic port layer
        |
thin game-specific backend
        |
FrameCube
        |
libogc2
        |
GameCube
```

The regression framework is a particularly strong candidate for extraction,
provided the harness does not consume resources that are themselves under
test.

Memory Card B should not be reserved for regression bookkeeping.

# Quake2Cube

A future Quake II / Quake2Generic port is a useful acceptance project for
FrameCube.

The point is not merely to run Quake II. It should prove that DoomCube's
GameCube-specific infrastructure can be reused cleanly by another substantial
engine.

# Project discipline

For future work:

1. One coherent feature/fix per branch where practical.
2. Keep commits reviewable rather than creating giant checkpoint commits.
3. Preserve a known-good branch before invasive history surgery.
4. Add regression coverage for bugs that are likely to return.
5. Prefer empirical limits and measurements over arbitrary capacity increases.
6. Update `README.md` for player-facing changes.
7. Update `DEVELOPMENT.md` for setup/workflow changes.
8. Update `TECHNICAL.md` for implementation/architecture changes.
9. Update this `ROADMAP.md` whenever priorities or completed work change.
10. Do not let ROADMAP.md become a changelog; completed milestones can be
    condensed once they are no longer useful planning context.