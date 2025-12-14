#include <lilac/tty.h>
#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/signal.h>

//
// TTY line discipline
//

#define BUF_EMPTY(data) ((data)->input.rpos == (data)->input.wpos)
#define BUF_SPACE(data) (INPUT_BUF_SIZE - 1 - \
    (((data)->input.epos - (data)->input.rpos + INPUT_BUF_SIZE) % INPUT_BUF_SIZE))
#define EDIT_LEN(data)  ((data)->input.epos - (data)->line_start)
#define BUF_COUNT(data) (((data)->input.wpos - (data)->input.rpos + INPUT_BUF_SIZE) \
     % INPUT_BUF_SIZE)

static inline void tty_data_init(struct tty_data *data)
{
    memset(&data->input, 0, sizeof(data->input));
    data->line_start = 0;
    mutex_init(&data->read_lock);
}

static inline void nc_add_char(struct tty_data *data, char c)
{
    if (BUF_SPACE(data) > 0) {
        data->input.buf[data->input.epos % INPUT_BUF_SIZE] = c;
        data->input.epos++;
        data->input.wpos = data->input.epos; // Immediately readable
        data->vmin_cnt++;
    }
}

static inline void c_add_char(struct tty_data *data, char c)
{
    if (BUF_SPACE(data) > 0) {
        data->input.buf[data->input.epos % INPUT_BUF_SIZE] = c;
        data->input.epos++;
    }
}

static inline bool c_erase_char(struct tty_data *data)
{
    if (data->input.epos > data->line_start) {
        data->input.epos--;
        return true;
    }
    return false;
}

static inline void c_kill_line(struct tty_data *data)
{
    data->input.epos = data->line_start;
}

static inline void c_commit_line(struct tty_data *data)
{
    // Line is already in buffer from line_start to epos
    data->input.wpos = data->input.epos;
    data->line_start = data->input.epos;
    data->line_ready = true;
}

static inline int read_char(struct tty_data *data)
{
    if (BUF_EMPTY(data))
        return 0;

    char c = data->input.buf[data->input.rpos % INPUT_BUF_SIZE];
    data->input.rpos++;

    if (data->vmin_cnt > 0)
        data->vmin_cnt--;

    return c;
}

static inline bool has_readable_data(struct tty *tty)
{
    struct tty_data *data = tty->data;

    if (L_ICANON(tty)) {
        return data->line_ready && !BUF_EMPTY(data);
    } else {
        u8 vmin = tty->termios.c_cc[VMIN];
        return (vmin == 0) ? !BUF_EMPTY(data) : (data->vmin_cnt >= vmin);
    }
}

/* Helper function to check data availability (used by select/poll) */
bool tty_input_available(struct tty *tty)
{
    return has_readable_data(tty);
}

/* Echo helper functions */
static void echo_char(struct tty *tty, u8 c)
{
    if (c == '\n' && O_ONLCR(tty)) {
        tty->ops->write(tty, (const u8*)"\r\n", 2);
    } else {
        tty->ops->write(tty, &c, 1);
    }
}

static void echo_erase(struct tty *tty)
{
    if (L_ECHOE(tty)) {
        tty->ops->write(tty, (const u8*)"\b \b", 3);
    } else if (L_ECHO(tty)) {
        echo_char(tty, ERASE_CHAR(tty));
    }
}

static void echo_kill(struct tty *tty, int count)
{
    if (L_ECHOK(tty)) {
        if (L_ECHOKE(tty)) {
            for (int i = 0; i < count; i++)
                tty->ops->write(tty, (const u8*)"\b \b", 3);
        } else {
            echo_char(tty, '\n');
        }
    } else if (L_ECHO(tty)) {
        echo_char(tty, KILL_CHAR(tty));
    }
}

static void echo_erase_multi(struct tty *tty, int count)
{
    if (L_ECHOE(tty)) {
        for (int i = 0; i < count; i++) {
            tty->ops->write(tty, (const u8*)"\b \b", 3);
        }
    }
}

static void echo_reprint_line(struct tty *tty)
{
    struct tty_data *data = tty->data;

    if (L_ECHOPRT(tty)) {
        tty->ops->write(tty, (const u8*)"\n", 1);
        for (int i = data->line_start; i < data->input.epos; i++) {
            echo_char(tty, data->input.buf[i % INPUT_BUF_SIZE]);
        }
    }
}

/* Canonical editing helpers */
static int c_erase_word(struct tty_data *data)
{
    int erased = 0;
    int pos = data->input.epos;

    // Skip trailing whitespace
    while (pos > data->line_start) {
        char c = data->input.buf[(pos - 1) % INPUT_BUF_SIZE];
        if (c != ' ' && c != '\t') break;
        pos--;
        erased++;
    }

    // Erase word characters
    while (pos > data->line_start) {
        char c = data->input.buf[(pos - 1) % INPUT_BUF_SIZE];
        if (c == ' ' || c == '\t') break;
        pos--;
        erased++;
    }

    data->input.epos = pos;
    return erased;
}

static void flush_input(struct tty *tty)
{
    struct tty_data *data = tty->data;

    if (L_ICANON(tty)) {
        data->input.epos = data->line_start;
    } else {
        data->input.rpos = data->input.wpos = data->input.epos;
        data->vmin_cnt = 0;
    }
}


void default_tty_set_termios(struct tty *tty, const struct termios *old)
{
    struct tty_data *data = tty->data;

    bool was_canon = old ? (old->c_lflag & ICANON) : false;
    bool is_canon = L_ICANON(tty);

    if (is_canon && !was_canon) {
        // Move any buffered data to edit area
        while (data->input.rpos != data->input.wpos) {
            char c = data->input.buf[data->input.rpos++ % INPUT_BUF_SIZE];
            c_add_char(data, c);
        }
        data->line_start = data->input.wpos;
        data->input.epos = data->input.wpos;
        data->line_ready = false;
    } else if (!is_canon && was_canon) {
        if (EDIT_LEN(data) > 0)
            c_commit_line(data);
        data->input.epos = data->input.wpos;
        data->vmin_cnt = BUF_COUNT(data);
    }

    if (!is_canon) {
        // wake_all(&tty->read_wait);
        wake_first(&tty->read_wait);
    }

    klog(LOG_DEBUG, "termios changed: canon=%d echo=%d isig=%d\n",
         is_canon, L_ECHO(tty), L_ISIG(tty));
}

/* Input flag processing (c_iflag) */
static u8 input_char(struct tty *tty, u8 c)
{
    if (I_ISTRIP(tty))
        c &= 0x7f;

    if (c == 0 && I_IGNBRK(tty))
        return 0;

    if (c == '\n' && I_INLCR(tty))
        c = '\r';

    if (c == '\r' && I_IGNCR(tty))
        return 0;

    if (c == '\r' && I_ICRNL(tty))
        c = '\n';

    if (I_IUCLC(tty) && c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';

    return c;
}

/* Signal character handling */
static bool signal_char(struct tty *tty, u8 c)
{
    int sig = 0;
    const char *echo_str = NULL;

    if (c == INTR_CHAR(tty)) {
        sig = SIGINT;
        echo_str = "^C";
    } else if (c == QUIT_CHAR(tty)) {
        sig = SIGQUIT;
        echo_str = "^\\";
    } else if (c == SUSP_CHAR(tty)) {
        sig = SIGTSTP;
        echo_str = "^Z";
    } else {
        return false;
    }

    if (tty->ctrl.pgrp > 0) {
        klog(LOG_DEBUG, "signal_char: sending %d to pgrp %d\n", sig, tty->ctrl.pgrp);
        kill_pgrp(tty->ctrl.pgrp, sig);
    }

    if (L_ECHO(tty) && echo_str) {
        tty->ops->write(tty, (const u8*)echo_str, strlen(echo_str));
        if (L_ECHONL(tty))
            tty->ops->write(tty, (const u8*)"\n", 1);
    }

    if (sig == SIGINT || sig == SIGQUIT) {
        flush_input(tty);
        // TODO: flush output too
    }

    return true;
}

/* Flow control handling */
static bool flow_control(struct tty *tty, u8 c)
{
    if (!I_IXON(tty))
        return false;

    if (c == START_CHAR(tty)) {
        tty->ctrl.stopped = false;
        // TODO: wake output processes
        return true;
    } else if (c == STOP_CHAR(tty)) {
        tty->ctrl.stopped = true;
        return true;
    }

    if (I_IXANY(tty) && tty->ctrl.stopped) {
        tty->ctrl.stopped = false;
    }

    return false;
}

/* Canonical mode character processing */
static void handle_c_char(struct tty *tty, u8 c)
{
    struct tty_data *data = tty->data;
    int n;

    if (c == ERASE_CHAR(tty)) {
        if (c_erase_char(data))
            echo_erase(tty);
    } else if (c == KILL_CHAR(tty)) {
        n = EDIT_LEN(data);
        c_kill_line(data);
        echo_kill(tty, n);
    } else if (c == WERASE_CHAR(tty)) {
        n = c_erase_word(data);
        echo_erase_multi(tty, n);
    } else if (c == REPRINT_CHAR(tty)) {
        echo_reprint_line(tty);
    } else if (c == EOF_CHAR(tty)) {
        // commit current line and mark EOF
        if (EDIT_LEN(data) > 0) {
            c_commit_line(data);
            // wake_all(&tty->read_wait);
        } else {
            data->at_eof = true;
            // wake_all(&tty->read_wait);
        }
        wake_first(&tty->read_wait);
    } else if (c == '\n' || (L_ICANON(tty) && c == EOL_CHAR(tty))) {
        c_add_char(data, '\n');
        c_commit_line(data);

        if (L_ECHO(tty) || L_ECHONL(tty))
            echo_char(tty, '\n');

        // wake_all(&tty->read_wait);
        wake_first(&tty->read_wait);
    } else if (c >= 32 || c == '\t') {
        c_add_char(data, c);
        if (L_ECHO(tty))
            echo_char(tty, c);
    } else {
        // Other control character
        c_add_char(data, c);

        if (L_ECHO(tty)) {
            if (L_ECHOCTL(tty) && c != '\t') {
                // Echo as ^X
                char echo_buf[2] = {'^', c + 'A' - 1};
                if (c == 0x7f) {
                    echo_buf[1] = '?';  // DEL shows as ^?
                }
                tty->ops->write(tty, (u8*)echo_buf, 2);
            } else {
                // echo_char(tty, c);
            }
        }
    }
}

/* Non-canonical mode character processing */
static void handle_nc_char(struct tty *tty, u8 c)
{
    struct tty_data *data = tty->data;

    nc_add_char(data, c);

    if (L_ECHO(tty)) {
        if (L_ECHOCTL(tty) && c < 32 && c != '\t' && c != '\n') {
            char echo_buf[2] = {'^', c + 'A' - 1};
            tty->ops->write(tty, (u8*)echo_buf, 2);
        } else {
            echo_char(tty, c);
        }
    }

    u8 vmin = tty->termios.c_cc[VMIN];
    if (vmin == 0 || data->vmin_cnt >= vmin ||
        c == '\n' || c == EOF_CHAR(tty)) {
        // wake_all(&tty->read_wait);
        wake_first(&tty->read_wait);
    }
}


void default_tty_receive_buf(struct tty *tty, const u8 *cp, const u8 *fp, size_t count)
{
    struct tty_data *data = tty->data;

    mutex_lock(&data->read_lock);

    u8 c;
    while (count--) {
        c = *cp++;

        if (c == 0 && I_IGNPAR(tty))
            continue;

        c = input_char(tty, c);
        if (c == 0) continue; // Character was ignored/filtered

        if (L_ISIG(tty) && signal_char(tty, c))
            continue;

        if (flow_control(tty, c))
            continue;

        if (L_ICANON(tty))
            handle_c_char(tty, c);
        else
            handle_nc_char(tty, c);
    }

    mutex_unlock(&data->read_lock);
}

static ssize_t c_read(struct tty *tty, u8 *buf, size_t nr)
{
    struct tty_data *data = tty->data;
    size_t copied = 0;
    int c;

    while (copied < nr) {
        while (!data->line_ready && !data->at_eof && BUF_EMPTY(data)) {
            klog(LOG_DEBUG, "c_read: proc %d waiting for line\n", get_pid());
            mutex_unlock(&data->read_lock);
            if (sleep_on(&tty->read_wait) == -EINTR) {
                mutex_lock(&data->read_lock);
                return copied > 0 ? (ssize_t)copied : -EINTR;
            }
            mutex_lock(&data->read_lock);
        }

        // EOF
        if (data->at_eof && BUF_EMPTY(data)) {
            data->at_eof = false;
            break;
        }

        while (copied < nr && !BUF_EMPTY(data)) {
            c = read_char(data);
            if (c < 0) break;

            buf[copied++] = (u8)c;

            if (c == '\n')
                goto done;
        }

        if (BUF_EMPTY(data)) {
            data->line_ready = false;
        }
    }

done:
    return copied;
}

static ssize_t nc_read(struct tty *tty, u8 *buf, size_t nr)
{
    struct tty_data *data = tty->data;
    u8 vmin = tty->termios.c_cc[VMIN];
    u8 vtime = tty->termios.c_cc[VTIME];
    size_t copied = 0;
    int c;

#ifdef DEBUG_TTY
    klog(LOG_DEBUG, "nc_read: vmin=%d vtime=%d available=%d\n",
         vmin, vtime, data->vmin_cnt);
#endif

    if (vmin == 0 && vtime == 0) {
        // Case 1: Non-blocking read - return immediately with available data
        while (copied < nr && !BUF_EMPTY(data)) {
            c = read_char(data);
            if (c >= 0) {
                buf[copied++] = (u8)c;
            }
        }

    } else if (vmin > 0 && vtime == 0) {
        // Case 2: Block until VMIN characters available
        while (copied < vmin || (copied < nr && !BUF_EMPTY(data))) {
            while (BUF_EMPTY(data)) {
                // klog(LOG_DEBUG, "noncanon_read: waiting for vmin=%d (have %lu)\n", vmin, copied);
                mutex_unlock(&data->read_lock);
                if (sleep_on(&tty->read_wait) == -EINTR) {
                    mutex_lock(&data->read_lock);
                    return copied > 0 ? (ssize_t)copied : -EINTR;
                }
                mutex_lock(&data->read_lock);
            }

            c = read_char(data);
            if (c >= 0) {
                buf[copied++] = (u8)c;
            }

            if (copied >= vmin) {
                while (copied < nr && !BUF_EMPTY(data)) {
                    c = read_char(data);
                    if (c >= 0) {
                        buf[copied++] = (u8)c;
                    }
                }
                break;
            }
        }

    } else if (vmin == 0 && vtime > 0) {
        // Case 3: Return after VTIME timeout or when data available
        // TODO: Implement timeout logic - needs timer support
        // For now, behave like case 1
        while (copied < nr && !BUF_EMPTY(data)) {
            c = read_char(data);
            if (c >= 0) {
                buf[copied++] = (u8)c;
            }
        }

    } else {
        // Case 4: VMIN > 0, VTIME > 0 - inter-character timer
        // TODO: Complex timeout logic - needs timer support
        // For now, behave like case 2
        while (copied < vmin) {
            while (BUF_EMPTY(data)) {
                mutex_unlock(&data->read_lock);
                if (sleep_on(&tty->read_wait) == -EINTR) {
                    mutex_lock(&data->read_lock);
                    return copied > 0 ? (ssize_t)copied : -EINTR;
                }
                mutex_lock(&data->read_lock);
            }
            c = read_char(data);
            if (c >= 0) {
                buf[copied++] = (u8)c;
            }
        }
    }

#ifdef DEBUG_TTY
    for (size_t i = 0; i < copied; i++) {
        klog(LOG_DEBUG, "nc_read: read char 0x%x\n", buf[i]);
    }
#endif

    return copied;
}

/* Output processing for c_oflag */
static ssize_t process_output(struct tty *tty, const u8 *buf, size_t nr)
{
    // size_t written = 0;
    u8 output_buf[256];
    size_t out_pos = 0;

    for (size_t i = 0; i < nr; i++) {
        u8 c = buf[i];

        if (c == '\n' && O_ONLCR(tty)) {
            output_buf[out_pos++] = '\r';
            if (out_pos >= sizeof(output_buf)) {
                tty->ops->write(tty, output_buf, out_pos);
                // written += out_pos;
                out_pos = 0;
            }
            output_buf[out_pos++] = '\n';
        } else if (c == '\r' && O_OCRNL(tty)) {
            output_buf[out_pos++] = '\n';
        } else if (c == '\r' && O_ONOCR(tty) && tty->ctrl.column == 0) {
            // Don't output CR at beginning of line
            continue;
        } else {
            output_buf[out_pos++] = c;
        }

        if (c == '\n' || c == '\r') {
            tty->ctrl.column = 0;
        } else if (c == '\b' && tty->ctrl.column > 0) {
            tty->ctrl.column--;
        } else if (c >= 32) {
            tty->ctrl.column++;
        }

        if (out_pos >= sizeof(output_buf)) {
            ssize_t result = tty->ops->write(tty, output_buf, out_pos);
            if (result < 0) return result;
            // written += result;
            out_pos = 0;
        }
    }

    if (out_pos > 0) {
        ssize_t result = tty->ops->write(tty, output_buf, out_pos);
        if (result < 0) return result;
        // written += result;
    }

    return nr;
}



ssize_t default_tty_read(struct tty *tty, struct file *file, u8 *buf, size_t nr, unsigned long offset)
{
    struct tty_data *data = tty->data;

    mutex_lock(&data->read_lock);

    ssize_t ret;
    if (L_ICANON(tty)) {
        ret = c_read(tty, buf, nr);
    } else {
        ret = nc_read(tty, buf, nr);
    }

    mutex_unlock(&data->read_lock);
    return ret;
}


ssize_t default_tty_write(struct tty *tty, struct file *file, const u8 *buf, size_t nr)
{
    mutex_lock(&tty->write_lock);

    if (tty->ctrl.stopped) {
        // TODO: Should block until resumed
        klog(LOG_WARN, "tty_write: output is stopped by flow control\n");
        mutex_unlock(&tty->write_lock);
        return -EAGAIN;
    }

    ssize_t ret;
    if (!(tty->termios.c_oflag & (ONLCR|OCRNL|ONLRET|ONOCR))) {
        ret = tty->ops->write(tty, buf, nr);
    } else {
        ret = process_output(tty, buf, nr);
    }

    mutex_unlock(&tty->write_lock);
    return ret;
}



const struct tty_ldisc_ops default_tty_ldisc_ops = {
    .read = default_tty_read,
    .write = default_tty_write,
    .set_termios = default_tty_set_termios,
    .receive_buf = default_tty_receive_buf
};
