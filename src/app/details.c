/* SPDX-License-Identifier: GPL-3.0-only */
#include "app/details.h"
#include "app/browser.h"
#include "ui/drawing.h"
#include "ui/panels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mainui_details_close(MainUIDetails *details)
{
    *details = (MainUIDetails){0};
}

void mainui_details_progress(MainUIDetails *details, Uint32 wait_ms)
{
    if (!details->open) {
        return;
    }
    if (details->preview) {
        mainui_preview_request_within(details->preview, details->catalog, details->library,
                                      details->index, wait_ms);
    }
    /* Snapshots wait for the artwork and must not animate. In the live UI,
     * advance one wrapped description line at a gentle pace. The last page
     * stays in place so the player can finish reading. */
    if (wait_ms == MAINUI_PREVIEW_WAIT_FOREVER) {
        return;
    }
    Uint32 now = SDL_GetTicks();
    if (!details->auto_scroll_at) {
        details->auto_scroll_at = now;
        return;
    }
    int last = details->line_count - details->lines_per_page;
    if (last < 0) {
        last = 0;
    }
    if ((Sint32)(now - details->auto_scroll_at) >= 0 && details->scroll_line < last) {
        details->scroll_line++;
        details->auto_scroll_at = now + 1800;
    }
    Uint32 elapsed = now - details->scroll_updated_at;
    details->scroll_updated_at = now;
    float target = (float)details->scroll_line;
    float distance = target - details->scroll_position;
    float movement = (float)elapsed / 1800.0f;
    if (distance > movement) {
        details->scroll_position += movement;
    }
    else if (distance < -movement) {
        details->scroll_position -= movement;
    }
    else {
        details->scroll_position = target;
    }
}

void mainui_details_markers(MainUIDetails *details, const MainUILibrary *favorites)
{
    if (details->open) {
        details->favorite = mainui_library_contains(favorites, details->rom);
    }
}

static bool folder(MainUICatalog *catalog, const MainUILibrary *library, int row)
{
    return library ? mainui_library_is_folder(library, row) : mainui_browser_folder(catalog, row);
}

/* Runs of spaces, tabs and line breaks become one space; none lead or trail. */
static void collapse_space(char *text)
{
    char *write = text;
    bool space = false;
    for (const char *read = text; *read; ++read) {
        if (*read == ' ' || *read == '\n' || *read == '\r' || *read == '\t') {
            space = write != text;
        }
        else {
            if (space) {
                *write++ = ' ';
            }
            *write++ = *read;
            space = false;
        }
    }
    *write = 0;
}

bool mainui_details_open(MainUIDetails *details, MainUICatalog *catalog,
                         const MainUILibrary *library, const MainUILibrary *favorites, int row,
                         MainUIPreview *preview)
{
    if (!preview || !catalog || row < 0 || folder(catalog, library, row)) {
        return false;
    }
    const char *label = NULL, *rom = NULL, *launch = NULL;
    int index = library ? row : mainui_browser_index(catalog, row);
    if (library) {
        if (row >= library->visible_count) {
            return false;
        }
        const MainUILibraryItem *item = &library->items[library->visible[row]];
        if (item->type == 3) {
            return false;
        }
        label = item->label;
        rom = item->rom;
        launch = item->launch;
    }
    else {
        if (!catalog->depth) {
            return false;
        }
        MainUIEntry *entry = mainui_catalog_entry(catalog, index);
        if (!entry) {
            return false;
        }
        label = entry->label;
        rom = entry->path;
        /* A Search result names its source console; other rows are the
         * open console's. */
        int system = catalog->pages[0].view.selected;
        launch = entry->launch ? entry->launch
                 : system >= 0 && system < catalog->pages[0].count
                     ? catalog->pages[0].entries[system].launch
                     : NULL;
    }
    char host[MAINUI_PATH_MAX];
    if (!mainui_catalog_path(host, catalog->sd, catalog->sd, rom) ||
        strlen(label) >= sizeof details->title) {
        return false;
    }
    mainui_details_close(details);
    details->preview = preview;
    strcpy(details->title, label);
    /* The game's console by its launcher: consoles can share a ROM folder,
     * and a saved game's may be outside the catalog (an Expert console while
     * Games is open), named by its config.json with the ROM folder its
     * gamelist.xml may be in. */
    const char *root = library ? NULL : catalog->pages[1].path;
    MainUISource source;
    if (mainui_catalog_source(catalog, rom, launch, &source)) {
        snprintf(details->system, sizeof details->system, "%s", source.label);
        root = source.rom_root;
    }
    cJSON *record = library ? NULL : mainui_catalog_record(catalog, index);
    const cJSON *saved_rom = cJSON_GetObjectItemCaseSensitive(record, "rompath");
    snprintf(details->rom, sizeof details->rom, "%s",
             library                     ? rom
             : cJSON_IsString(saved_rom) ? saved_rom->valuestring
                                         : "");
    details->favorite = mainui_library_contains(favorites, details->rom);
    cJSON_Delete(record);
    mainui_gamelist_metadata(host, root, &details->metadata);
    /* Match the reference metadata parser's collapsed XML whitespace, also in
     * the genre, where a line break would split the summary line. */
    collapse_space(details->metadata.description);
    collapse_space(details->metadata.genre);
    /* Select the cover without waiting: a cached one shows at once, any
     * other when its decode is done (mainui_details_progress). */
    details->catalog = catalog;
    details->library = library;
    details->index = index;
    details->auto_scroll_at = SDL_GetTicks() + 1400;
    details->scroll_updated_at = SDL_GetTicks();
    mainui_preview_request_within(details->preview, catalog, library, index, 0);
    int leading = library ? library->leading_folders
                          : catalog->pages[catalog->depth].folder_count + (catalog->depth > 1);
    details->ordinal = row + 1 - leading;
    details->total = library ? library->visible_games : mainui_browser_count(catalog) - leading;
    details->open = true;
    return true;
}

void mainui_details_key(MainUIDetails *details, MainUICatalog *catalog,
                        const MainUILibrary *library, const MainUILibrary *favorites,
                        MainUIViewport *view, int rows, SDLKey key)
{
    /* B/Escape, Left and Right all return to the game list from Game Info.
     * Left or Right opens Game Info from the list; while this screen is active,
     * either direction returns to the list. */
    if (key == SDLK_ESCAPE || key == SDLK_LEFT || key == SDLK_RIGHT) {
        mainui_details_close(details);
        return;
    }
    if (key == SDLK_HOME || key == SDLK_END) {
        int last = details->line_count - details->lines_per_page;
        if (last < 0) {
            last = 0;
        }
        details->scroll_line +=
            key == SDLK_HOME ? -details->lines_per_page : details->lines_per_page;
        if (details->scroll_line < 0) {
            details->scroll_line = 0;
        }
        if (details->scroll_line > last) {
            details->scroll_line = last;
        }
    }
    if (key != SDLK_UP && key != SDLK_DOWN) {
        return;
    }
    int total = library ? library->visible_count : mainui_browser_count(catalog);
    int target = view->selected, direction = key == SDLK_UP ? -1 : 1;
    for (int checked = 0; checked < total; checked++) {
        target = (target + total + direction) % total;
        if (!folder(catalog, library, target) &&
            mainui_details_open(details, catalog, library, favorites, target, details->preview)) {
            mainui_viewport_move(view, rows, target - view->selected, false);
            return;
        }
    }
}

/* Font-measured wrapping never splits a UTF-8 continuation byte. Return the
 * next input byte, preserving explicit newlines and making progress on wide
 * glyphs. Whole words are measured first; only the word that crosses the
 * width is measured a character at a time. */
static const char *next_character(const char *p)
{
    const char *next = p + 1;
    while (((unsigned char)*next & 0xc0) == 0x80) {
        next++;
    }
    return next;
}

static bool prefix_fits(TTF_Font *font, const char *start, const char *end, char out[4096],
                        int width)
{
    size_t size = (size_t)(end - start);
    if (size >= 4096) {
        return false;
    }
    memcpy(out, start, size);
    out[size] = 0;
    int measured = 0;
    return TTF_SizeUTF8(font, out, &measured, NULL) >= 0 && measured <= width;
}

static __attribute__((noinline)) void detail_line(SDL_Surface *screen, MainUITheme *theme,
                                                  TTF_Font *font, const char *text, int x, int y,
                                                  int width, bool justify)
{
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", text);
    char *words[256];
    int widths[256], count = 0, words_width = 0;
    char *cursor = copy;
    while (*cursor && count < (int)(sizeof words / sizeof words[0])) {
        while (*cursor == ' ')
            cursor++;
        if (!*cursor)
            break;
        words[count] = cursor;
        while (*cursor && *cursor != ' ')
            cursor++;
        if (*cursor)
            *cursor++ = 0;
        if (TTF_SizeUTF8(font, words[count], &widths[count], NULL) < 0) {
            mainui_label(screen, font, theme->color, text, x, y);
            return;
        }
        words_width += widths[count++];
    }
    if (!justify || count < 2) {
        mainui_label(screen, font, theme->color, text, x, y);
        return;
    }
    int gaps = count - 1, extra = width - words_width;
    if (extra < 0)
        extra = 0;
    int gap = extra / gaps, remainder = extra % gaps;
    int left = x;
    for (int i = 0; i < count; i++) {
        SDL_Surface *surface = mainui_theme_text(theme, font, words[i], theme->color);
        mainui_blit(screen, surface, left, y);
        left += widths[i];
        if (i < gaps)
            left += gap + (i < remainder);
    }
}

static const char *line(TTF_Font *font, const char *text, char out[4096], int width)
{
    const char *p = text, *fit = text, *space = NULL;
    while (*p && *p != '\n') {
        /* A space is a word of its own: wrapping prefers the last space. */
        const char *end = p;
        if (*end == ' ') {
            end++;
        }
        else {
            while (*end && *end != ' ' && *end != '\n' && (size_t)(end - text) < 4095) {
                end = next_character(end);
            }
        }
        if (end == p) {
            break;
        }
        if (prefix_fits(font, text, end, out, width)) {
            if (*p == ' ') {
                space = p;
            }
            p = fit = end;
            continue;
        }
        while (p < end) {
            const char *next = next_character(p);
            if (!prefix_fits(font, text, next, out, width)) {
                break;
            }
            fit = next;
            if (*p == ' ') {
                space = p;
            }
            p = next;
        }
        /* Shaping can make a word narrower than one of its prefixes. */
        if (p < end) {
            break;
        }
    }
    if (p == text && *p && *p != '\n') {
        fit = next_character(p);
    }
    else if (*p && *p != '\n' && space) {
        fit = space;
    }
    size_t size = (size_t)(fit - text);
    memcpy(out, text, size);
    out[size] = 0;
    if (*fit == ' ' || *fit == '\n') {
        fit++;
    }
    return fit;
}

/* The title's line breaks, from the theme's layouts of the last 5 titles,
 * or worked out and kept in the least recently used one. NULL for a title
 * too long to keep (over 4095 bytes or 256 lines): it is wrapped each time. */
static const MainUITitleLayout *title_lines(MainUITheme *theme, const char *title, int width)
{
    MainUITitleLayout *layout = &theme->title_layouts[0];
    for (int i = 0; i < 5; i++) {
        MainUITitleLayout *kept = &theme->title_layouts[i];
        if (kept->used && kept->font == theme->title_font && kept->width == width &&
            !strcmp(kept->title, title)) {
            kept->used = ++theme->title_layout_clock;
            return kept;
        }
        if (kept->used < layout->used) {
            layout = kept;
        }
    }
    size_t bytes = strlen(title);
    if (bytes >= sizeof layout->title) {
        return NULL;
    }
    layout->used = 0;
    layout->count = 0;
    char wrapped[4096];
    for (const char *p = title; *p;) {
        if (layout->count >= sizeof layout->lines / sizeof layout->lines[0]) {
            return NULL;
        }
        const char *start = p;
        p = line(theme->title_font, p, wrapped, width);
        layout->lines[layout->count].start = (unsigned short)(start - title);
        layout->lines[layout->count++].length = (unsigned short)strlen(wrapped);
    }
    memcpy(layout->title, title, bytes + 1);
    layout->font = theme->title_font;
    layout->width = width;
    layout->used = ++theme->title_layout_clock;
    return layout;
}

static void detail_footer_value(SDL_Surface *screen, MainUITheme *theme, TTF_Font *font,
                                const char *label, const char *value, int x, int y, int width)
{
    char text[512];
    size_t used = strlen(label);
    if (used >= sizeof text)
        return;
    memcpy(text, label, used);
    text[used] = 0;
    const char *cursor = value;
    while (*cursor) {
        const char *next = next_character(cursor);
        size_t bytes = (size_t)(next - cursor);
        if (used + bytes + 4 >= sizeof text)
            break;
        memcpy(text + used, cursor, bytes);
        text[used + bytes] = 0;
        int measured = 0;
        if (TTF_SizeUTF8(font, text, &measured, NULL) < 0 || measured > width - 18)
            break;
        used += bytes;
        cursor = next;
    }
    if (*cursor && used + 3 < sizeof text) {
        memcpy(text + used, "...", 4);
    }
    mainui_label(screen, font, theme->hint_color, text, x, y);
}

static void detail_footer_value_right(SDL_Surface *screen, MainUITheme *theme, TTF_Font *font,
                                      const char *label, const char *value, int x, int y, int width)
{
    char text[128];
    snprintf(text, sizeof text, "%s%s", label, value);
    int measured = 0;
    if (TTF_SizeUTF8(font, text, &measured, NULL) < 0 || measured > width) {
        detail_footer_value(screen, theme, font, label, value, x, y, width);
        return;
    }
    mainui_label(screen, font, theme->hint_color, text, x + width - measured, y);
}

static void details_metadata_footer(MainUIDetails *details, MainUITheme *theme, SDL_Surface *screen)
{
    SDL_Rect strip = {0, 420, 640, 60};
    if (theme->background && theme->background->w >= 640 && theme->background->h >= 480) {
        SDL_BlitSurface(theme->background, &strip, screen, &strip);
    }
    else {
        SDL_FillRect(screen, &strip, SDL_MapRGB(screen->format, 24, 24, 24));
    }
    TTF_Font *font = theme->detail_font;
    detail_footer_value(screen, theme, font, "Dev: ", details->metadata.developer, 16, 423, 242);
    detail_footer_value(screen, theme, font, "Genre: ", details->metadata.genre, 274, 423, 350);
    detail_footer_value(screen, theme, font, "Pub: ", details->metadata.publisher, 16, 447, 242);
    detail_footer_value(screen, theme, font, "Players: ", details->metadata.players, 274, 447, 150);
    detail_footer_value_right(screen, theme, font, "Date: ", details->metadata.release_date, 430,
                              447, 194);
}

void mainui_details_draw(MainUIDetails *details, MainUITheme *theme, SDL_Surface *screen)
{
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 24, 24, 24));
    mainui_blit(screen, theme->background, 0, 0);
    mainui_draw_header(screen, theme, mainui_translate(84, "GAME INFO"));
    mainui_draw_detail_counter(screen, theme, details->ordinal);
    if (details->favorite && theme->favorite) {
        int x = 634 - theme->favorite->w;
        mainui_blit(screen, theme->favorite, x < 274 ? 274 : x, 60);
    }
    SDL_Surface *image = details->preview && details->preview->image
                             ? details->preview->image
                             : mainui_theme_detail_default(theme);
    if (image) {
        mainui_blit(screen, image, (250 - image->w) / 2, 67);
    }
    SDL_Rect clip = {274, 98, 360, 322};
    SDL_SetClipRect(screen, &clip);
    char wrapped[4096];
    const char *text = details->title;
    int y = 98, height = TTF_FontHeight(theme->title_font) * 7 / 8, count = 0;
    if (height < 20) {
        height = 20;
    }
    if (height > 40) {
        height = 40;
    }
    /* The title is the same on every frame: its line breaks and the lines'
     * surfaces are kept rather than measured and rendered again. */
    const MainUITitleLayout *layout = title_lines(theme, text, 360);
    for (unsigned i = 0; layout && i < layout->count; i++) {
        memcpy(wrapped, text + layout->lines[i].start, layout->lines[i].length);
        wrapped[layout->lines[i].length] = 0;
        SDL_Surface *surface = mainui_theme_text(theme, theme->title_font, wrapped, theme->color);
        mainui_blit(screen, surface, 274, y);
        y += height;
    }
    while (!layout && *text) {
        text = line(theme->title_font, text, wrapped, 360);
        mainui_label(screen, theme->title_font, theme->color, wrapped, 274, y);
        y += height;
    }
    /* Leave the content area below the title for the scrollable description. */
    y += 14;
    int metadata_height = TTF_FontHeight(theme->detail_font);
    if (metadata_height < 10) {
        metadata_height = 10;
    }
    if (metadata_height > 40) {
        metadata_height = 40;
    }
    height = metadata_height + 2;
    int visible = (420 - y + 2) / height;
    if (visible < 1) {
        details->lines_per_page = 0;
        SDL_SetClipRect(screen, NULL);
        details_metadata_footer(details, theme, screen);
        return;
    }
    text = details->metadata.description;
    count = 0;
    while (*text) {
        text = line(theme->detail_font, text, wrapped, 360);
        count++;
    }
    details->line_count = count;
    details->lines_per_page = visible;
    int start = (int)details->scroll_position;
    /* Patcher meta_input clamps to line_count - lines_per_page, so the final
     * page remains full even when its last advance is less than one page. */
    int last = count > visible ? count - visible : 0;
    if (start > last) {
        start = last;
    }
    if (details->scroll_line > last) {
        details->scroll_line = last;
    }
    if (details->scroll_position > last) {
        details->scroll_position = (float)last;
    }
    int offset = (int)((details->scroll_position - start) * height);
    SDL_Rect description_clip = {274, (Sint16)y, 360, (Sint16)(420 - y)};
    SDL_SetClipRect(screen, &description_clip);
    text = details->metadata.description;
    for (int i = 0; *text && i <= start + visible; i++) {
        const char *next = line(theme->detail_font, text, wrapped, 360);
        if (i >= start) {
            detail_line(screen, theme, theme->detail_font, wrapped, 274,
                        y + (i - start) * height - offset, 360, *next && next[-1] != '\n');
        }
        text = next;
    }
    SDL_SetClipRect(screen, NULL);
    details_metadata_footer(details, theme, screen);
}
