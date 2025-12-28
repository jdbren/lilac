#include <drivers/keyboard.h>
#include <lilac/libc.h>
#include <lilac/tty.h>
#include <lilac/fs.h>
#include <lilac/device.h>
#include <lilac/err.h>

#define K_STOP      256
#define K_F1        257
#define K_F2        258
#define K_F3        259
#define K_F4        260
#define K_F5        261
#define K_F6        262
#define K_F7        263
#define K_F8        264
#define K_F9        265
#define K_F10       266
#define K_F11       277
#define K_F12       278

#define K_HOME      267
#define K_PGUP      268
#define K_UP        269
#define K_LT        270
#define K_RT        271
#define K_DN        272
#define K_END       273
#define K_PGDN      274
#define K_INS       275
#define K_DEL       276

struct kbd_state vt_kbd = {
    .mode = KB_STD,
    .reserved = 0
};

unsigned short keyboard_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    '\n', /* Enter key */
    0,    /* 29   - Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    /* Left shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,    /* Right shift */
    '*',
    0,  /* Alt */
    ' ',  /* Space bar */
    0,  /* Caps lock */
    K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    K_HOME,  /* Home key */
    K_UP,  /* Up Arrow */
    K_PGUP,  /* Page Up */
    '-',
    K_LT,  /* Left Arrow */
    0,
    K_RT,  /* Right Arrow */
    '+',
    K_END,  /* 79 - End key*/
    K_DN,  /* Down Arrow */
    K_PGDN,  /* Page Down */
    K_INS,  /* Insert Key */
    K_DEL,  /* Delete Key */
    0,   0,   0,
    K_F11,  /* F11 Key */
    K_F12,  /* F12 Key */
    0,  /* All other keys are undefined */
};

unsigned short keyboard_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    '\n', /* Enter key */
    0,    /* Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    /* Left shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,    /* Right shift */
    '*',
    0,  /* Alt */
    ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* F1 key ... */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* F10 */
    0,  /* Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
    '_', /* Some keyboards use a different symbol for shifted '-' */
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
    '+',
    0,  /* End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0   /* All other keys undefined */
};

static volatile u8 key_status_map[256];

#define SHIFT_PRESSED 0x2A
#define SHIFT_RELEASED 0xAA
#define CTRL_PRESSED 0x1D
#define CTRL_RELEASED 0x9D
#define ALT_PRESSED 0x38
#define ALT_RELEASED 0xB8
#define CAPS_LOCK 0x3A


/* Structure to hold escape sequences. */
struct escseq {
  int code;
  const char *vt100_st;
  const char *vt100_app;
  const char *ansi;
};

/* Escape sequences for different terminal types. */
static struct escseq vt_keys[] = {
  { K_F1,	"OP",	"OP",	"OP" },
  { K_F2,	"OQ",	"OQ",	"OQ" },
  { K_F3,	"OR",	"OR",	"OR" },
  { K_F4,	"OS",	"OS",	"OS" },
  { K_F5,	"[16~",	"[16~",	"OT" },
  { K_F6,	"[17~",	"[17~",	"OU" },
  { K_F7,	"[18~",	"[18~",	"OV" },
  { K_F8,	"[19~",	"[19~",	"OW" },
  { K_F9,	"[20~",	"[20~",	"OX" },
  { K_F10,	"[21~",	"[21~",	"OY" },
  { K_F11,	"[23~",	"[23~",	"OY" },
  { K_F12,	"[24~",	"[24~",	"OY" },
  { K_HOME,	"[1~",	"[1~",	"[H" },
  { K_PGUP,	"[5~",	"[5~",	"[V" },
  { K_UP,	"[A",	"OA",	"[A" },
  { K_LT,	"[D",	"OD",	"[D" },
  { K_RT,	"[C",	"OC",	"[C" },
  { K_DN,	"[B",	"OB",	"[B" },
  { K_END,	"[4~",	"[4~",	"[Y" },
  { K_PGDN,	"[6~",	"[6~",	"[U" },
  { K_INS,	"[2~",	"[2~",	"[@" },
  { K_DEL,	"[3~",	"[3~",	"\177" },
  { 0,		NULL,	NULL,	NULL }
};


static void kbd_send(int keycode)
{
    char esc_buf[8];
    int f;
    int len;
    if (keycode < 0x100) {
        tty_recv_char(keycode);
    } else {
        /* Look up code in translation table. */
        for (f = 0; vt_keys[f].code; f++)
            if (vt_keys[f].code == keycode)
                break;
        if (vt_keys[f].code == 0)
            return;

        /* Now send appropriate escape code. */
        len = snprintf(esc_buf, 8, "\033%s", vt_keys[f].vt100_st);
        tty_recv_buf(esc_buf, len);
    }
}

static int scancode_queue[32];
static int scancode_queue_start = 0;
static int scancode_queue_end = 0;

static void scancode_add(int scancode)
{
    scancode_queue[scancode_queue_end] = scancode;
    scancode_queue_end = (scancode_queue_end + 1) % 32;
}

static int scancode_pop()
{
    if (scancode_queue_start == scancode_queue_end)
        return -1; // empty
    int scancode = scancode_queue[scancode_queue_start];
    scancode_queue_start = (scancode_queue_start + 1) % 32;
    return scancode;
}

void kb_code(int scancode)
{
    int c = 0;

    if (vt_kbd.mode == KB_RAW) {
        scancode_add(scancode);
        return;
    }

    switch (scancode) {
        case SHIFT_PRESSED:
            key_status_map[SHIFT_PRESSED] = 1;
            break;
        case SHIFT_RELEASED:
            key_status_map[SHIFT_PRESSED] = 0;
            break;
        case CTRL_PRESSED:
            key_status_map[CTRL_PRESSED] = 1;
            break;
        case CTRL_RELEASED:
            key_status_map[CTRL_PRESSED] = 0;
            break;
        case ALT_PRESSED:
            key_status_map[ALT_PRESSED] = 1;
            break;
        case ALT_RELEASED:
            key_status_map[ALT_PRESSED] = 0;
            break;
        case CAPS_LOCK:
            key_status_map[CAPS_LOCK] = !key_status_map[CAPS_LOCK];
            break;
        default:
        if (scancode < (int)ARRAY_SIZE(keyboard_map) && keyboard_map[scancode]) {
            if (key_status_map[SHIFT_PRESSED] || key_status_map[CAPS_LOCK])
                c = keyboard_map_shift[scancode];
            else
                c = keyboard_map[scancode];

            if (key_status_map[CTRL_PRESSED])
                c = CTRL(c);

            if (key_status_map[ALT_PRESSED])
                c = ALT(c);
        }
    }

    if (c) {
        // klog(LOG_DEBUG, "Keycode: %x\n", c);
        kbd_send(c);
    }
}

int kb_set_mode(int mode)
{
    if (mode < KB_RAW || mode > KB_STD)
        return -EINVAL;
    vt_kbd.mode = mode;
    return 0;
}

ssize_t kb_read_raw(struct file *f, void *buf, size_t size)
{
    size_t bytes_read = 0;
    u8 *cbuf = buf;
    while (bytes_read < size) {
        int scancode = scancode_pop();
        if (scancode == -1)
            break; // no more scancodes
        cbuf[bytes_read++] = (u8)scancode;
    }
    return bytes_read;
}

int kb_ioctl(struct file *f, int op, void *argp)
{
    switch (op) {
    case KBDGMODE:
        return vt_kbd.mode;
    case KBDSMODE:
        return kb_set_mode((int)(uintptr_t)argp);
    default:
        return -EINVAL;
    }
}

static const struct file_operations keyboard_fops = {
    .read = kb_read_raw,
    .ioctl = kb_ioctl
};

int kb_open(struct inode *inode, struct file *file)
{
    file->f_op = &keyboard_fops;
    return 0;
}

static const struct inode_operations keyboard_iops = {
    .open = kb_open,
};

void kbd_init(void)
{
    kbd_int_init();
    dev_create("/dev/kbd0", &keyboard_fops, &keyboard_iops, S_IFCHR|S_IREAD, INPUT_DEVICE);
}
