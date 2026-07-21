# Latin-1 / Portuguese font support (v1.3.0)

**Date:** 2026-07-21
**Version:** 1.2.2 -> 1.3.0 (SemVer: backwards-compatible feature)

## Goal

Full Portuguese rendering across the device: reader content (all 5 font
families) and UI (library titles, menus, status text).

## Problem

All bitmap fonts covered ASCII only (0x20-0x7E). Portuguese needs the Latin-1
Supplement (a-tilde, c-cedilla, acute/grave/circumflex vowels, guillemets).
Three independent layers had to change consistently; missing any one breaks
rendering silently:

1. **Glyphs** - fonts regenerated with charset 0x20-0xFF.
2. **Width caches** - `FontMgr::_charWidths` and `TextRenderer::_gfxCharWidths`
   widened from 128 to 256 entries; cache loops now run 32..255 (loop variable
   changed from `uint8_t` to `int` to avoid wrap-around).
3. **Encoding** - `Adafruit_GFX::write()` consumes one byte per glyph, so all
   UTF-8 text is collapsed to Latin-1 before reaching the display layer.

## Design decisions

### Fonts as `.h` (extern) + `.cpp` (data)

`const` arrays defined in headers have internal linkage in C++: every
translation unit including them gets its own copy in flash. `TextRenderer.h`
(4 serif families) is included by 2 TUs and `FontMgr.h` by 5. With ~430 KB of
font data this duplication is unacceptable, so each family is now an extern
declarations header plus a single `.cpp` definition file. This also removes
the duplication that already existed.

### FreeSans replaced by GNU FreeFont build

Adafruit's `<Fonts/FreeSans*pt7b.h>` are ASCII-only and cannot be extended, so
the project now carries its own `Fonts/FreeSans.h/.cpp` generated from Debian's
`fonts-freefont-ttf` (GPLv3 + font exception), Regular and Bold at 9/12/18/24pt.
`fontconvert` names extended-charset fonts with a `pt8b` suffix, so every
reference moved from `*pt7b` to `*pt8b` (FontMgr, TextRenderer, AppReader,
DisplayMgr, BatteryMgr).

### yAdvance patching (recurring trap - documented in every font header)

TTF face metrics differ between mirrors/builds, so generated `yAdvance` values
drift from the line heights the layout was tuned for. After generation the
values are patched to: Merriweather 22/30/44/59, Gelasio 22/30/45/60, Literata
26/35/52/70, SourceSerif4 24/32/48/64, FreeSans 22/29/42/56 (the Adafruit
originals; the Debian TTF generated 19/26/39/52 and was patched). The Debian
FreeSans mismatch was caught and corrected during generation.

### Conversion points (UTF-8 preserved where it matters)

- `FontMgr::utf8ToLatin1()` (static, char* + String overloads): full UTF-8
  decoder; codepoints > 0xFF map to ASCII fallbacks (curly quotes, dashes,
  ellipsis) or `?`; NBSP becomes a regular space (allows word-wrap); soft
  hyphens are dropped; invalid bytes pass through (already-Latin-1 input is
  idempotent). Host-tested (14 cases).
- **UI catch-all**: `FontMgr::drawText()`/`getTextWidth()` convert internally,
  so menus, WiFi SSIDs, status strings and anything routed through FontMgr
  render correctly with no call-site changes.
- **Library titles**: converted once at metadata load in `AppReader.cpp`
  (they are drawn/measured directly, bypassing FontMgr). `books_meta.json`
  itself stays UTF-8, so the WebUI is unaffected.
- **Reader content**: `EpubLoader::parseHtmlToRichContent()` now decodes HTML
  entities (named Portuguese set + numeric `&#NNN;`/`&#xHH;`, host-tested,
  11 cases) and then converts each text node to Latin-1, after the existing
  smart-punctuation replaces (which match raw UTF-8 sequences).

## Flash budget

App partitions are 0x280000 (2.56 MB); previous firmware was 1.31 MB. Doubling
glyph coverage adds roughly 200-250 KB of font data, partially offset by
removing the pre-existing per-TU duplication. Verify the linker output after
`pio run` stays well under the partition size.

## Regeneration procedure

1. `fonttools varLib.instancer` to pin variable TTFs to wght=400/700.
2. `fontconvert <ttf> <size> 32 255` for each family/weight/size.
3. Re-apply the yAdvance values listed above.
4. Keep the `.h`/`.cpp` split.

## Rebase onto v1.2.2

This work was first prepared against v1.0 and reapplied onto v1.2.2. Diffing
the two bases showed that of the eight source files touched here, only
`AppReader.cpp` had diverged upstream: v1.2.0 added manual library ordering
(`BookOrderLogic.h` / `applyBookOrderT`) inside `loadBooks()`. That block sits
after the title assignment and does not interact with the Latin-1 conversion,
so both changes coexist; the ordering code is preserved verbatim. No new
font consumers appeared in v1.1-v1.2.2, so the `pt7b` -> `pt8b` rename remains
confined to the same five files.
