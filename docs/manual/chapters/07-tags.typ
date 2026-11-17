#import "../style.typ": *

#chapter(7, "Tags",
  lead: [Tags are yours: names, colours and assignments live in the catalog. They are the fastest way to narrow a library without remembering file names.],
  inthis: (
    [Create, rename, recolour and delete tags],
    [Assign tags to rows, folders and whole subtrees],
    [Recent tags and Add last tag],
    [Filter by tag, combine with Starred, reset assignments],
    [Favourite folders],
  ),
)[

#section("Creating and managing tags")

The tag menu is reachable from the row context menu (*Tag ▸*), from the toolbar *Tags* filter and
from *Edit tags…* in the Analysis panel. *New tag* asks for a name and assigns a colour; *Manage
tags* renames, recolours and deletes existing tags.

Deleting a tag removes its assignments from every file. Stars, smart metadata, measured features and
the audio are untouched. Renaming keeps the assignments and updates the label everywhere, including
the recent-tag list.

Tags are per catalog. Two catalogs can use the same tag name with different colours and different
assignments; nothing is shared.

#section("Assigning")

Select one or more rows and use *Tag ▸*; the menu starts with the five most recently *assigned* tags
in this catalog, newest first. Choosing one assigns it to the whole selection and never removes
another tag.

The recent list is built from your explicit assignments only: the quick items, the *Tag* submenu,
*New tag* and *Tag folder*. AutoTag application deliberately does not touch it, so a bulk run cannot
scramble the order. Opening a menu or removing a tag does not change it either.

*Add last tag* (no default key) repeats the last tag you assigned to a selection. It is disabled when
the selection is empty or no tag has been assigned yet.

To remove one tag from one file, Alt-click its tag in the TAGS cell. To remove all assignments in a
scope, use *Reset tags…*.

#section("Tag folder")

Right-click a folder and choose *Tag folder…*. The confirmation names the folder, whether subfolders
are included, how many catalogued files are below it and how many are already tagged. Applying is
idempotent: a repeated run adds nothing twice. The work runs in background batches and reports the
actual number of new assignments.

#section("Filtering")

The *Tags* chip in the toolbar opens the tag filter. Include several tags to require all of them. The count line updates to the matching rows, and header sorting
still sorts the whole filtered result.

#plate("/docs/manual/assets/tags.png", [Figure 7.1 — A tag filter for *Kick* with two rows selected. The Analysis panel reports the one tag shared by the selection.])

*Starred* is a separate filter and combines with the tag filter. Stars are a quick personal mark, not
a tag: they do not appear in the tag menu and cannot be renamed.

*Clear filters* turns off the tag filter, the star filter and *Hide empty*, and saves that state. It
does not clear the search field.

#section("Reset tags")

*Reset tags…* is available for a selection and for a folder subtree. It removes every tag assignment
in the confirmed scope but keeps the tag definitions. The confirmation names the scope, the number of
catalogued files and how many currently have tags. Stars, smart overrides, measured features, source
files and recent-tag history are untouched.

The work runs in the same background batches as *Tag folder* and can be cancelled; completed batches
stay. A repeated reset is a no-op.

#section("Favourite folders")

The folder menu toggles *Add to favorites* / *Remove from favorites*. A folder already covered by a
favourite shows a disabled *Already in favorites*. Adding a parent absorbs redundant child
favourites.

Favorites appear in a second tree below the folder tree. Clicking a node filters the library and
clears the normal tree's selection. A path that is no longer available stays listed but muted. The
list is global: it survives catalog switches, and a favourite with no rows in the current catalog
remains visible with nothing to show.

]
