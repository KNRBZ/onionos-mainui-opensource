# Game media viewer

This branch adds a media viewer to the game list. It resolves assets beside the selected ROM, so filenames are based on the ROM filename without its extension.

## Controls

- **Left** on a selected game: open Game Info.
- **Right**: open the quick carousel for Screenshot and Video. Left or Escape returns to the game list.
- **Select**: open the full Game Media menu.
- In the full menu, use Up/Down to select an item and A/Return to open it.
- In an image viewer, Right/A advances to the next available item; Left/Escape returns to the media menu.
- In a TXT walkthrough, Up/Down scrolls one line at a time.

## Supported file layout

For a ROM at `Roms/[SYSTEM]/[Game Name].ext`:

| Asset | Relative path |
| --- | --- |
| Front box art | `media/Box2D/[Game Name].png` |
| Back box art | `media/Box2DBack/[Game Name].png` |
| Screenshot | `media/Screenshot/[Game Name].png` |
| Video | `media/Videos/[Game Name].mp4` |
| PDF manual | `media/manuals/[Game Name].pdf` |
| Walkthrough | `[Game Name].txt` |
| Game maps | `media/maps/[Game Name]_map001*.png`, etc. |
| Map titles | Optional `gamelist.xml` `<map path="..." title="..."/>` entries |

Map files are sorted naturally by filename number (map2 before map10). When a matching `<map>` entry has a `title` attribute, that title is used; otherwise the viewer displays a numbered fallback label.

The media scanner uses the ROM's directory and does not trust file paths from XML. XML is only read for optional map titles.

## Current limitations

- PNG images and TXT walkthroughs are displayed in MainUI.
- MP4 videos and PDF manuals are detected and listed, but playback/PDF rendering is not implemented yet; the viewer displays a status message and the path.
- The quick carousel can show a screenshot and/or a video when those files exist. It does not show unavailable items.

## Build and device test

Build using the normal Open MainUI instructions in [BUILDING.md](BUILDING.md). Before replacing the installed MainUI, back up the SD card and keep the stock binary available for recovery.

Test at least:
1. A game with no media files.
2. A game with a screenshot only.
3. A game with a screenshot and video.
4. A game with front/back covers, multiple maps, and map titles in `gamelist.xml`.
5. A game with a TXT walkthrough, including scrolling and returning to the game list.
6. A PDF and MP4 entry, confirming they are reported as detected rather than mistakenly presented as playable.
