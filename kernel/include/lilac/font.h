#ifndef _LILAC_FONT_H
#define _LILAC_FONT_H

#include <lilac/types.h>

#define PSF_FONT_MAGIC 0x864ab572

typedef struct {
    u32 magic;         /* magic bytes to identify PSF */
    u32 version;       /* zero */
    u32 headersize;    /* offset of bitmaps in file, 32 */
    u32 flags;         /* 0 if there's no unicode table */
    u32 numglyph;      /* number of glyphs */
    u32 bytesperglyph; /* size of each glyph */
    u32 height;        /* height in pixels */
    u32 width;         /* width in pixels */
} PSF_font;

struct font {
    PSF_font *font;
    u16 *unicode;
};

#endif
