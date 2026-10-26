/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MAINUI_MEDIA_H
#define MAINUI_MEDIA_H
#include "ui/theme.h"
#include <SDL.h>
#include <stdbool.h>

#define MAINUI_MEDIA_ITEMS 128
#define MAINUI_MEDIA_PATH 4096
typedef enum {
    MAINUI_MEDIA_MENU,
    MAINUI_MEDIA_IMAGE,
    MAINUI_MEDIA_TEXT,
    MAINUI_MEDIA_INFO
} MainUIMediaMode;
typedef struct {
    char label[96];
    char path[MAINUI_MEDIA_PATH];
    int type;
} MainUIMediaItem;
typedef struct {
    bool open;
    MainUIMediaMode mode;
    char game[256];
    char directory[MAINUI_MEDIA_PATH];
    MainUIMediaItem items[MAINUI_MEDIA_ITEMS];
    int count, selected, page;
    SDL_Surface *image;
    char text[16384];
    char status[256];
} MainUIMedia;

bool mainui_media_open(MainUIMedia *media, const char *rom_path);\nbool mainui_media_open_carousel(MainUIMedia *media, const char *rom_path);
void mainui_media_close(MainUIMedia *media);
void mainui_media_key(MainUIMedia *media, SDLKey key);
void mainui_media_draw(MainUIMedia *media, MainUITheme *theme, SDL_Surface *screen);
#endif