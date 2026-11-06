# Branding assets

Originals are stored unmodified in `originals/`. Derived files are generated from them;
never overwrite an original.

| Original | Source | Derived | Use |
|---|---|---|---|
| `originals/logo-silver-original.png` | user, 2026-11-06 | `logo-silver.png` (trimmed) | interface wordmark, light-on-dark use |
| `originals/icon-mark-original.png` | user, 2026-11-06 | `icon-mark-1024.png` (trimmed, square canvas) | icon variant A, flat mark |
| `originals/icon-tile-original.png` | user, 2026-11-06 | `icon-tile-1024.png`, `icon-header.png` | app icon and header mark |

All three originals are 8-bit RGBA PNG with a transparent background.

The app icon currently uses the rounded tile (`icon-tile-1024.png`). The flat mark is kept
as variant A; switching is a one-line change in `CMakeLists.txt` and `icon-header.png`.
