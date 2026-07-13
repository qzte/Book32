# Reader font families

These headers are pre-rendered Adafruit-GFX bitmap fonts (`GFXfont`), generated
with the `fontconvert` tool from Adafruit-GFX-Library, at 9/12/18pt (Regular)
and 9/12/18/24pt (Bold), matching the sizes already used for `FreeSans` in
`TextRenderer`.

They were chosen as freely-licensed substitutes for two proprietary,
non-redistributable typefaces:

| Header             | Font (as embedded)      | Substitute for   | License                  |
|---------------------|--------------------------|------------------|---------------------------|
| `Merriweather.h`    | Merriweather              | Bookerly (Amazon, proprietary) | SIL Open Font License 1.1 |
| `Literata.h`        | Literata                  | (requested directly) | SIL Open Font License 1.1 |
| `SourceSerif4.h`    | Source Serif 4            | Source Serif Pro (renamed by Adobe) | SIL Open Font License 1.1 |
| `Gelasio.h`         | Gelasio                   | Georgia (Microsoft, proprietary) | SIL Open Font License 1.1 |

Source TTFs: [google/fonts](https://github.com/google/fonts) (`ofl/` directory).
Variable font instances were pinned to static Regular (wght=400) and Bold
(wght=700) weights with `fonttools varLib.instancer` before conversion.

Full OFL 1.1 license text: https://openfontlicense.org
