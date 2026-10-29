/* SPDX-License-Identifier: GPL-3.0-only */
#include "app/media.h"
#include "ui/artwork.h"
#include "ui/drawing.h"
#include "ui/panels.h"
#include <SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

enum {
    MEDIA_IMAGE = 1,
    MEDIA_TEXT,
    MEDIA_INFO,
    MEDIA_MAP,
    MEDIA_VIDEO,
    MEDIA_GALLERY,
    MEDIA_GAME_INFO,
    MEDIA_PDF
};

static bool regular(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool path_join(char out[MAINUI_MEDIA_PATH], const char *a, const char *b)
{
    int n = snprintf(out, MAINUI_MEDIA_PATH, "%s/%s", a, b);
    return n > 0 && n < MAINUI_MEDIA_PATH;
}

static void add(MainUIMedia *m, const char *label, const char *path, int type)
{
    if (m->count >= MAINUI_MEDIA_ITEMS || !regular(path))
        return;
    MainUIMediaItem *item = &m->items[m->count++];
    *item = (MainUIMediaItem){.type = type};
    snprintf(item->label, sizeof item->label, "%s", label);
    snprintf(item->path, sizeof item->path, "%s", path);
}

static bool gallery_add(MainUIMedia *m, const char *label, const char *path, int type)
{
    if (m->gallery_items >= MAINUI_MEDIA_ITEMS || !regular(path))
        return false;
    if (m->gallery_items == m->gallery_capacity) {
        int capacity = m->gallery_capacity ? m->gallery_capacity * 2 : 8;
        if (capacity > MAINUI_MEDIA_ITEMS)
            capacity = MAINUI_MEDIA_ITEMS;
        MainUIMediaItem *items = realloc(m->gallery, (size_t)capacity * sizeof *items);
        if (!items)
            return false;
        m->gallery = items;
        m->gallery_capacity = capacity;
    }
    MainUIMediaItem *item = &m->gallery[m->gallery_items++];
    *item = (MainUIMediaItem){.type = type};
    snprintf(item->label, sizeof item->label, "%s", label);
    snprintf(item->path, sizeof item->path, "%s", path);
    return true;
}

static void add_gallery_menu_item(MainUIMedia *m, const char *label, int first, int count)
{
    if (count <= 0 || m->count >= MAINUI_MEDIA_ITEMS)
        return;
    MainUIMediaItem *item = &m->items[m->count++];
    *item =
        (MainUIMediaItem){.type = MEDIA_GALLERY, .gallery_first = first, .gallery_count = count};
    snprintf(item->label, sizeof item->label, "%s", label);
}

static void add_game_info_item(MainUIMedia *m)
{
    if (m->count >= MAINUI_MEDIA_ITEMS)
        return;
    MainUIMediaItem *item = &m->items[m->count++];
    *item = (MainUIMediaItem){.type = MEDIA_GAME_INFO};
    snprintf(item->label, sizeof item->label, "Game Info");
}

static __attribute__((noinline)) void add_named(MainUIMedia *m, const char *folder,
                                                const char *suffix, const char *label, int type)
{
    char name[512], path[MAINUI_MEDIA_PATH];
    if (snprintf(name, sizeof name, "%s%s", m->game, suffix) >= (int)sizeof name ||
        snprintf(path, sizeof path, "%s/%s/%s", m->directory, folder, name) >= (int)sizeof path)
        return;
    add(m, label, path, type);
}

static __attribute__((noinline)) void add_box_art(MainUIMedia *m)
{
    static const struct {
        const char *folder, *label;
    } covers[] = {{"media/Box2D", "Front cover"}, {"media/Box2DBack", "Back cover"}};

    int first = m->gallery_items;
    char path[MAINUI_MEDIA_PATH];
    for (size_t i = 0; i < sizeof covers / sizeof covers[0]; i++) {
        if (snprintf(path, sizeof path, "%s/%s/%s.png", m->directory, covers[i].folder, m->game) <
            (int)sizeof path) {
            gallery_add(m, covers[i].label, path, MEDIA_IMAGE);
        }
    }
    add_gallery_menu_item(m, "Box Art", first, m->gallery_items - first);
}

static bool starts_map(const char *name, const char *stem)
{
    size_t n = strlen(stem);
    if (strncasecmp(name, stem, n) || strncasecmp(name + n, "_map", 4))
        return false;
    const char *p = name + n + 4;
    return *p >= '0' && *p <= '9';
}

static bool image_file(const char *name)
{
    const char *extension = strrchr(name, '.');
    return extension && (!strcasecmp(extension, ".png") || !strcasecmp(extension, ".jpg") ||
                         !strcasecmp(extension, ".jpeg") || !strcasecmp(extension, ".bmp"));
}

static bool video_extension(const char *extension, int *priority)
{
    static const char *extensions[] = {".mp4", ".mkv", ".avi", ".mov", ".webm", ".m4v"};
    for (int i = 0; i < (int)(sizeof extensions / sizeof extensions[0]); i++) {
        if (!strcasecmp(extension, extensions[i])) {
            *priority = i;
            return true;
        }
    }
    return false;
}

static __attribute__((noinline)) void add_video(MainUIMedia *m)
{
    char *directory = malloc(MAINUI_MEDIA_PATH);
    char *path = malloc(MAINUI_MEDIA_PATH);
    char best[sizeof m->game + sizeof ".webm"] = "";
    if (!directory || !path || !path_join(directory, m->directory, "media/Videos")) {
        free(directory);
        free(path);
        return;
    }
    DIR *d = opendir(directory);
    if (!d) {
        free(directory);
        free(path);
        return;
    }
    int best_priority = 6;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        const char *extension = strrchr(entry->d_name, '.');
        int priority;
        if (!extension || !video_extension(extension, &priority) || priority >= best_priority ||
            (size_t)(extension - entry->d_name) != strlen(m->game) ||
            strncasecmp(entry->d_name, m->game, strlen(m->game))) {
            continue;
        }
        if (path_join(path, directory, entry->d_name) && regular(path)) {
            snprintf(best, sizeof best, "%s", entry->d_name);
            best_priority = priority;
        }
    }
    closedir(d);
    if (*best && path_join(path, directory, best))
        add(m, "Gameplay", path, MEDIA_VIDEO);
    free(directory);
    free(path);
}

static int map_name_compare(const void *a, const void *b)
{
    const char *first = strrchr(*(const char *const *)a, '/');
    const char *second = strrchr(*(const char *const *)b, '/');
    const unsigned char *x = (const unsigned char *)(first ? first + 1 : *(const char *const *)a);
    const unsigned char *y = (const unsigned char *)(second ? second + 1 : *(const char *const *)b);
    while (*x && *y) {
        if (*x >= '0' && *x <= '9' && *y >= '0' && *y <= '9') {
            unsigned long nx = 0, ny = 0;
            while (*x >= '0' && *x <= '9') {
                nx = nx * 10 + (*x++ - '0');
            }
            while (*y >= '0' && *y <= '9') {
                ny = ny * 10 + (*y++ - '0');
            }
            if (nx != ny)
                return nx < ny ? -1 : 1;
            continue;
        }
        if (*x != *y)
            return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return (*x != 0) - (*y != 0);
}

/* Read the optional title from the matching <map path="..." title="..."/>
 * element. XML is only metadata here; paths are never taken from the XML. */

static __attribute__((noinline)) char *load_map_xml(const MainUIMedia *m)
{
    char path[MAINUI_MEDIA_PATH];
    if (!path_join(path, m->directory, "gamelist.xml"))
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *xml = malloc(8u * 1024u * 1024u + 1);
    if (!xml) {
        fclose(f);
        return NULL;
    }
    size_t size = fread(xml, 1, 8u * 1024u * 1024u, f);
    bool read_error = ferror(f) != 0;
    bool too_large = !read_error && size == 8u * 1024u * 1024u && fgetc(f) != EOF;
    fclose(f);
    if (too_large || read_error) {
        free(xml);
        return NULL;
    }
    xml[size] = 0;
    return xml;
}

/* Read the optional title from the matching <map path="..." title="..."/>
 * element. XML is only metadata here; paths are never taken from the XML. */
static bool map_xml_title(const char *xml, const char *filename, char out[96])
{
    if (!xml)
        return false;
    char *match = strstr(xml, filename);
    bool found = false;
    if (match) {
        char *start = match;
        while (start > xml && *start != '<')
            start--;
        char *end = strchr(match, '>');
        if (*start == '<' && end) {
            char *title = strstr(start, "title=");
            if (title && title < end) {
                title += 6;
                if (*title == '"' || *title == '\'') {
                    char quote = *title++;
                    char *finish = memchr(title, quote, (size_t)(end - title));
                    if (finish && finish > title) {
                        size_t n = (size_t)(finish - title);
                        if (n >= 96)
                            n = 95;
                        memcpy(out, title, n);
                        out[n] = 0;

                        const struct {
                            const char *from, *to;
                        } entities[] = {{"&amp;", "&"},
                                        {"&apos;", "'"},
                                        {"&quot;", "\""},
                                        {"&lt;", "<"},
                                        {"&gt;", ">"}};

                        char decoded[96];
                        snprintf(decoded, sizeof decoded, "%s", out);
                        out[0] = 0;
                        for (size_t i = 0; decoded[i] && strlen(out) < 94;) {
                            bool replaced = false;
                            for (size_t j = 0; j < sizeof entities / sizeof entities[0]; j++) {
                                size_t len = strlen(entities[j].from);
                                if (!strncmp(decoded + i, entities[j].from, len)) {
                                    strcat(out, entities[j].to);
                                    i += len;
                                    replaced = true;
                                    break;
                                }
                            }
                            if (!replaced) {
                                size_t used = strlen(out);
                                out[used] = decoded[i++];
                                out[used + 1] = 0;
                            }
                        }
                        found = out[0] != 0;
                    }
                }
            }
        }
    }
    return found;
}

static __attribute__((noinline)) void add_maps(MainUIMedia *m)
{
    static const char *folders[] = {"media/maps", "media/Maps"};
    char **paths = calloc(MAINUI_MEDIA_ITEMS, sizeof *paths);
    if (!paths)
        return;
    char path[MAINUI_MEDIA_PATH];
    size_t count = 0;
    int first = m->gallery_items;
    for (size_t folder = 0; folder < sizeof folders / sizeof folders[0]; folder++) {
        if (!path_join(path, m->directory, folders[folder]))
            continue;
        DIR *d = opendir(path);
        if (!d)
            continue;
        struct dirent *e;
        while ((e = readdir(d)) != NULL && count < MAINUI_MEDIA_ITEMS) {
            if (!starts_map(e->d_name, m->game) || !image_file(e->d_name))
                continue;
            if (snprintf(path, MAINUI_MEDIA_PATH, "%s/%s/%s", m->directory, folders[folder],
                         e->d_name) >= MAINUI_MEDIA_PATH ||
                !regular(path))
                continue;
            bool duplicate = false;
            for (size_t i = 0; i < count; i++) {
                const char *name = strrchr(paths[i], '/');
                if (!strcasecmp(name ? name + 1 : paths[i], e->d_name)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate && (paths[count] = strdup(path)))
                count++;
        }
        closedir(d);
    }
    qsort(paths, count, sizeof *paths, map_name_compare);
    char *xml = load_map_xml(m);
    for (size_t i = 0; i < count && m->gallery_items < MAINUI_MEDIA_ITEMS; i++) {
        char label[96];
        const char *name = strrchr(paths[i], '/');
        name = name ? name + 1 : paths[i];
        if (!map_xml_title(xml, name, label)) {
            snprintf(label, sizeof label, "Game Map %zu", i + 1);
        }
        gallery_add(m, label, paths[i], MEDIA_MAP);
        free(paths[i]);
    }
    free(xml);
    free(paths);
    add_gallery_menu_item(m, "Game Maps", first, m->gallery_items - first);
}

static void load_text(MainUIMedia *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n = 0;
    if (f) {
        n = fread(m->text, 1, sizeof m->text - 1, f);
        fclose(f);
    }
    m->text[n] = 0;
    m->text_scroll = 0;
    m->text_lines = 0;
    m->mode = MAINUI_MEDIA_TEXT;
}

bool mainui_media_open(MainUIMedia *m, const char *rom_path)
{
    if (!m || !rom_path || !*rom_path)
        return false;
    mainui_media_close(m);
    const char *slash = strrchr(rom_path, '/');
    if (!slash || slash == rom_path || (size_t)(slash - rom_path) >= sizeof m->directory ||
        strlen(slash + 1) >= sizeof m->game)
        return false;
    memcpy(m->directory, rom_path, (size_t)(slash - rom_path));
    m->directory[slash - rom_path] = 0;
    snprintf(m->game, sizeof m->game, "%s", slash + 1);
    char *dot = strrchr(m->game, '.');
    if (dot)
        *dot = 0;
    mainui_gamelist_metadata(rom_path, m->directory, &m->metadata);
    add_game_info_item(m);
    add_box_art(m);
    add_named(m, "media/Screenshot", ".png", "Screenshot", MEDIA_IMAGE);
    add_video(m);
    add_named(m, "media/manuals", ".pdf", "Manual (PDF)", MEDIA_PDF);
    char path[MAINUI_MEDIA_PATH];
    if (snprintf(path, sizeof path, "%s/%s.txt", m->directory, m->game) < (int)sizeof path)
        add(m, "Walkthrough (TXT)", path, MEDIA_TEXT);
    add_maps(m);
    if (!m->count) {
        snprintf(m->status, sizeof m->status, "No media found for this game.");
    }
    m->open = true;
    m->mode = MAINUI_MEDIA_MENU;
    m->selected = 0;
    return true;
}

static void open_item(MainUIMedia *m);

static void swap_items(MainUIMediaItem *first, MainUIMediaItem *second)
{
    MainUIMediaItem temporary = *first;
    *first = *second;
    *second = temporary;
}

bool mainui_media_open_carousel(MainUIMedia *m, const char *rom_path)
{
    if (!mainui_media_open(m, rom_path))
        return false;
    m->compact = true;
    int screenshot = -1, gameplay = -1;
    for (int i = 0; i < m->count; i++) {
        if (!strcmp(m->items[i].label, "Screenshot"))
            screenshot = i;
        else if (!strcmp(m->items[i].label, "Gameplay"))
            gameplay = i;
    }
    int count = (screenshot >= 0) + (gameplay >= 0);
    if (screenshot >= 0 && screenshot != 0)
        swap_items(&m->items[0], &m->items[screenshot]);
    gameplay = -1;
    for (int i = 0; i < m->count; i++) {
        if (!strcmp(m->items[i].label, "Gameplay"))
            gameplay = i;
    }
    int gameplay_slot = screenshot >= 0 ? 1 : 0;
    if (gameplay >= 0 && gameplay != gameplay_slot) {
        swap_items(&m->items[gameplay_slot], &m->items[gameplay]);
    }
    m->count = count;
    m->selected = 0;
    if (!count) {
        snprintf(m->status, sizeof m->status, "No screenshot or video found.");
    }
    else {
        open_item(m);
    }
    return true;
}

bool mainui_media_selected_pdf(const MainUIMedia *m)
{
    return m && m->open && m->selected >= 0 && m->selected < m->count &&
           m->items[m->selected].type == MEDIA_PDF;
}

void mainui_media_close(MainUIMedia *m)
{
    if (!m)
        return;
    if (m->image)
        SDL_FreeSurface(m->image);
    free(m->gallery);
    *m = (MainUIMedia){0};
}

static SDL_Surface *fit_image(SDL_Surface *image, int max_height)
{
    int width = image->w, height = image->h;
    if (width <= 640 && height <= max_height)
        return image;
    double scale = 640.0 / width;
    if ((double)max_height / height < scale)
        scale = (double)max_height / height;
    width = (int)(image->w * scale);
    height = (int)(image->h * scale);
    SDL_Surface *scaled = SDL_CreateRGBSurface(
        SDL_SWSURFACE, width, height, image->format->BitsPerPixel, image->format->Rmask,
        image->format->Gmask, image->format->Bmask, image->format->Amask);
    if (!scaled || SDL_SoftStretch(image, NULL, scaled, NULL) != 0) {
        if (scaled)
            SDL_FreeSurface(scaled);
        return image;
    }
    SDL_FreeSurface(image);
    return scaled;
}

static bool load_image(MainUIMedia *m, const MainUIMediaItem *item)
{
    SDL_Surface *loaded = IMG_Load(item->path);
    SDL_Surface *normalized = mainui_artwork_software(loaded);
    if (loaded)
        SDL_FreeSurface(loaded);
    if (!normalized) {
        snprintf(m->status, sizeof m->status, "Cannot load image.");
        m->mode = MAINUI_MEDIA_INFO;
        return false;
    }
    normalized = fit_image(normalized, 400);
    if (m->image)
        SDL_FreeSurface(m->image);
    m->image = normalized;
    m->mode = MAINUI_MEDIA_IMAGE;
    return true;
}

static void open_gallery_image(MainUIMedia *m)
{
    if (m->gallery_count <= 0 || m->gallery_index < 0 || m->gallery_index >= m->gallery_count)
        return;
    load_image(m, &m->gallery[m->gallery_first + m->gallery_index]);
}

static const char *next_utf8(const char *text)
{
    const char *next = text + 1;
    while (((unsigned char)*next & 0xc0) == 0x80)
        next++;
    return next;
}

static const char *media_line(TTF_Font *font, const char *text, char *out, size_t capacity,
                              int width)
{
    const char *cursor = text, *fit = text, *space = NULL;
    while (*cursor && *cursor != '\n') {
        const char *next = next_utf8(cursor);
        size_t length = (size_t)(next - text);
        if (length >= capacity)
            break;
        memcpy(out, text, length);
        out[length] = 0;
        int measured = 0;
        if (TTF_SizeUTF8(font, out, &measured, NULL) < 0 || measured > width) {
            if (space && space > text)
                fit = space;
            else if (fit == text)
                fit = next;
            break;
        }
        if (*cursor == ' ')
            space = cursor;
        cursor = fit = next;
    }
    if (fit == text && *text == '\n')
        fit++;
    else if (*fit == ' ' || *fit == '\n')
        fit++;
    size_t length = (size_t)(fit - text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\n'))
        length--;
    if (length >= capacity)
        length = capacity - 1;
    memcpy(out, text, length);
    out[length] = 0;
    return fit;
}

static __attribute__((noinline)) void media_justify_line(SDL_Surface *screen, MainUITheme *theme,
                                                         const char *text, int x, int y, int width,
                                                         bool justify)
{
    char copy[512];
    snprintf(copy, sizeof copy, "%s", text);
    char *words[64];
    int widths[64], count = 0, words_width = 0;
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
        if (TTF_SizeUTF8(theme->detail_font, words[count], &widths[count], NULL) < 0) {
            mainui_label(screen, theme->detail_font, theme->color, text, x, y);
            return;
        }
        words_width += widths[count++];
    }
    if (!justify || count < 2) {
        mainui_label(screen, theme->detail_font, theme->color, text, x, y);
        return;
    }
    int gaps = count - 1, extra = width - words_width;
    if (extra < 0)
        extra = 0;
    int gap = extra / gaps, remainder = extra % gaps, left = x;
    for (int i = 0; i < count; i++) {
        SDL_Surface *surface = mainui_theme_text(theme, theme->detail_font, words[i], theme->color);
        mainui_blit(screen, surface, left, y);
        left += widths[i];
        if (i < gaps)
            left += gap + (i < remainder);
    }
}

enum {
    MEDIA_ALIGN_LEFT,
    MEDIA_ALIGN_RIGHT
};

static int media_metadata_heading(SDL_Surface *screen, MainUITheme *theme, const char *label, int x,
                                  int y, int width, int alignment)
{
    TTF_Font *font = theme->detail_font;
    int style = TTF_GetFontStyle(font);
    TTF_SetFontStyle(font, style | TTF_STYLE_BOLD);
    int measured = 0;
    TTF_SizeUTF8(font, label, &measured, NULL);
    int start = alignment == MEDIA_ALIGN_RIGHT ? x + width - measured : x;
    mainui_label(screen, font, theme->color, label, start, y);
    TTF_SetFontStyle(font, style);
    return start;
}

static void media_metadata_field(SDL_Surface *screen, MainUITheme *theme, const char *label,
                                 const char *value, int x, int y, int width, int alignment)
{
    TTF_Font *font = theme->detail_font;
    int value_width = alignment == MEDIA_ALIGN_RIGHT ? width : x + width - x;
    char text[512] = "";
    size_t used = 0;
    const char *cursor = value;
    while (*cursor) {
        const char *next = next_utf8(cursor);
        size_t bytes = (size_t)(next - cursor);
        if (used + bytes + 4 >= sizeof text)
            break;
        memcpy(text + used, cursor, bytes);
        text[used + bytes] = 0;
        int measured = 0;
        if (TTF_SizeUTF8(font, text, &measured, NULL) < 0 || measured > value_width)
            break;
        used += bytes;
        cursor = next;
    }
    if (*cursor && used + 3 < sizeof text)
        memcpy(text + used, "...", 4);
    int value_x = media_metadata_heading(screen, theme, label, x, y, width, alignment);
    mainui_label(screen, font, theme->color, text, value_x, y + 20);
}

static int media_metadata_wrap_field(SDL_Surface *screen, MainUITheme *theme, const char *label,
                                     const char *value, int x, int y, int width)
{
    media_metadata_heading(screen, theme, label, x, y, width, MEDIA_ALIGN_LEFT);
    const char *text = value;
    char wrapped[512];
    int rows = 0;
    while (*text) {
        const char *next = media_line(theme->detail_font, text, wrapped, sizeof wrapped, width);
        if (next == text)
            break;
        mainui_label(screen, theme->detail_font, theme->color, wrapped, x, y + 20 + rows * 22);
        text = next;
        rows++;
    }
    return rows ? rows : 1;
}

static void open_item(MainUIMedia *m)
{
    if (m->selected < 0 || m->selected >= m->count)
        return;
    MainUIMediaItem *item = &m->items[m->selected];
    if (item->type == MEDIA_GALLERY) {
        m->gallery_active = true;
        m->gallery_first = item->gallery_first;
        m->gallery_count = item->gallery_count;
        m->gallery_index = 0;
        m->status[0] = 0;
        open_gallery_image(m);
    }
    else if (item->type == MEDIA_IMAGE) {
        m->gallery_active = false;
        load_image(m, item);
    }
    else if (item->type == MEDIA_TEXT) {
        load_text(m, item->path);
    }
    else if (item->type == MEDIA_VIDEO) {
        snprintf(m->status, sizeof m->status, "Starting video...");
        m->mode = MAINUI_MEDIA_PLAYBACK;
    }
    else if (item->type == MEDIA_GAME_INFO) {
        m->game_info_page = 0;
        m->game_info_scroll_line = 0;
        m->game_info_scroll_last = -1;
        m->game_info_scroll_position = 0;
        m->game_info_scroll_at = 0;
        m->game_info_scroll_updated_at = 0;
        m->game_info_lines = -1;
        m->text_scroll = 0;
        m->mode = MAINUI_MEDIA_GAME_INFO;
    }
    else if (item->type == MEDIA_PDF) {
        snprintf(m->status, sizeof m->status, "Opening PDF manual...");
        m->mode = MAINUI_MEDIA_PLAYBACK;
    }
    else {
        snprintf(m->status, sizeof m->status,
                 "No PDF reader is installed in this build.\nAvailable at:\n%s", item->path);
        m->mode = MAINUI_MEDIA_INFO;
    }
}

void mainui_media_video_finished(MainUIMedia *m, bool success)
{
    if (!m || !m->open || m->mode != MAINUI_MEDIA_PLAYBACK)
        return;
    (void)success;
    if (m->compact) {
        mainui_media_close(m);
        return;
    }
    m->status[0] = 0;
    m->gallery_active = false;
    m->mode = MAINUI_MEDIA_MENU;
}

void mainui_media_key(MainUIMedia *m, SDLKey key)
{
    if (!m || !m->open)
        return;
    if (key == SDLK_ESCAPE) {
        if (m->compact || m->mode == MAINUI_MEDIA_MENU) {
            mainui_media_close(m);
        }
        else {
            m->mode = MAINUI_MEDIA_MENU;
            m->gallery_active = false;
            m->status[0] = 0;
        }
        return;
    }
    if (m->mode == MAINUI_MEDIA_MENU) {
        if (key == SDLK_UP && m->count)
            m->selected = (m->selected + m->count - 1) % m->count;
        else if (key == SDLK_DOWN && m->count)
            m->selected = (m->selected + 1) % m->count;
        else if (key == SDLK_RETURN)
            open_item(m);
    }
    else if (m->mode == MAINUI_MEDIA_TEXT) {
        int direction = (key == SDLK_UP || key == SDLK_PAGEUP || key == SDLK_HOME)      ? -1
                        : (key == SDLK_DOWN || key == SDLK_PAGEDOWN || key == SDLK_END) ? 1
                                                                                        : 0;
        if (direction) {
            int lines = m->text_lines;
            if (!lines) {
                for (const char *p = m->text; *p; p++) {
                    if (*p == '\n')
                        lines++;
                }
                if (*m->text && m->text[strlen(m->text) - 1] != '\n')
                    lines++;
            }
            int visible = 16;
            int last = lines > visible ? lines - visible : 0;
            bool page =
                key == SDLK_PAGEUP || key == SDLK_PAGEDOWN || key == SDLK_HOME || key == SDLK_END;
            m->text_scroll += direction * (page ? visible : 1);
            if (m->text_scroll < 0)
                m->text_scroll = 0;
            if (m->text_scroll > last)
                m->text_scroll = last;
        }
    }
    else if (m->mode == MAINUI_MEDIA_GAME_INFO) {
        if (m->game_info_page == 0) {
            if (key == SDLK_DOWN) {
                m->game_info_page = 1;
                m->text_scroll = 0;
            }
        }
        else if (key == SDLK_UP && m->text_scroll == 0) {
            m->game_info_page = 0;
            m->game_info_scroll_updated_at = SDL_GetTicks();
            m->game_info_scroll_at = m->game_info_scroll_updated_at + 1800;
        }
        else {
            int direction = (key == SDLK_UP || key == SDLK_PAGEUP || key == SDLK_HOME)      ? -1
                            : (key == SDLK_DOWN || key == SDLK_PAGEDOWN || key == SDLK_END) ? 1
                                                                                            : 0;
            if (direction) {
                int visible = 10;
                int last = m->game_info_lines > visible ? m->game_info_lines - visible : 0;
                bool page = key == SDLK_PAGEUP || key == SDLK_PAGEDOWN || key == SDLK_HOME ||
                            key == SDLK_END;
                m->text_scroll += direction * (page ? visible : 1);
                if (m->text_scroll < 0)
                    m->text_scroll = 0;
                if (m->text_scroll > last)
                    m->text_scroll = last;
            }
        }
    }
    else if (m->mode == MAINUI_MEDIA_INFO) {
        return;
    }
    else if (m->mode == MAINUI_MEDIA_PLAYBACK) {
        return;
    }
    else if (m->gallery_active && m->mode == MAINUI_MEDIA_IMAGE && m->gallery_count > 0 &&
             (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_RETURN)) {
        int direction = key == SDLK_LEFT ? -1 : 1;
        m->gallery_index = (m->gallery_index + m->gallery_count + direction) % m->gallery_count;
        open_gallery_image(m);
    }
    else if (m->compact && m->mode == MAINUI_MEDIA_IMAGE &&
             (key == SDLK_RIGHT || key == SDLK_RETURN)) {
        if (m->count) {
            m->selected = (m->selected + 1) % m->count;
            open_item(m);
        }
    }
    else if (m->mode == MAINUI_MEDIA_IMAGE) {
        return;
    }
    else if (key == SDLK_RIGHT || key == SDLK_RETURN) {
        if (m->count) {
            m->selected = (m->selected + 1) % m->count;
            open_item(m);
        }
    }
}

static void draw_image(SDL_Surface *screen, SDL_Surface *image, int max_height)
{
    if (!image)
        return;
    mainui_blit(screen, image, (640 - image->w) / 2, 60 + (max_height - image->h) / 2);
}

void mainui_media_draw(MainUIMedia *m, MainUITheme *theme, SDL_Surface *screen)
{
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 24, 24, 24));
    mainui_blit(screen, theme->background, 0, 0);
    const char *header = "MEDIA INFO";
    if (m->mode == MAINUI_MEDIA_MENU) {
        header = "GAME MEDIA";
    }
    else if (m->mode == MAINUI_MEDIA_TEXT) {
        header = "WALKTHROUGH";
    }
    else if (m->mode == MAINUI_MEDIA_GAME_INFO) {
        header = "GAME INFO";
    }
    else if (m->mode == MAINUI_MEDIA_PLAYBACK) {
        header = mainui_media_selected_pdf(m) ? "PDF READER" : "GAMEPLAY";
    }
    else if (m->gallery_active && m->gallery_index < m->gallery_count) {
        header = m->gallery[m->gallery_first + m->gallery_index].label;
    }
    else if (m->selected >= 0 && m->selected < m->count) {
        header = m->items[m->selected].label;
    }
    mainui_draw_header(screen, theme, header);
    if (m->mode == MAINUI_MEDIA_MENU) {
        int first = m->selected > 8 ? m->selected - 8 : 0;
        for (int i = first; i < m->count && i < first + 9; i++) {
            char row[128];
            snprintf(row, sizeof row, "%s%s", i == m->selected ? "> " : "  ", m->items[i].label);
            mainui_label(screen, theme->menu_font, theme->color, row, 48, 78 + (i - first) * 36);
        }
        if (!m->count)
            mainui_label(screen, theme->menu_font, theme->color, m->status, 48, 100);
    }
    else if (m->mode == MAINUI_MEDIA_IMAGE) {
        bool map_gallery = m->gallery_active && m->gallery_count > 0 &&
                           m->gallery[m->gallery_first].type == MEDIA_MAP;
        draw_image(screen, m->image, 400);
        if (map_gallery) {
            char counter[24];
            snprintf(counter, sizeof counter, "%d/%d", m->gallery_index + 1, m->gallery_count);
            mainui_label(screen, theme->hint_font, theme->hint_color, counter, 8, 458);
        }
    }
    else if (m->mode == MAINUI_MEDIA_GAME_INFO) {
        const char *title = *m->metadata.name ? m->metadata.name : m->game;
        int title_width = 0;
        TTF_SizeUTF8(theme->title_font, title, &title_width, NULL);
        int title_x = (640 - title_width) / 2;
        if (title_x < 24)
            title_x = 24;
        mainui_label(screen, theme->title_font, theme->color, title, title_x, 70);
        const char *text = m->metadata.description;
        char wrapped[512];
        if (m->game_info_lines < 0) {
            int total_lines = 0;
            while (*text) {
                const char *next =
                    media_line(theme->detail_font, text, wrapped, sizeof wrapped, 580);
                if (next == text)
                    break;
                text = next;
                total_lines++;
            }
            m->game_info_lines = total_lines;
        }
        if (m->game_info_page == 0) {
            const int title_gap = 22;
            const int first_metadata_y = 116 + title_gap;
            const int second_metadata_y = 174 + title_gap;
            TTF_Font *metadata_font = theme->detail_font;
            int metadata_style = TTF_GetFontStyle(metadata_font);
            TTF_SetFontStyle(metadata_font, metadata_style | TTF_STYLE_BOLD);
            int release_heading_width = 0;
            TTF_SizeUTF8(metadata_font, "Release Date", &release_heading_width, NULL);
            TTF_SetFontStyle(metadata_font, metadata_style);
            int right_column_x = 432 + 184 - release_heading_width;
            int middle_left = 24 + 184;
            int middle_width = right_column_x - middle_left;
            int middle_column_x = middle_left + (middle_width - 180) / 2;
            media_metadata_field(screen, theme, "Developer",
                                 *m->metadata.developer ? m->metadata.developer : "-", 24,
                                 first_metadata_y, 184, MEDIA_ALIGN_LEFT);
            media_metadata_field(screen, theme, "Publisher",
                                 *m->metadata.publisher ? m->metadata.publisher : "-",
                                 middle_column_x, first_metadata_y, 180, MEDIA_ALIGN_LEFT);
            media_metadata_field(screen, theme, "Release Date",
                                 *m->metadata.release_date ? m->metadata.release_date : "-", 432,
                                 first_metadata_y, 184, MEDIA_ALIGN_RIGHT);
            int genre_rows = media_metadata_wrap_field(screen, theme, "Genre",
                                                       *m->metadata.genre ? m->metadata.genre : "-",
                                                       24, second_metadata_y, 184);
            media_metadata_field(screen, theme, "Players",
                                 *m->metadata.players ? m->metadata.players : "-", middle_column_x,
                                 second_metadata_y, 180, MEDIA_ALIGN_LEFT);
            media_metadata_field(screen, theme, "Rating",
                                 *m->metadata.rating ? m->metadata.rating : "-", right_column_x,
                                 second_metadata_y, 616 - right_column_x, MEDIA_ALIGN_LEFT);
            int description_heading_y = 276 + title_gap;
            int genre_end_y = second_metadata_y + 20 + genre_rows * 22;
            if (description_heading_y < genre_end_y + 38) {
                description_heading_y = genre_end_y + 38;
            }
            media_metadata_heading(screen, theme, "Description", 24, description_heading_y, 592,
                                   MEDIA_ALIGN_LEFT);
            if (!m->game_info_scroll_updated_at) {
                m->game_info_scroll_updated_at = SDL_GetTicks();
                m->game_info_scroll_at = m->game_info_scroll_updated_at + 1800;
            }
            Uint32 now = SDL_GetTicks();
            int description_y = description_heading_y + 28;
            int visible = (420 - description_y) / 22;
            if (visible < 1)
                visible = 1;
            int last = m->game_info_lines > visible ? m->game_info_lines - visible : 0;
            m->game_info_scroll_last = last;
            if ((Sint32)(now - m->game_info_scroll_at) >= 0 && m->game_info_scroll_line < last) {
                m->game_info_scroll_line++;
                m->game_info_scroll_at = now + 1800;
            }
            Uint32 elapsed = now - m->game_info_scroll_updated_at;
            m->game_info_scroll_updated_at = now;
            float target = (float)m->game_info_scroll_line;
            float distance = target - m->game_info_scroll_position;
            float movement = (float)elapsed / 1800.0f;
            if (distance > movement)
                m->game_info_scroll_position += movement;
            else if (distance < -movement)
                m->game_info_scroll_position -= movement;
            else
                m->game_info_scroll_position = target;

            int start = (int)m->game_info_scroll_position;
            if (start > last)
                start = last;
            int offset = (int)((m->game_info_scroll_position - start) * 22);
            SDL_Rect clip = {18, (Sint16)description_y, 604, (Sint16)(420 - description_y)};
            SDL_SetClipRect(screen, &clip);
            text = m->metadata.description;
            for (int line = 0; *text && line <= start + visible; line++) {
                const char *next =
                    media_line(theme->detail_font, text, wrapped, sizeof wrapped, 580);
                if (line >= start) {
                    media_justify_line(screen, theme, wrapped, 24,
                                       description_y + (line - start) * 22 - offset, 580,
                                       *next && next[-1] != '\n');
                }
                text = next;
            }
        }
        else {
            media_metadata_heading(screen, theme, "Description", 24, 138, 592, MEDIA_ALIGN_LEFT);
            const int visible = 10;
            int last = m->game_info_lines > visible ? m->game_info_lines - visible : 0;
            if (m->text_scroll > last)
                m->text_scroll = last;
            SDL_Rect clip = {18, 182, 604, 238};
            SDL_SetClipRect(screen, &clip);
            text = m->metadata.description;
            for (int skipped = 0; skipped < m->text_scroll && *text; skipped++) {
                text = media_line(theme->detail_font, text, wrapped, sizeof wrapped, 580);
            }
            int y = 182;
            while (*text && y < 418) {
                const char *next =
                    media_line(theme->detail_font, text, wrapped, sizeof wrapped, 580);
                if (next == text)
                    break;
                media_justify_line(screen, theme, wrapped, 24, y, 580, *next && next[-1] != '\n');
                text = next;
                y += 22;
            }
        }
        SDL_SetClipRect(screen, NULL);
    }
    else if (m->mode == MAINUI_MEDIA_TEXT) {
        char wrapped[512];
        int lines = 0;
        const char *line = m->text;
        while (*line) {
            const char *next = media_line(theme->detail_font, line, wrapped, sizeof wrapped, 580);
            if (next == line)
                break;
            line = next;
            lines++;
        }
        m->text_lines = lines;
        int last = lines > 16 ? lines - 16 : 0;
        if (m->text_scroll > last)
            m->text_scroll = last;
        line = m->text;
        for (int skip = 0; skip < m->text_scroll && *line; skip++) {
            line = media_line(theme->detail_font, line, wrapped, sizeof wrapped, 580);
        }
        SDL_Rect clip = {18, 62, 604, 358};
        SDL_SetClipRect(screen, &clip);
        int y = 70;
        while (*line && y < 420) {
            const char *next = media_line(theme->detail_font, line, wrapped, sizeof wrapped, 580);
            if (next == line)
                break;
            mainui_label(screen, theme->detail_font, theme->color, wrapped, 20, y);
            line = next;
            y += 22;
        }
        SDL_SetClipRect(screen, NULL);
    }
    else {
        char copy[512];
        snprintf(copy, sizeof copy, "%s", m->status);
        char *line = copy, *end;
        int y = 100;
        while (*line && y < 400) {
            end = strchr(line, '\n');
            if (end)
                *end = 0;
            mainui_label(screen, theme->menu_font, theme->color, line, 28, y);
            if (!end)
                break;
            line = end + 1;
            y += 32;
        }
    }
    if (m->mode == MAINUI_MEDIA_GAME_INFO) {
        char indicator[16];
        snprintf(indicator, sizeof indicator, "%d/2", m->game_info_page + 1);
        mainui_label(screen, theme->detail_font, theme->hint_color, indicator, 8, 456);
    }
    else if (m->mode != MAINUI_MEDIA_IMAGE) {
        mainui_draw_footer(screen, theme, 0, -1);
    }
}
