# Reusable custom genres

Approved in conversation on 2026-11-15: show built-in and manually entered genres in the same
assignment list for rows and folders. Remember them in the current catalog across restarts and
after clearing/removing their last assignment; backfill existing manual genres. Deduplicate
case-insensitively and trim surrounding whitespace. Do not change automatic inference.

Approaches: deriving the list from current overrides is smaller but forgets genres after their
last assignment is cleared. A persistent catalog list is chosen for reliable reuse. Global
settings storage is not chosen because catalogs should remain portable and independent.

Use an additive schema-19 `custom_genres` table with a folded name key, first-entered display
spelling and creation timestamp. Register successful manual assignments in the same transaction;
do not register cancelled/failed/empty folder assignments or Unknown/not-applicable choices.
Menus consume a frozen combined list (built-ins first, custom entries alphabetically after them).
Use non-overlapping dynamic IDs and capture the list when opening a menu, not when acting on it.

No separate genre manager, deletion/rename UI or inference dictionary changes in this task.
