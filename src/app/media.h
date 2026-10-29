/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MAINUI_MEDIA_H
#define MAINUI_MEDIA_H
#include "catalog/gamelist.h"
#include "ui/theme.h"
#include <SDL.h>
#include <stdbool.h>

#define MAINUI_MEDIA_ITEMS 128
#define MAINUI_MEDIA_PATH 4096

typedef enum {
    MAINUI_MEDIA_MENU,
    MAINUI_MEDIA_IMAGE,
    MAINUI_MEDIA_TEXT,
    MAINUI_MEDIA_INFO,
    MAINUI_MEDIA_PLAYBACK,
    MAINUI_MEDIA_GAME_INFO
} MainUIMediaMode;

typedef struct {
    char label[96];
    char path[MAINUI_MEDIA_PATH];
    int type, gallery_first, gallery_count;
} MainUIMediaItem;

typedef struct {
    bool open;
    bool compact; /* Escape returns directly to the game list in the quick carousel. */
    bool gallery_active;
    MainUIMediaMode mode;
    char game[256];
    char directory[MAINUI_MEDIA_PATH];
    MainUIMediaItem items[MAINUI_MEDIA_ITEMS];
    MainUIMediaItem *gallery;
    int count, selected, page, text_scroll, text_lines, game_info_lines;
    int game_info_page, game_info_scroll_line, game_info_scroll_last;
    int gallery_items, gallery_capacity, gallery_first, gallery_count, gallery_index;
    float game_info_scroll_position;
    Uint32 game_info_scroll_at, game_info_scroll_updated_at;
    SDL_Surface *image;
    MainUIMetadata metadata;
    char text[16384];
    char status[256];
} MainUIMedia;

bool mainui_media_open(MainUIMedia *media, const char *rom_path);
bool mainui_media_open_carousel(MainUIMedia *media, const char *rom_path);
bool mainui_media_selected_pdf(const MainUIMedia *media);
void mainui_media_close(MainUIMedia *media);
void mainui_media_key(MainUIMedia *media, SDLKey key);
void mainui_media_video_finished(MainUIMedia *media, bool success);
void mainui_media_draw(MainUIMedia *media, MainUITheme *theme, SDL_Surface *screen);
#endif