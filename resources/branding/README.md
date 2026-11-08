# Branding assets

Originals are stored unmodified in `originals/`. Derived files are generated from them;
never overwrite an original.

| Original | Source | Derived | Use |
|---|---|---|---|
| `originals/logo-silver-original.png` | user, 2026-11-06 | `logo-silver.png` (alpha >=16 visible bounds + 8px margin) | enlarged interface wordmark, light-on-dark use |
| `originals/icon-mark-original.png` | user, 2026-11-06 | `icon-mark-1024.png` (trimmed, square canvas) | icon variant A, flat mark |
| `originals/icon-tile-original.png` | user, 2026-11-06 | `icon-tile-1024.png`, `icon-header.png` | previous app icon, retained; header mark retained but no longer displayed |
| `originals/icon-glossy-chevron-original.png` | user, 2026-11-08, `Глянцевый значок с голубым шевроном.png` | `icon-glossy-chevron-1024.png` | selected application icon |

All originals are 8-bit RGBA PNG with a transparent background.

The app icon uses the user-selected glossy silver tile with a blue chevron
(`icon-glossy-chevron-1024.png`). Its original is preserved byte-for-byte at 1254×1254;
the 1024×1024 derivative uses Pillow LANCZOS resizing of the complete canvas, without
cropping, background replacement or redrawing. JUCE generates platform icons from this
PNG through `ICON_BIG` in `CMakeLists.txt`. The interface wordmark is unchanged; earlier
icon variants remain available and are not deleted.
