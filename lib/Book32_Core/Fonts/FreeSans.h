// Auto-generated with Adafruit-GFX fontconvert from GNU FreeFont FreeSans (GPLv3 with font exception). Source: Debian fonts-freefont-ttf.
// Charset: 0x20-0xFF (ASCII + Latin-1 Supplement) for Portuguese and other
// Western European languages. Regenerate with: fontconvert <ttf> <size> 32 255
//
// NOTE: yAdvance values are intentionally patched to the project's established
// line heights (TTF face metrics differ between mirrors/builds). If you
// regenerate these fonts, re-apply the same yAdvance values or pagination and
// UI layout will shift. See docs/plans/2026-07-21-latin1-portuguese-fonts-design.md
//
// Data lives in the matching .cpp so that including this header from multiple
// translation units does not duplicate the font bitmaps in flash.
#ifndef FONT_FREESANS_L1_H
#define FONT_FREESANS_L1_H

#include <Adafruit_GFX.h>

extern const GFXfont FreeSans9pt8b;
extern const GFXfont FreeSans10pt8b;
extern const GFXfont FreeSans12pt8b;
extern const GFXfont FreeSans14pt8b;
extern const GFXfont FreeSans16pt8b;
extern const GFXfont FreeSans18pt8b;
extern const GFXfont FreeSans20pt8b;
extern const GFXfont FreeSans24pt8b;
extern const GFXfont FreeSansBold9pt8b;
extern const GFXfont FreeSansBold10pt8b;
extern const GFXfont FreeSansBold12pt8b;
extern const GFXfont FreeSansBold14pt8b;
extern const GFXfont FreeSansBold16pt8b;
extern const GFXfont FreeSansBold18pt8b;
extern const GFXfont FreeSansBold20pt8b;
extern const GFXfont FreeSansBold24pt8b;

// 10/14/16/20pt sizes were added later (reader font-size options) and
// generated from a different FreeSans mirror (opensourcedesign/fonts) than
// the original Debian fonts-freefont-ttf build, so their yAdvance is
// linearly interpolated between the established 9/12/18/24 anchors above
// instead of trusting the mirror's own (diverging) metrics. See
// docs/plans/2026-09-25-reader-font-size-px-options-design.md

#endif // FONT_FREESANS_L1_H
