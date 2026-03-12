/*******************************************************************************
 * Size: 18 px
 * Bpp: 1
 * Opts: --bpp 1 --size 18 --font D:/Desktop/project-rawing/rawing_ui480320/assets/DSEG7Classic-Bold.ttf -o D:/Desktop/project-rawing/rawing_ui480320/assets\ui_font_Font18.c --format lvgl --symbols 0123456789 --no-compress --no-prefilter
 ******************************************************************************/

#include "../ui.h"

#ifndef UI_FONT_FONT18
#define UI_FONT_FONT18 1
#endif

#if UI_FONT_FONT18

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0030 "0" */
    0x7f, 0xd7, 0xf7, 0x1, 0xe0, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0x80, 0x30, 0x7, 0x1, 0xe0,
    0x3c, 0x7, 0x80, 0xf0, 0x1e, 0x3, 0xbf, 0xaf,
    0xf8,

    /* U+0031 "1" */
    0x7f, 0xfd, 0x7f, 0xfd,

    /* U+0032 "2" */
    0x7f, 0xc7, 0xf4, 0x1, 0x80, 0x30, 0x6, 0x0,
    0xc0, 0x18, 0x3, 0x3f, 0xb7, 0xf3, 0x0, 0x60,
    0xc, 0x1, 0x80, 0x30, 0x6, 0x0, 0xbf, 0x8f,
    0xf8,

    /* U+0033 "3" */
    0xff, 0x9f, 0xd0, 0xc, 0x3, 0x0, 0xc0, 0x30,
    0xc, 0x3, 0x7f, 0x5f, 0xd0, 0xc, 0x3, 0x0,
    0xc0, 0x30, 0xc, 0x3, 0x7f, 0x7f, 0xe0,

    /* U+0034 "4" */
    0x80, 0x38, 0xf, 0x1, 0xe0, 0x3c, 0x7, 0x80,
    0xf0, 0x1d, 0xfd, 0x3f, 0xa0, 0xc, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xc0, 0x18, 0x1,

    /* U+0035 "5" */
    0x7f, 0xd7, 0xf3, 0x0, 0x60, 0xc, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xbf, 0x87, 0xf4, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xc0, 0x18, 0x3, 0x3f, 0xaf,
    0xf8,

    /* U+0036 "6" */
    0x7f, 0xd7, 0xf3, 0x0, 0x60, 0xc, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xbf, 0x97, 0xf7, 0x1, 0xe0,
    0x3c, 0x7, 0x80, 0xf0, 0x1e, 0x3, 0xbf, 0xaf,
    0xf8,

    /* U+0037 "7" */
    0x7f, 0xd7, 0xf7, 0x1, 0xe0, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0x80, 0x20, 0x4, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xc0, 0x18, 0x3, 0x0, 0x20,

    /* U+0038 "8" */
    0x7f, 0xd7, 0xf7, 0x1, 0xe0, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0xbf, 0xb7, 0xf7, 0x1, 0xe0,
    0x3c, 0x7, 0x80, 0xf0, 0x1e, 0x3, 0xbf, 0xaf,
    0xf8,

    /* U+0039 "9" */
    0x7f, 0xd7, 0xf7, 0x1, 0xe0, 0x3c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0xbf, 0xa7, 0xf4, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xc0, 0x18, 0x3, 0x3f, 0xaf,
    0xf8
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 235, .box_w = 2, .box_h = 16, .ofs_x = 11, .ofs_y = 1},
    {.bitmap_index = 29, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 235, .box_w = 10, .box_h = 18, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 77, .adv_w = 235, .box_w = 11, .box_h = 16, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 99, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 124, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 149, .adv_w = 235, .box_w = 11, .box_h = 17, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 173, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 235, .box_w = 11, .box_h = 18, .ofs_x = 2, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 48, .range_length = 10, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_Font18 = {
#else
lv_font_t ui_font_Font18 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_FONT18*/

