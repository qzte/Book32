# Reader font size in px, 7-value set (v1.28.0)

**Date:** 2026-09-25 (px options), revised 2026-09-27 (7-size set)
**Version:** 1.26.0 -> 1.27.0 -> 1.28.0 (SemVer: backwards-compatible feature, each time)

## Goal

Let the reader pick an exact reading size in pixels instead of three named
buckets (Pequeno/Medio/Grande = 9/12/18pt), on the web UI and the on-device
menu alike.

## Problem

The reader's body font is not a scalable web font: it's a set of pre-rendered
Adafruit-GFX bitmap fonts (`GFXfont`), one glyph set per exact point size, per
family, per weight. Adding a size means generating a whole new glyph set with
`fontconvert` from the original TTF sources — there is no "just change a CSS
number" path here.

## History

- **v1.27.0**: replaced the 3-bucket scheme (9/12/18pt) with 6 explicit sizes
  (10/12/14/16/18/20px). All 6 reader font families (FreeSans, Open Sans,
  Merriweather, Literata, Source Serif 4, Gelasio) were regenerated at those
  sizes (Regular) plus a shared 24pt Bold top for headers.
- **v1.28.0** (this revision): replaced that 6-value set with 9/10/11/12/13/14/16px
  — closer, finer-grained steps at the low end (where most reading actually
  happens) instead of the coarser 10/12/14/16/18/20 spread. `FreeSans` gained
  9/10/11/12/13/14/16px sizes going forward (it keeps 18pt/24pt too, since
  those two are also the on-device system UI's fonts via `FontMgr` — see
  below); the other five families were fully regenerated at the new set,
  dropping the now-unused 18/20pt sizes to reclaim flash.

## Design decisions

### Size ladder isn't uniform-stride

9/10/11/12/13/14 step by 1px, then 14->16 steps by 2px. `SettingsStore::
clampFontSize()` and `TextRenderer`'s local `normalizeFontSize()` both snap
to the nearest value with explicit per-boundary thresholds rather than a
formula, because the last gap's midpoint (15) doesn't fall out of a uniform
formula the way the 1px steps do.

### Header ladder generalizes to 7 body sizes

Each family's `B32_FONT_SET` macro (in `TextRenderer.cpp::getGFXFont()`)
picks four header weights (`h1`-`h4`) per body size, all Bold:

- `h4` = Bold at the body size itself (bold body text as the smallest header
  level).
- `h3` = Bold at the next size up the ladder.
- `h2` = Bold at two sizes up the ladder, capped at the ladder's top.
- `h1` = always Bold24 (the top of the ladder), regardless of body size.

Ladder (with the shared 24pt top): `[9, 10, 11, 12, 13, 14, 16, 24]`. This
keeps the H1>H2>H3>H4 hierarchy visually distinct at every body size without
headers ballooning as the body size grows (a body of 16px never needs a
header bigger than 24pt to read as "bigger").

### FreeSans: mixed provenance, patched yAdvance

`FreeSans.h`/`.cpp` is shared with the on-device system UI (`FontMgr`, menus
etc.), which still wants 18pt regular/bold. So instead of a full regenerate
per revision, FreeSans only gets new sizes *added* (this revision: 11pt,
13pt; the previous revision: 10pt, 14pt, 16pt — its now-superseded 20pt was
removed since nothing references it) while its established sizes (9, 12, 18,
24) stay byte-for-byte untouched, so the system UI's look never shifts.

The new sizes come from a different FreeSans mirror
([opensourcedesign/fonts](https://github.com/opensourcedesign/fonts)) than
the original Debian `fonts-freefont-ttf` build (not reachable from every
build environment), and that mirror's own `yAdvance` output diverges from
the project's established per-size line heights outside of 12pt (which
matches by coincidence — verified, not assumed). So every FreeSans size
added this way gets its `yAdvance` **linearly interpolated** between the
nearest established anchors instead of trusting the mirror's raw output:

- 10px: between 9(22) and 12(29) anchors -> 24
- 11px: between 10(24, itself interpolated) and 12(29) -> 27
- 13px: between 12(29) and 14(33, itself interpolated) -> 31
- 14px: between 12(29) and 18(42) anchors -> 33
- 16px: between 12(29) and 18(42) anchors -> 38

### The other five families: full regenerate each time, no patch needed

Open Sans, Merriweather, Literata, Source Serif 4 and Gelasio aren't shared
with any other consumer, so each size-set revision fully regenerates all of
them from the same Google Fonts variable-font sources
([google/fonts](https://github.com/google/fonts), `ofl/` directory, pinned
to the "Regular"/"Bold" named instances with `fonttools varLib.instancer`).
Their generated `yAdvance` has, at every size that has ever been requested so
far, come out identical to this project's established per-size line heights
— verified by direct comparison against the values already checked in before
each regenerate, not assumed. So none of the five has ever needed a manual
`yAdvance` patch. See `lib/Book32_Core/Fonts/README.md` for the exact source
files and pinned axis values, and for the general regeneration procedure.

## Flash budget

Each size-set revision regenerates or extends the reader font data; verify
the linker output after `pio run` stays comfortably under the app partition
size (2.56 MB per `docs/plans/2026-07-21-latin1-portuguese-fonts-design.md`).
Dropping unused sizes (this revision: FreeSans 20pt; the five other families'
18/20pt) offsets most of what adding new sizes costs.

## Regeneration procedure

Same as `docs/plans/2026-07-21-latin1-portuguese-fonts-design.md`'s, run once
per family per weight per new size:

1. `fonttools varLib.instancer` to pin the variable TTF to the family's
   "Regular"/"Bold" named instance (or, for FreeSans, use the mirror's static
   Regular/Bold TTFs directly).
2. `fontconvert <ttf> <size> 32 255`.
3. For the five Google Fonts families: use the generated `yAdvance` as-is
   (verify it against the already-established sizes first). For FreeSans:
   linearly interpolate `yAdvance` between the nearest established 9/12/18/24
   anchors instead.
4. Keep the `.h`/`.cpp` split; keep `FreeSans` additive (never touch its
   9/12/18/24 data, since `FontMgr` depends on it staying exactly as-is).
