#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <lilac/types.h>
#include <lilac/sync.h>

struct file;
struct tty;
struct vc_state;

// old basic output
struct console {
    spinlock_t lock;
    u32 cx, cy; //cursor
    u32 height, width;
    char *data;
    int first_row;
};

// TODO make param order consistent
struct con_display_ops {
    void    (*con_init)(struct vc_state *, int);
    void    (*con_deinit)(struct vc_state *);
    void    (*con_clear)(struct vc_state *, int y, int x, int h, int w);
    void    (*con_putc)(struct vc_state *, int c, int x, int y);
    void    (*con_putcs)(struct vc_state *, const unsigned short *, int, int, int);
    void    (*con_cursor)(struct vc_state *, int mode);
    void    (*con_scroll)(struct vc_state *, int top, int bot, int dir, int count);
    void    (*con_bmove)(struct vc_state *, int sy, int sx, int dy, int dx, int h, int w);
    int     (*con_switch)(struct vc_state *);
    int     (*con_blank)(struct vc_state *, int, int);
    void    (*con_color)(struct vc_state *, unsigned fg, unsigned bg);
};

#define CURSOR_ON 1
#define CURSOR_OFF 2

struct vc_state {
    const struct con_display_ops *con_ops;
    unsigned char *screen_buf; // raw fb
    void *display_data; // display driver data

    u8 esc_s;
    u8 vt_type;
    u32 attr;
    int curx, cury;
    int xs, ys;
    int vt_fg;                  /* Standard foreground color. */
    int vt_bg;                  /* Standard background color. */


    char *vt_trans[2];
    int vt_charset;             /* Character set. */
    int vt_bs;                  /* Code that backspace key sends. */
    int vt_nl_delay;

    int vt_echo         : 1;    /* Local echo on/off. */
    int vt_wrap         : 1;    /* Line wrap on/off */
    int vt_addlf        : 1;    /* Add linefeed on/off */
    int vt_addcr        : 1;    /* Add carriagereturn on/off */
    int vt_keypad       : 2;    /* Keypad mode. */
    int vt_cursor_mode  : 2;    /* cursor key mode. */
    int vt_cursor_on    : 1;    /* cursor on/off */
    int vt_asis         : 1;    /* 8bit clean mode. */
    int vt_insert       : 1;    /* Insert mode */
    int vt_crlf         : 1;    /* Return sends CR/LF */
    int vt_om           : 1;    /* Origin mode. */
    int vt_doscroll     : 1;

    int escparms[8];        /* Accumulated escape sequence. */
    int ptr;                /* Index into escparms array. */
    unsigned vt_tabs[5];    /* Tab stops for max. 32*5 = 160 columns. */

    short scroll_top;     /* Current size of scrolling region. */
    short scroll_bottom;

    /* Saved color and positions */
    int savex, savey, saveattr;
    int save_fg, save_bg;
    int savecharset;
    char *savetrans[2];
};

#define S_UP 1
#define S_DOWN 2

extern int write_to_screen;

void console_init(void);
ssize_t console_write(struct file *file, const void *buf, size_t count);

extern const struct con_display_ops fbcon_ops;

#endif
