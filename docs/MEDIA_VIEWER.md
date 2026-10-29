# Game media viewer

The Game Media viewer is part of Open MainUI 1.1.0. It resolves assets beside the selected ROM, so filenames are based on the ROM filename without its extension.

## Controls

- **Left** on a selected game: open Game Info.
- **Right**: open the quick carousel for Screenshot and Gameplay. Left or Escape returns to the game list.
- **Select**: open the full Game Media menu.
- The first item in the full menu is **Game Info**. It shows the XML game name (or ROM name), description, genre, rating, developer, publisher, release date and player count from `gamelist.xml`, without artwork. Compact `YYYYMMDD...` dates are displayed as `DD.MM.YYYY`. Missing metadata fields are omitted or shown as `-`.
- In the full menu, Box Art groups front and back covers, and Game Maps groups every map image. Use Up/Down to select an item and A/Return to open it.
- In either image gallery, Left/Right moves between images and B/Escape returns to the full menu. D-pad Down does not change the image; Left never closes the gallery.
- Select-menu images use the full area below the title bar without the footer. Game Maps show only a small `current/total` indicator at the lower left.
- The left-D-pad Game Info view keeps artwork and shows developer, publisher, release date, players and genre in the lower footer area.
- In a TXT walkthrough, Up/Down scrolls one line. L1/R1 and L2/R2 move one page; B/Escape returns to the full menu.
- The quick carousel is headed **Screenshot** or **Gameplay**. The full menu shows an image's map title when one is available.

## Supported file layout

For a ROM at `Roms/[SYSTEM]/[Game Name].ext`:

| Asset | Relative path |
| --- | --- |
| Front box art | `media/Box2D/[Game Name].png` |
| Back box art | `media/Box2DBack/[Game Name].png` |
| Screenshot | `media/Screenshot/[Game Name].png` |
| Gameplay video | `media/Videos/[Game Name].mp4` (also `.mkv`, `.avi`, `.mov`, `.webm`, `.m4v`) |
| PDF manual | `media/manuals/[Game Name].pdf` |
| Walkthrough | `[Game Name].txt` |
| Game maps | `media/Maps/[Game Name]_map001*.png` or `media/maps/...` |
| Map titles | Optional `gamelist.xml` `<map path="..." title="..."/>` entries |

Map files are sorted naturally by filename number (map2 before map10). When a matching `<map>` entry has a `title` attribute, that title is used; otherwise the viewer displays a numbered fallback label.

Map directory and filename matching accept either `Maps` or `maps` and are case-insensitive. PNG, JPG, JPEG and BMP map images are supported.

The media scanner uses the ROM's directory and does not trust file paths from XML. XML is only read for optional map titles.

## Preparing the SD card

There is no scraper bundled with MainUI. Use a scraper that can export the files below, or copy them by hand. Keep each scraped filename identical to the ROM filename without its extension; for example, `Super Mario World.sfc` uses the basename `Super Mario World`.

For a ROM at `Roms/SNES/Super Mario World.sfc`, the expected layout is:

```text
Roms/SNES/
	Super Mario World.sfc
	Super Mario World.txt
	miyoogamelist.xml
	gamelist.xml
	media/
		Box2D/Super Mario World.png
		Box2DBack/Super Mario World.png
		Screenshot/Super Mario World.png
		Videos/Super Mario World.mp4
		manuals/Super Mario World.pdf
		maps/Super Mario World_map001.png
```

Only files you have scraped need to be present. MainUI reads game descriptions and other game information from Onion's `miyoogamelist.xml` in the system folder. Optional map titles come from matching `<map path="..." title="..."/>` entries in that folder's `gamelist.xml`; the path should name the map image. Media files are located beside the ROM system folder, not in per-game subdirectories.

## Device dependencies

- **Videos:** copy the complete FFplay package to `Emu/ffplay/` on the SD card. MainUI executes `Emu/ffplay/bin/ffplay` and requires the accompanying `Emu/ffplay/libs/` directory and its shared libraries. Supported extensions are `.mp4`, `.mkv`, `.avi`, `.mov`, `.webm` and `.m4v`.
- **PDF manuals:** install the Green reader provided for Onion. MainUI requires the executable at `.tmp_update/bin/green/green` and its libraries in `.tmp_update/lib/parasyte/`. The `Green` launcher/config files alone are not the reader binary or its libraries.

Video and PDF viewing use Onion's installed FFplay and Green programs. Neither player nor its libraries are bundled with Open MainUI; the required SD-card paths are listed above.

## Current limitations

- PNG images and TXT walkthroughs are displayed in MainUI.
- Gameplay videos in the listed formats start at zero through `Emu/ffplay/bin/ffplay` on the handheld. MainUI temporarily saves and hides FFplay's `pos.cfg` so an old resume point is not reused, then restores it after playback. At end of file FFplay remains on the last frame; B returns to the full menu. Player output and exit timing are recorded in `.tmp_update/logs/MainUI-video.log`. This FFplay build has fixed 10-second Left/Right seeks.
- PDF manuals open with the installed Green reader at `.tmp_update/bin/green/green`, using libraries from `.tmp_update/lib/parasyte`. B closes the reader and returns to the full menu. Reader output is logged in `.tmp_update/logs/MainUI-pdf.log`.
- The quick carousel can show a screenshot and/or a video when those files exist. It does not show unavailable items.

## Build and device test

Build using the normal Open MainUI instructions in [BUILDING.md](BUILDING.md). Before replacing the installed MainUI, back up the SD card and keep the stock binary available for recovery.

Test at least:
1. A game with metadata, confirming Game Info shows publisher, developer, release date and description without artwork.
2. A game with no media files and a game with a screenshot only.
3. A game with a screenshot and video, confirming it starts at zero, B returns to the Select menu, and the end frame stays visible.
4. A game with front/back covers, multiple maps, and map titles in `gamelist.xml`.
5. A game with a TXT walkthrough, including page scrolling and returning to the game list.
6. A PDF manual, confirming Green opens the selected file and B returns to the Select menu.
