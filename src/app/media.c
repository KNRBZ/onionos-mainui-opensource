/* SPDX-License-Identifier: GPL-3.0-only */
#include "app/media.h"
#include "ui/drawing.h"
#include "ui/panels.h"
#include <SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { MEDIA_IMAGE = 1, MEDIA_TEXT, MEDIA_INFO };
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
    if (m->count >= MAINUI_MEDIA_ITEMS || !regular(path)) return;
    MainUIMediaItem *item = &m->items[m->count++];
    snprintf(item->label, sizeof item->label, "%s", label);
    snprintf(item->path, sizeof item->path, "%s", path);
    item->type = type;
}
static void add_named(MainUIMedia *m, const char *folder, const char *suffix,
                      const char *label, int type)
{
    char dir[MAINUI_MEDIA_PATH], name[512], path[MAINUI_MEDIA_PATH];
    if (snprintf(name, sizeof name, "%s%s", m->game, suffix) >= (int)sizeof name ||
        !path_join(dir, m->directory, folder) || !path_join(path, dir, name)) return;
    add(m, label, path, type);
}
static bool starts_map(const char *name, const char *stem)
{
    size_t n = strlen(stem);
    if (strncmp(name, stem, n) || strncmp(name + n, "_map", 4)) return false;
    const char *p = name + n + 4;
    return *p >= '0' && *p <= '9';
}
static void add_maps(MainUIMedia *m)
{
    char dir[MAINUI_MEDIA_PATH];
    if (!path_join(dir, m->directory, "media/maps")) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && m->count < MAINUI_MEDIA_ITEMS) {
        if (!starts_map(e->d_name, m->game)) continue;
        char path[MAINUI_MEDIA_PATH];
        if (!path_join(path, dir, e->d_name)) continue;
        char label[96];
        snprintf(label, sizeof label, "Game Map %d", m->count + 1);
        add(m, label, path, MEDIA_IMAGE);
    }
    closedir(d);
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
    m->mode = MAINUI_MEDIA_TEXT;
}
bool mainui_media_open(MainUIMedia *m, const char *rom_path)
{
    if (!m || !rom_path || !*rom_path) return false;
    mainui_media_close(m);
    char normalized[MAINUI_MEDIA_PATH];
    if (snprintf(normalized, sizeof normalized, "%s", rom_path) >= (int)sizeof normalized) return false;
    char *slash = strrchr(normalized, '/');
    if (!slash || slash == normalized) return false;
    *slash = 0;
    snprintf(m->directory, sizeof m->directory, "%s", normalized);
    const char *base = slash + 1;
    /* slash was replaced above; recover the basename from the original path. */
    base = strrchr(rom_path, '/');
    base = base ? base + 1 : rom_path;
    snprintf(m->game, sizeof m->game, "%s", base);
    char *dot = strrchr(m->game, '.');
    if (dot) *dot = 0;
    add_named(m, "media/Box2D", ".png", "Box Art - Front", MEDIA_IMAGE);
    add_named(m, "media/Box2DBack", ".png", "Box Art - Back", MEDIA_IMAGE);
    add_named(m, "media/Screenshot", ".png", "Screenshot", MEDIA_IMAGE);
    add_named(m, "media/Videos", ".mp4", "Video", MEDIA_INFO);
    add_named(m, "media/manuals", ".pdf", "Manual (PDF)", MEDIA_INFO);
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
void mainui_media_close(MainUIMedia *m)
{
    if (!m) return;
    if (m->image) SDL_FreeSurface(m->image);
    *m = (MainUIMedia){0};
}
static void open_item(MainUIMedia *m)
{
    if (m->selected < 0 || m->selected >= m->count) return;
    MainUIMediaItem *item = &m->items[m->selected];
    if (item->type == MEDIA_IMAGE) {
        SDL_Surface *loaded = IMG_Load(item->path);
        if (!loaded) {
            snprintf(m->status, sizeof m->status, "Cannot load image.");
            m->mode = MAINUI_MEDIA_INFO;
            return;
        }
        if (m->image) SDL_FreeSurface(m->image);
        m->image = loaded;
        m->mode = MAINUI_MEDIA_IMAGE;
    } else if (item->type == MEDIA_TEXT) {
        load_text(m, item->path);
    } else {
        snprintf(m->status, sizeof m->status, "%s\nAvailable at:\n%s",
                 !strcmp(item->label, "Video") ? "Video file detected. Playback is not implemented yet."
                                                : "PDF manual detected. PDF rendering is not implemented yet.",
                 item->path);
        m->mode = MAINUI_MEDIA_INFO;
    }
}
void mainui_media_key(MainUIMedia *m, SDLKey key)
{
    if (!m || !m->open) return;
    if (key == SDLK_ESCAPE || key == SDLK_LEFT) {
        if (m->mode == MAINUI_MEDIA_MENU) mainui_media_close(m);
        else { m->mode = MAINUI_MEDIA_MENU; m->status[0] = 0; }
        return;
    }
    if (m->mode == MAINUI_MEDIA_MENU) {
        if (key == SDLK_UP && m->count) m->selected = (m->selected + m->count - 1) % m->count;
        else if (key == SDLK_DOWN && m->count) m->selected = (m->selected + 1) % m->count;
        else if (key == SDLK_RETURN) open_item(m);
    } else if (m->mode == MAINUI_MEDIA_IMAGE && (key == SDLK_RIGHT || key == SDLK_RETURN)) {
        if (m->count) { m->selected = (m->selected + 1) % m->count; open_item(m); }
    }
}
static void draw_image(SDL_Surface *screen, SDL_Surface *image)
{
    if (!image) return;
    int maxw = 600, maxh = 390, w = image->w, h = image->h;
    if (w > maxw || h > maxh) {
        double scale = (double)maxw / w;
        if ((double)maxh / h < scale) scale = (double)maxh / h;
        w = (int)(image->w * scale); h = (int)(image->h * scale);
    }
    SDL_Surface *scaled = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 32, 0, 0, 0, 0);
    if (scaled && SDL_SoftStretch(image, NULL, scaled, NULL) == 0)
        mainui_blit(screen, scaled, (640 - w) / 2, 54 + (390 - h) / 2);
    if (scaled) SDL_FreeSurface(scaled);
}
void mainui_media_draw(MainUIMedia *m, MainUITheme *theme, SDL_Surface *screen)
{
    SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 24, 24, 24));
    mainui_blit(screen, theme->background, 0, 0);
    mainui_draw_header(screen, theme, m->mode == MAINUI_MEDIA_MENU ? "GAME MEDIA" :
                       m->mode == MAINUI_MEDIA_IMAGE ? "MEDIA VIEWER" :
                       m->mode == MAINUI_MEDIA_TEXT ? "WALKTHROUGH" : "MEDIA INFO");
    if (m->mode == MAINUI_MEDIA_MENU) {
        int first = m->selected > 8 ? m->selected - 8 : 0;
        for (int i = first; i < m->count && i < first + 9; i++) {
            char row[128];
            snprintf(row, sizeof row, "%s%s", i == m->selected ? "> " : "  ", m->items[i].label);
            mainui_label(screen, theme->menu_font, theme->color, row, 48, 78 + (i - first) * 36);
        }
        if (!m->count) mainui_label(screen, theme->menu_font, theme->color, m->status, 48, 100);
    } else if (m->mode == MAINUI_MEDIA_IMAGE) {
        draw_image(screen, m->image);
        if (m->count) {
            char counter[64];
            snprintf(counter, sizeof counter, "%d/%d  %s", m->selected + 1, m->count,
                     m->items[m->selected].label);
            mainui_label(screen, theme->menu_font, theme->color, counter, 20, 438);
        }
    } else if (m->mode == MAINUI_MEDIA_TEXT) {
        char *line = m->text, *end;
        int y = 70;
        while (*line && y < 420) {
            end = strchr(line, '\n');
            if (end) *end = 0;
            char row[160];
            snprintf(row, sizeof row, "%.*s", 150, line);
            mainui_label(screen, theme->detail_font, theme->color, row, 18, y);
            if (end) { *end = '\n'; line = end + 1; } else break;
            y += 22;
        }
    } else {
        char copy[512];
        snprintf(copy, sizeof copy, "%s", m->status);
        char *line = copy, *end;
        int y = 100;
        while (*line && y < 400) {
            end = strchr(line, '\n');
            if (end) *end = 0;
            mainui_label(screen, theme->menu_font, theme->color, line, 28, y);
            if (!end) break;
            line = end + 1; y += 32;
        }
    }
    mainui_draw_footer(screen, theme, 0, -1);
}
