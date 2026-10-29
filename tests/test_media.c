/* SPDX-License-Identifier: GPL-3.0-only */
#include "app/media.h"
#include "support.h"
#include "ui/artwork.h"
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void make_directory(const char *path)
{
    assert(mkdir(path, 0755) == 0);
}

static void make_picture(const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 8, 8, 32, 0xff0000, 0xff00, 0xff, 0);
    assert(surface);
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 20, 40, 60));
    assert(SDL_SaveBMP(surface, path) == 0);
    SDL_FreeSurface(surface);
}

int mainui_suite_media(void)
{
    assert(SDL_Init(0) == 0);
    char root[256], rom[512], media[512], maps[512], shots[512], videos[512], manuals[512];
    char front_dir[512], back_dir[512], first[512], second[512], third[512];
    char screenshot[512], video[512];
    char front[512], back[512], walkthrough[512], manual[512], gamelist[512];
    TEST_PATH(root, "build/media-check-%ld", (long)getpid());
    make_directory(root);
    TEST_PATH(rom, "%s/Test Game.rom", root);
    FILE *file = fopen(rom, "wb");
    assert(file && fclose(file) == 0);
    TEST_PATH(media, "%s/media", root);
    make_directory(media);
    TEST_PATH(maps, "%s/Maps", media);
    make_directory(maps);
    TEST_PATH(front_dir, "%s/Box2D", media);
    make_directory(front_dir);
    TEST_PATH(back_dir, "%s/Box2DBack", media);
    make_directory(back_dir);
    TEST_PATH(shots, "%s/Screenshot", media);
    make_directory(shots);
    TEST_PATH(videos, "%s/Videos", media);
    make_directory(videos);
    TEST_PATH(manuals, "%s/manuals", media);
    make_directory(manuals);
    TEST_PATH(first, "%s/test game_MAP2.bmp", maps);
    TEST_PATH(second, "%s/test game_map10.BMP", maps);
    TEST_PATH(third, "%s/test game_map20.png", maps);
    TEST_PATH(front, "%s/Test Game.png", front_dir);
    TEST_PATH(back, "%s/Test Game.png", back_dir);
    TEST_PATH(screenshot, "%s/Test Game.png", shots);
    TEST_PATH(video, "%s/Test Game.MKV", videos);
    TEST_PATH(manual, "%s/Test Game.pdf", manuals);
    TEST_PATH(walkthrough, "%s/Test Game.txt", root);
    TEST_PATH(gamelist, "%s/gamelist.xml", root);
    make_picture(first);
    make_picture(second);
    make_picture(third);
    make_picture(front);
    make_picture(back);
    make_picture(screenshot);
    file = fopen(video, "wb");
    assert(file && fputs("video fixture", file) >= 0 && fclose(file) == 0);
    file = fopen(manual, "wb");
    assert(file && fputs("pdf fixture", file) >= 0 && fclose(file) == 0);
    file = fopen(walkthrough, "wb");
    assert(file);
    for (int line = 0; line < 40; line++)
        assert(fprintf(file, "Walkthrough line %d\n", line) > 0);
    assert(fclose(file) == 0);
    file = fopen(gamelist, "wb");
    assert(file &&
           fputs("<gameList><game><path>Test Game.rom</path>"
                 "<name>Example Game Name</name>"
                 "<developer>Example Developer</developer>"
                 "<publisher>Example Publisher</publisher>"
                 "<releasedate>19981021T000000</releasedate><players>1-2</players>"
                 "<rating>0.85</rating>"
                 "<genre>Action</genre><desc>A game description.</desc>"
                 "</game></gameList>",
                 file) >= 0 &&
           fclose(file) == 0);

    MainUIMedia viewer = {0};
    assert(mainui_media_open(&viewer, rom));
    assert(viewer.count == 7);
    assert(!strcmp(viewer.items[0].label, "Game Info"));
    assert(!strcmp(viewer.items[1].label, "Box Art"));
    assert(!strcmp(viewer.items[6].label, "Game Maps"));
    assert(!strcmp(viewer.metadata.developer, "Example Developer"));
    assert(!strcmp(viewer.metadata.name, "Example Game Name"));
    assert(!strcmp(viewer.metadata.publisher, "Example Publisher"));
    assert(!strcmp(viewer.metadata.release_date, "21.10.1998"));
    assert(!strcmp(viewer.metadata.players, "1-2"));
    assert(!strcmp(viewer.metadata.rating, "9/10"));
    viewer.selected = 0;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_GAME_INFO && viewer.game_info_page == 0);
    mainui_media_key(&viewer, SDLK_DOWN);
    assert(viewer.game_info_page == 1);
    mainui_media_key(&viewer, SDLK_UP);
    assert(viewer.game_info_page == 0);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU);

    viewer.selected = 1;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_IMAGE && viewer.image);
    assert(viewer.gallery_active && viewer.gallery_count == 2 && viewer.gallery_index == 0);
    assert(!strcmp(viewer.gallery[viewer.gallery_first].label, "Front cover"));
    mainui_media_key(&viewer, SDLK_DOWN);
    assert(viewer.gallery_index == 0);
    mainui_media_key(&viewer, SDLK_RIGHT);
    assert(viewer.gallery_index == 1);
    mainui_media_key(&viewer, SDLK_LEFT);
    assert(viewer.mode == MAINUI_MEDIA_IMAGE && viewer.gallery_index == 0);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU && !viewer.gallery_active);

    viewer.selected = 6;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.gallery_active && viewer.gallery_count == 3);
    mainui_media_key(&viewer, SDLK_RIGHT);
    mainui_media_key(&viewer, SDLK_RIGHT);
    assert(viewer.gallery_index == 2);
    mainui_media_key(&viewer, SDLK_LEFT);
    assert(viewer.gallery_index == 1);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU);

    viewer.selected = 2;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_IMAGE && !viewer.gallery_active);
    mainui_media_key(&viewer, SDLK_LEFT);
    assert(viewer.mode == MAINUI_MEDIA_IMAGE);
    mainui_media_key(&viewer, SDLK_RIGHT);
    assert(viewer.mode == MAINUI_MEDIA_IMAGE && viewer.selected == 2);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU);

    viewer.selected = 4;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_PLAYBACK && mainui_media_selected_pdf(&viewer));
    mainui_media_key(&viewer, SDLK_RIGHT);
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_PLAYBACK && viewer.selected == 4);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU);

    viewer.selected = 5;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_TEXT);
    mainui_media_key(&viewer, SDLK_PAGEDOWN);
    assert(viewer.text_scroll == 16);
    mainui_media_key(&viewer, SDLK_END);
    assert(viewer.text_scroll == 24);
    mainui_media_key(&viewer, SDLK_PAGEUP);
    assert(viewer.text_scroll == 8);
    mainui_media_key(&viewer, SDLK_HOME);
    assert(viewer.text_scroll == 0);
    mainui_media_key(&viewer, SDLK_ESCAPE);
    assert(viewer.mode == MAINUI_MEDIA_MENU);

    Uint8 red, green, blue;
    viewer.selected = 6;
    mainui_media_key(&viewer, SDLK_RETURN);
    SDL_GetRGB(*(Uint32 *)viewer.image->pixels, viewer.image->format, &red, &green, &blue);
    assert(red == 20 && green == 40 && blue == 60);
    mainui_media_close(&viewer);

    assert(mainui_media_open_carousel(&viewer, rom));
    assert(viewer.count == 2 && !strcmp(viewer.items[0].label, "Screenshot"));
    assert(!strcmp(viewer.items[1].label, "Gameplay"));
    mainui_media_key(&viewer, SDLK_RIGHT);
    assert(viewer.mode == MAINUI_MEDIA_PLAYBACK);
    mainui_media_video_finished(&viewer, true);
    assert(!viewer.open);
    assert(mainui_media_open(&viewer, rom));
    viewer.selected = 3;
    mainui_media_key(&viewer, SDLK_RETURN);
    assert(viewer.mode == MAINUI_MEDIA_PLAYBACK);
    mainui_media_video_finished(&viewer, false);
    assert(viewer.mode == MAINUI_MEDIA_MENU && viewer.open);
    mainui_media_close(&viewer);

    assert(remove(first) == 0 && remove(second) == 0 && remove(third) == 0);
    assert(remove(front) == 0 && remove(back) == 0);
    assert(remove(screenshot) == 0 && remove(video) == 0 && remove(manual) == 0);
    assert(remove(walkthrough) == 0);
    assert(remove(gamelist) == 0 && remove(rom) == 0);
    assert(rmdir(maps) == 0 && rmdir(front_dir) == 0 && rmdir(back_dir) == 0);
    assert(rmdir(shots) == 0 && rmdir(videos) == 0 && rmdir(manuals) == 0);
    assert(rmdir(media) == 0 && rmdir(root) == 0);
    SDL_Quit();
    return 0;
}