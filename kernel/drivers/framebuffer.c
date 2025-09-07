#include <drivers/framebuffer.h>
#include <lilac/lilac.h>
#include <lilac/font.h>
#include <lilac/kmm.h>
#include <utility/termius.h>

#define WINDOW_BORDER 16


void psf_init_font();

static struct font termius_font;
static struct framebuffer fb_data;
struct framebuffer *fb = &fb_data;

static void put_pixel(struct framebuffer *screen, u32 x, u32 y, u32 color) {
    u32 where = x * screen->fb_width + y * screen->fb_pitch;
    screen->fb[where] = color & 0xff;              // BLUE
    screen->fb[where + 1] = (color >> 8) & 0xff;   // GREEN
    screen->fb[where + 2] = (color >> 16) & 0xff;  // RED
}

#define PIXEL u32

void graphics_putc(u16 c, u32 cx, u32 cy)
{
    PSF_font *font = (PSF_font*)Lat2_Terminus16;
    int bytesperline = (font->width + 7) / 8;

    if (termius_font.unicode != NULL) {
        c = termius_font.unicode[c];
    }
    /* get the glyph for the character */
    const unsigned char *glyph = Lat2_Terminus16 + font->headersize +
        (c > 0 && c < font->numglyph ? c : 0) * font->bytesperglyph;
    /* upper left corner on screen where we want to display */
    u32 offs =
        (cy * font->height * fb->fb_pitch) +
        (cx * (font->width + 1) * sizeof(PIXEL));
    /* display pixels according to the bitmap */
    u32 x, y, line, mask;
    for(y = 0; y < font->height; y++) {
        /* save the starting position of the line */
        line = offs;
        mask = 1 << (font->width - 1);
        /* display a row */
        for(x = 0; x < font->width; x++) {
            *((PIXEL*)(fb->fb + line)) = *((unsigned int*)glyph) & mask ? fb->fb_fg : fb->fb_bg;
            /* adjust to the next pixel */
            mask >>= 1;
            line += sizeof(PIXEL);
        }
        /* adjust to the next line */
        glyph += bytesperline;
        offs += fb->fb_pitch;
    }
}

void print_charset(u32 start_x, u32 start_y)
{
    // print all characters
    for (u32 i = 0; i < termius_font.font->numglyph; i++) {
        graphics_putc(i, (start_x + i) % 80, (start_y + i) / 80);
    }
}

void graphics_scroll(void)
{
    u8 *dst, *src;
    u32 line_size = fb->fb_pitch * termius_font.font->height;

    for (dst = fb->fb, src = fb->fb + line_size;
            src < fb->fb + fb->fb_pitch * fb->fb_height;
            dst += line_size, src += line_size) {
        memcpy(dst, src, line_size);
    }
    memset(dst, 0, line_size);
}

struct framebuffer_color graphics_getcolor(void)
{
    return (struct framebuffer_color){fb->fb_fg, fb->fb_bg};
}

void graphics_clear(void)
{
    memset(fb->fb, 0, fb->fb_pitch * fb->fb_height);
}

void graphics_setcolor(u32 fg, u32 bg)
{
    fb->fb_fg = fg;
    fb->fb_bg = bg;
}

static void print_font_info(struct font *terminal_font);

void graphics_init(struct multiboot_tag_framebuffer *mfb)
{
    if (mfb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB
        || mfb->common.framebuffer_bpp != 32) {
        kerror("Unsupported framebuffer type\n");
    }

    fb->fb = (u8*)map_phys(
            (void*)(uintptr_t)mfb->common.framebuffer_addr,
            mfb->common.framebuffer_pitch * mfb->common.framebuffer_height,
            PG_WRITE|PG_STRONG_UC);
    fb->fb_width = mfb->common.framebuffer_width;
    fb->fb_height = mfb->common.framebuffer_height;
    fb->fb_pitch = mfb->common.framebuffer_pitch;
    fb->fb_fg = RGB_WHITE;
    fb->fb_bg = RGB_BLACK;
    fb->bypp = mfb->common.framebuffer_bpp ? mfb->common.framebuffer_bpp / 8 : 4;

    psf_init_font();
    graphics_clear();
    fb->font = &termius_font;

    kstatus(STATUS_OK, "Graphics mode terminal initialized\n");
}


/**********************************/

#define PSF1_FONT_MAGIC 0x0436

typedef struct {
    u16 magic; // Magic bytes for identification.
    u8 fontMode; // PSF font mode.
    u8 characterSize; // PSF character size.
} PSF1_Header;


void psf_init_font()
{
    termius_font.font = (PSF_font*)Lat2_Terminus16;
    termius_font.height = termius_font.font->height;
    termius_font.width = termius_font.font->width;
    // no unicode yet
}

static void print_font_info(struct font *terminal_font)
{
    printf("Font width: %d, height: %d\n", terminal_font->font->width, terminal_font->font->height);
    printf("Font has %d glyphs\n", terminal_font->font->numglyph);
    printf("Font bytes per glyph: %d\n", terminal_font->font->bytesperglyph);
    printf("Font header size: %d\n", terminal_font->font->headersize);
    printf("Font flags: %d\n", terminal_font->font->flags);
    printf("Font unicode table: %s\n", terminal_font->unicode ? "yes" : "no");

    // Print raw bitmap for all characters
    for (u32 i = 0; i < terminal_font->font->numglyph; i++) {
        printf("Glyph %d\n", i);
        for (u32 j = 0; j < terminal_font->font->bytesperglyph; j++) {
            unsigned char byte = Lat2_Terminus16[terminal_font->font->headersize + i * terminal_font->font->bytesperglyph + j];
            for (int k = 7; k >= 0; k--) {
                printf("%c", (byte & (1 << k)) ? '1' : '0');
            }
            printf("\n");
        }
        printf("\n");
    }
}
