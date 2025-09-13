#include <lilac/tty.h>
#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/signal.h>

//
// TTY line discipline
//

void default_tty_receive_buf(struct tty *tty, const u8 *cp, const u8 *fp, size_t count)
{
    unsigned char c;
    struct tty_data *data = tty->data;

    while (count--) {
        c = *cp++;
        if (c == INTR_CHAR(tty)) {
            kill_pgrp(tty->ctrl.pgrp, SIGINT);
            tty->ops->write(tty, (u8*)"^C", 2);
        } else if (c == EOF_CHAR(tty)) {
            tty->ops->write(tty, (u8*)"^D", 2);
        } else if (c == QUIT_CHAR(tty)) {
            kill_pgrp(tty->ctrl.pgrp, SIGQUIT);
            tty->ops->write(tty, (u8*)"^\\", 2);
        } else {
            switch(c) {
            case '\b': // Backspace
            // case '\x7f': // Delete key
                if (data->input.epos > data->input.wpos) {
                    data->input.epos--;
                    tty->ops->write(tty, (u8*)"\b \b", 3);
                }
            break;
            default:
                if (c != 0 && data->input.epos - data->input.rpos < INPUT_BUF_SIZE) {
                    c = (c == '\r') ? '\n' : c;

                    data->input.buf[data->input.epos++ % INPUT_BUF_SIZE] = c;

                    // echo
                    if (L_ECHO(tty)) {
                        if (c == '\x1b')
                            tty->ops->write(tty, (u8*)"^[", 2);
                        else
                            tty->ops->write(tty, &c, 1);
                    }

                    if (c == '\n' || data->input.epos - data->input.rpos == INPUT_BUF_SIZE) {
                        data->input.wpos = data->input.epos;
                        wake_first(&tty->read_wait);
                    }
                }
            }
        }
    }
}

void default_tty_set_termios(struct tty *tty, const struct ktermios *old)
{

}

ssize_t default_tty_read(struct tty *tty, struct file *file, u8 *buf, size_t nr, unsigned long offset)
{
    struct tty_data *data = tty->data;
    u32 target;
    int c;
    char cbuf;

    target = nr;
    while(nr > 0) {
        if (data->input.rpos == data->input.wpos) {
            klog(LOG_DEBUG, "console_read: proc %d waiting for input\n", get_pid());
            sleep_on(&tty->read_wait);
            if (data->input.rpos == data->input.wpos) // interrupted
                return -EINTR;
        }

        c = data->input.buf[data->input.rpos++ % INPUT_BUF_SIZE];

        // if(c == C('D')){  // end-of-file
        //     if(count < target){
        //         // Save ^D for next time, to make sure
        //         // caller gets a 0-byte result.
        //         con.rpos--;
        //     }
        //     break;
        // }

        // copy the input byte to the buffer.
        cbuf = c;
        memcpy(buf, &cbuf, 1);

        buf++;
        --nr;

        if(c == '\n')
            break;
    }

    return target - nr;
}

ssize_t default_tty_write(struct tty *tty, struct file *file, const u8 *buf, size_t nr)
{
    return tty->ops->write(tty, buf, nr);
}


const struct tty_ldisc_ops default_tty_ldisc_ops = {
    .read = default_tty_read,
    .write = default_tty_write,
    .set_termios = default_tty_set_termios,
    .receive_buf = default_tty_receive_buf
};
