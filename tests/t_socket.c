// AF_UNIX sockets: bind, datagram transfer, stream listen/connect/accept, close.
#include "ktest.h"
#include <sys/socket.h>
#include <sys/un.h>

/* Unique socket path in a fresh directory, so tests never collide. */
static void tmp_addr(struct sockaddr_un *a)
{
    char *dir = ktest_mkdtemp("/tmp");
    ASSERT_PTR(dir);
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    snprintf(a->sun_path, sizeof(a->sun_path), "%s/s", dir);
    free(dir);
}

#define SA(a) ((struct sockaddr *)(a))

static int bound_sock(int type, const struct sockaddr_un *a)
{
    int fd = ASSERT_OK(socket(AF_UNIX, type, 0));
    ASSERT_OK(bind(fd, SA(a), sizeof(*a)));
    return fd;
}

static int listener(const struct sockaddr_un *a, int backlog)
{
    int fd = bound_sock(SOCK_STREAM, a);
    ASSERT_OK(listen(fd, backlog));
    return fd;
}

static int connected(int type, const struct sockaddr_un *a)
{
    int fd = ASSERT_OK(socket(AF_UNIX, type, 0));
    ASSERT_OK(connect(fd, SA(a), sizeof(*a)));
    return fd;
}

static void on_alarm(int sig)
{
    (void)sig;
}

/* SIGALRM in secs, without SA_RESTART so blocking calls fail with EINTR */
static void arm_interrupt(int secs)
{
    struct sigaction sa = { .sa_handler = on_alarm };
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGALRM, &sa, NULL));
    alarm(secs);
}

/* ---- common ---- */

TEST(create_and_close)
{
    int s = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    int d = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    struct stat st;
    EXPECT_OK(fstat(s, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    EXPECT_OK(fstat(d, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    EXPECT_OK(close(s));
    EXPECT_OK(close(d));
}

TEST(bad_type)
{
    EXPECT_ERR(socket(AF_UNIX, 99, 0), EINVAL);
}

TEST(not_a_socket)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int p[2];
    ASSERT_OK(pipe(p));
    /* repeated: each call used to leak a reference on the pipe file */
    for (int i = 0; i < 200; i++) {
        EXPECT_ERR(bind(p[0], SA(&a), sizeof(a)), ENOTSOCK);
        EXPECT_ERR(connect(p[0], SA(&a), sizeof(a)), ENOTSOCK);
        EXPECT_ERR(listen(p[0], 1), ENOTSOCK);
    }
    close(p[1]);
    char c;
    EXPECT_EQ(read(p[0], &c, 1), 0); /* writer gone: EOF, not a hang */
    close(p[0]);
}

TEST(many_sockets)
{
    for (int i = 0; i < 500; i++) {
        int fd = ASSERT_OK(socket(AF_UNIX, i & 1 ? SOCK_DGRAM : SOCK_STREAM, 0));
        ASSERT_OK(close(fd));
    }
}

TEST(bind_creates_node)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int fd = bound_sock(SOCK_DGRAM, &a);
    struct stat st;
    ASSERT_OK(stat(a.sun_path, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    close(fd);
    /* the node outlives the socket */
    EXPECT_OK(stat(a.sun_path, &st));
}

TEST(bind_path_in_use)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int fd = bound_sock(SOCK_DGRAM, &a);
    int fd2 = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(bind(fd2, SA(&a), sizeof(a)), EADDRINUSE);
    close(fd);
    /* still in use after close until unlinked */
    EXPECT_ERR(bind(fd2, SA(&a), sizeof(a)), EADDRINUSE);
    ASSERT_OK(unlink(a.sun_path));
    EXPECT_OK(bind(fd2, SA(&a), sizeof(a)));
    close(fd2);
}

TEST(bind_twice)
{
    struct sockaddr_un a, b;
    tmp_addr(&a);
    tmp_addr(&b);
    int fd = bound_sock(SOCK_DGRAM, &a);
    EXPECT_ERR(bind(fd, SA(&b), sizeof(b)), EINVAL);
    close(fd);
}

/* ---- datagram ---- */

TEST(dgram_roundtrip)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = connected(SOCK_DGRAM, &a);

    const char *msgs[] = { "hello", "x", "third message" };
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(send(tx, msgs[i], strlen(msgs[i]), 0), (long)strlen(msgs[i]));

    /* boundaries preserved, in order */
    for (int i = 0; i < 3; i++) {
        char buf[64] = {0};
        EXPECT_EQ(recv(rx, buf, sizeof(buf), 0), (long)strlen(msgs[i]));
        EXPECT_STREQ(buf, msgs[i]);
    }
    close(tx);
    close(rx);
}

TEST(dgram_truncates)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = connected(SOCK_DGRAM, &a);

    send(tx, "0123456789", 10, 0);
    send(tx, "ab", 2, 0);
    char buf[8] = {0};
    EXPECT_EQ(recv(rx, buf, 4, 0), 4);
    EXPECT_EQ(memcmp(buf, "0123", 4), 0);
    /* the rest of the first datagram is discarded */
    EXPECT_EQ(recv(rx, buf, sizeof(buf), 0), 2);
    EXPECT_EQ(memcmp(buf, "ab", 2), 0);
    close(tx);
    close(rx);
}

TEST(dgram_send_unconnected)
{
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(send(fd, "x", 1, 0), ENOTCONN);
    close(fd);
}

TEST(dgram_connect_regular_file)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int f = ASSERT_OK(open(a.sun_path, O_CREAT | O_WRONLY, 0644));
    close(f);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(connect(fd, SA(&a), sizeof(a)), ECONNREFUSED);
    close(fd);
}

TEST(dgram_connect_stream)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 1);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(connect(fd, SA(&a), sizeof(a)), EPROTOTYPE);
    close(fd);
    close(l);
}

TEST(dgram_peer_closed)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = connected(SOCK_DGRAM, &a);
    /* unread messages are freed with the receiver */
    for (int i = 0; i < 4; i++)
        EXPECT_EQ(send(tx, "queued", 6, 0), 6);
    close(rx);
    EXPECT_ERR(send(tx, "x", 1, 0), ECONNREFUSED);
    close(tx);
}

TEST(dgram_reconnect)
{
    struct sockaddr_un a, b;
    tmp_addr(&a);
    tmp_addr(&b);
    int ra = bound_sock(SOCK_DGRAM, &a);
    int rb = bound_sock(SOCK_DGRAM, &b);
    int tx = connected(SOCK_DGRAM, &a);
    ASSERT_OK(connect(tx, SA(&b), sizeof(b)));
    EXPECT_EQ(send(tx, "b", 1, 0), 1);
    char c = 0;
    EXPECT_EQ(recv(rb, &c, 1, 0), 1);
    EXPECT_EQ(c, 'b');
    /* the old peer dies cleanly once nothing points at it */
    close(ra);
    close(tx);
    close(rb);
}

TEST(dgram_recv_blocks)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        int tx = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (tx < 0 || connect(tx, SA(&a), sizeof(a)) < 0)
            _exit(1);
        _exit(send(tx, "late", 4, 0) == 4 ? 0 : 2);
    }

    char buf[8] = {0};
    long long t0 = ktest_now_ns();
    EXPECT_EQ(recv(rx, buf, sizeof(buf), 0), 4);
    EXPECT_GE(ktest_now_ns() - t0, 50000000LL);
    EXPECT_EQ(memcmp(buf, "late", 4), 0);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    close(rx);
}

TEST(dgram_recv_eintr)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);
    arm_interrupt(1);
    char c;
    EXPECT_ERR(recv(rx, &c, 1, 0), EINTR);
    close(rx);
}

TEST(dgram_sendto_recvfrom)
{
    struct sockaddr_un a, b;
    tmp_addr(&a);
    tmp_addr(&b);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = bound_sock(SOCK_DGRAM, &b);

    /* no connect needed */
    EXPECT_EQ(sendto(tx, "hi", 2, 0, SA(&a), sizeof(a)), 2);

    struct sockaddr_un from;
    memset(&from, 0, sizeof(from));
    socklen_t fromlen = sizeof(from);
    char buf[8] = {0};
    EXPECT_EQ(recvfrom(rx, buf, sizeof(buf), 0, SA(&from), &fromlen), 2);
    EXPECT_EQ(memcmp(buf, "hi", 2), 0);
    EXPECT_EQ(from.sun_family, AF_UNIX);
    EXPECT_STREQ(from.sun_path, b.sun_path);
    EXPECT_EQ(fromlen, (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                                   + strlen(b.sun_path) + 1));

    /* reply to the reported address */
    EXPECT_EQ(sendto(rx, "ok", 2, 0, SA(&from), fromlen), 2);
    EXPECT_EQ(recv(tx, buf, sizeof(buf), 0), 2);
    EXPECT_EQ(memcmp(buf, "ok", 2), 0);
    close(tx);
    close(rx);
}

TEST(dgram_recvfrom_unbound_sender)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_EQ(sendto(tx, "x", 1, 0, SA(&a), sizeof(a)), 1);

    struct sockaddr_un from;
    memset(&from, 0xff, sizeof(from));
    socklen_t fromlen = sizeof(from);
    char c;
    EXPECT_EQ(recvfrom(rx, &c, 1, 0, SA(&from), &fromlen), 1);
    EXPECT_EQ(fromlen, (socklen_t)sizeof(sa_family_t));
    EXPECT_EQ(from.sun_family, AF_UNIX);
    close(tx);
    close(rx);
}

TEST(dgram_recvfrom_short_addrlen)
{
    struct sockaddr_un a, b;
    tmp_addr(&a);
    tmp_addr(&b);
    int rx = bound_sock(SOCK_DGRAM, &a);
    int tx = bound_sock(SOCK_DGRAM, &b);
    EXPECT_EQ(sendto(tx, "x", 1, 0, SA(&a), sizeof(a)), 1);

    char raw[sizeof(struct sockaddr_un)];
    memset(raw, 0x5a, sizeof(raw));
    socklen_t fromlen = 4;
    char c;
    EXPECT_EQ(recvfrom(rx, &c, 1, 0, SA(raw), &fromlen), 1);
    /* full length reported, only 4 bytes written */
    EXPECT_GT(fromlen, (socklen_t)4);
    EXPECT_EQ(raw[4], 0x5a);
    close(tx);
    close(rx);
}

TEST(dgram_sendto_connected_elsewhere)
{
    struct sockaddr_un a, b;
    tmp_addr(&a);
    tmp_addr(&b);
    int ra = bound_sock(SOCK_DGRAM, &a);
    int rb = bound_sock(SOCK_DGRAM, &b);
    /* a only accepts from b once connected to it */
    ASSERT_OK(connect(ra, SA(&b), sizeof(b)));
    int tx = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(sendto(tx, "x", 1, 0, SA(&a), sizeof(a)), EPERM);
    EXPECT_EQ(sendto(rb, "y", 1, 0, SA(&a), sizeof(a)), 1);
    char c = 0;
    EXPECT_EQ(recv(ra, &c, 1, 0), 1);
    EXPECT_EQ(c, 'y');
    close(tx);
    close(ra);
    close(rb);
}

TEST(dgram_sendto_errors)
{
    struct sockaddr_un a, missing;
    tmp_addr(&a);
    tmp_addr(&missing);
    int l = listener(&a, 1);
    int tx = ASSERT_OK(socket(AF_UNIX, SOCK_DGRAM, 0));
    EXPECT_ERR(sendto(tx, "x", 1, 0, SA(&a), sizeof(a)), EPROTOTYPE);
    EXPECT_ERR(sendto(tx, "x", 1, 0, SA(&missing), sizeof(missing)), ENOENT);
    close(tx);
    close(l);
}

/* ---- stream: setup errors ---- */

TEST(listen_unbound)
{
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    EXPECT_ERR(listen(fd, 1), EINVAL);
    close(fd);
}

TEST(listen_dgram)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int fd = bound_sock(SOCK_DGRAM, &a);
    EXPECT_ERR(listen(fd, 1), EOPNOTSUPP);
    close(fd);
}

TEST(accept_not_listening)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int fd = bound_sock(SOCK_STREAM, &a);
    EXPECT_ERR(accept(fd, NULL, NULL), EINVAL);
    close(fd);
}

TEST(connect_not_listening)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int srv = bound_sock(SOCK_STREAM, &a);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    EXPECT_ERR(connect(fd, SA(&a), sizeof(a)), ECONNREFUSED);
    close(fd);
    close(srv);
}

TEST(connect_dgram_target)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int d = bound_sock(SOCK_DGRAM, &a);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    EXPECT_ERR(connect(fd, SA(&a), sizeof(a)), EPROTOTYPE);
    close(fd);
    close(d);
}

TEST(connect_self_listener)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 1);
    EXPECT_ERR(connect(l, SA(&a), sizeof(a)), EINVAL);
    close(l);
}

TEST(connect_after_listener_closed)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 1);
    close(l);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    EXPECT_ERR(connect(fd, SA(&a), sizeof(a)), ECONNREFUSED);
    close(fd);
}

/* ---- stream: connect/accept ---- */

TEST(connect_accept)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    int s = ASSERT_OK(accept(l, NULL, NULL));
    EXPECT_NE(s, l);
    EXPECT_NE(s, c);
    struct stat st;
    EXPECT_OK(fstat(s, &st));
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    close(s);
    close(c);
    close(l);
}

TEST(connect_twice)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    EXPECT_ERR(connect(c, SA(&a), sizeof(a)), EISCONN);
    close(c);
    close(l);
}

TEST(accept_fifo_order)
{
    struct sockaddr_un a, ca, cb;
    tmp_addr(&a);
    tmp_addr(&ca);
    tmp_addr(&cb);
    int l = listener(&a, 4);
    int c1 = bound_sock(SOCK_STREAM, &ca);
    int c2 = bound_sock(SOCK_STREAM, &cb);
    ASSERT_OK(connect(c1, SA(&a), sizeof(a)));
    ASSERT_OK(connect(c2, SA(&a), sizeof(a)));

    struct sockaddr_un peer;
    socklen_t len = sizeof(peer);
    int s1 = ASSERT_OK(accept(l, SA(&peer), &len));
    EXPECT_STREQ(peer.sun_path, ca.sun_path);
    len = sizeof(peer);
    int s2 = ASSERT_OK(accept(l, SA(&peer), &len));
    EXPECT_STREQ(peer.sun_path, cb.sun_path);
    close(s1);
    close(s2);
    close(c1);
    close(c2);
    close(l);
}

TEST(accept_addr_unnamed_peer)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    struct sockaddr_un peer;
    memset(&peer, 0x7f, sizeof(peer));
    socklen_t len = sizeof(peer);
    int s = ASSERT_OK(accept(l, SA(&peer), &len));
    EXPECT_EQ(len, sizeof(sa_family_t));
    EXPECT_EQ(peer.sun_family, AF_UNIX);
    close(s);
    close(c);
    close(l);
}

TEST(accept_addr_named_peer)
{
    struct sockaddr_un a, ca;
    tmp_addr(&a);
    tmp_addr(&ca);
    int l = listener(&a, 4);
    int c = bound_sock(SOCK_STREAM, &ca);
    ASSERT_OK(connect(c, SA(&a), sizeof(a)));
    struct sockaddr_un peer = {0};
    socklen_t len = sizeof(peer);
    int s = ASSERT_OK(accept(l, SA(&peer), &len));
    EXPECT_EQ(len, offsetof(struct sockaddr_un, sun_path) + strlen(ca.sun_path) + 1);
    EXPECT_STREQ(peer.sun_path, ca.sun_path);
    close(s);
    close(c);
    close(l);
}

TEST(connect_accept_cross_process)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(l);
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        if (c < 0 || connect(c, SA(&a), sizeof(a)) < 0)
            _exit(1);
        close(c);
        _exit(0);
    }

    int s = ASSERT_OK(accept(l, NULL, NULL));
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    close(s);
    close(l);
}

TEST(accept_blocks)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(l);
        usleep(100000);
        int c = socket(AF_UNIX, SOCK_STREAM, 0);
        _exit(c >= 0 && connect(c, SA(&a), sizeof(a)) == 0 ? 0 : 1);
    }

    long long t0 = ktest_now_ns();
    int s = ASSERT_OK(accept(l, NULL, NULL));
    EXPECT_GE(ktest_now_ns() - t0, 50000000LL);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    close(s);
    close(l);
}

TEST(accept_eintr)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    arm_interrupt(1);
    EXPECT_ERR(accept(l, NULL, NULL), EINTR);
    close(l);
}

/* ---- stream: backlog ---- */

TEST(backlog_full_blocks_connect)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 0); /* one pending connection fits */
    int c1 = connected(SOCK_STREAM, &a);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        int c2 = socket(AF_UNIX, SOCK_STREAM, 0);
        _exit(c2 >= 0 && connect(c2, SA(&a), sizeof(a)) == 0 ? 0 : 1);
    }

    usleep(200000);
    EXPECT_EQ(waitpid(pid, NULL, WNOHANG), 0); /* still blocked */
    int s1 = ASSERT_OK(accept(l, NULL, NULL)); /* frees a slot */
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    int s2 = ASSERT_OK(accept(l, NULL, NULL));
    close(s1);
    close(s2);
    close(c1);
    close(l);
}

TEST(backlog_connect_eintr)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 0);
    int c1 = connected(SOCK_STREAM, &a);
    int c2 = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    arm_interrupt(1);
    EXPECT_ERR(connect(c2, SA(&a), sizeof(a)), EINTR);
    close(c2);
    close(c1);
    close(l);
}

TEST(listener_close_wakes_connector)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 0);
    int c1 = connected(SOCK_STREAM, &a);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(l); /* only the parent's copy keeps the listener open */
        int c2 = socket(AF_UNIX, SOCK_STREAM, 0);
        if (c2 < 0)
            _exit(1);
        if (connect(c2, SA(&a), sizeof(a)) == 0)
            _exit(2);
        _exit(errno == ECONNREFUSED ? 0 : 3);
    }

    usleep(200000);
    EXPECT_EQ(waitpid(pid, NULL, WNOHANG), 0);
    close(l);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st));
    EXPECT_EQ(WEXITSTATUS(st), 0);
    close(c1);
}

/* ---- stream: close ordering ---- */

TEST(close_listener_with_pending)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 8);
    int c[4];
    for (int i = 0; i < 4; i++)
        c[i] = connected(SOCK_STREAM, &a);
    close(l);
    for (int i = 0; i < 4; i++)
        EXPECT_OK(close(c[i]));
}

TEST(close_clients_before_listener)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 8);
    for (int i = 0; i < 4; i++)
        close(connected(SOCK_STREAM, &a));
    /* the dead clients' connections are still queued */
    for (int i = 0; i < 4; i++) {
        int s = ASSERT_OK(accept(l, NULL, NULL));
        EXPECT_OK(close(s));
    }
    close(l);
}

TEST(close_accepted_first)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    int s = ASSERT_OK(accept(l, NULL, NULL));
    EXPECT_OK(close(s));
    EXPECT_OK(close(l));
    EXPECT_OK(close(c));
}

TEST(close_client_first)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    int s = ASSERT_OK(accept(l, NULL, NULL));
    EXPECT_OK(close(c));
    EXPECT_OK(close(l));
    EXPECT_OK(close(s));
}

TEST(close_shared_by_fork)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(c);
        close(l);
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    /* the child's close dropped only its file references */
    EXPECT_ERR(connect(c, SA(&a), sizeof(a)), EISCONN);
    int s = ASSERT_OK(accept(l, NULL, NULL));
    close(s);
    close(c);
    close(l);
}

TEST_TIMEOUT(connect_close_stress, 30)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 8);
    for (int i = 0; i < 200; i++) {
        int c = connected(SOCK_STREAM, &a);
        int s = ASSERT_OK(accept(l, NULL, NULL));
        if (i & 1) {
            close(c);
            close(s);
        } else {
            close(s);
            close(c);
        }
    }
    close(l);
}

/* ---- stream: data transfer ---- */

/* Connected pair: *c is the client, *s the accepted end. */
static void stream_pair(int *c, int *s)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    *c = connected(SOCK_STREAM, &a);
    *s = ASSERT_OK(accept(l, NULL, NULL));
    close(l);
}

TEST(stream_roundtrip)
{
    int c, s;
    stream_pair(&c, &s);
    char buf[16] = {0};
    EXPECT_EQ(send(c, "ping", 4, 0), 4);
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 4);
    EXPECT_EQ(memcmp(buf, "ping", 4), 0);
    EXPECT_EQ(send(s, "pong!", 5, 0), 5);
    EXPECT_EQ(recv(c, buf, sizeof(buf), 0), 5);
    EXPECT_EQ(memcmp(buf, "pong!", 5), 0);
    close(c);
    close(s);
}

TEST(stream_send_before_accept)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int l = listener(&a, 4);
    int c = connected(SOCK_STREAM, &a);
    EXPECT_EQ(send(c, "early", 5, 0), 5);
    int s = ASSERT_OK(accept(l, NULL, NULL));
    char buf[8] = {0};
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 5);
    EXPECT_EQ(memcmp(buf, "early", 5), 0);
    close(c);
    close(s);
    close(l);
}

TEST(stream_no_boundaries)
{
    int c, s;
    stream_pair(&c, &s);
    send(c, "abc", 3, 0);
    send(c, "def", 3, 0);
    char buf[16] = {0};
    /* both sends arrive as one run of bytes */
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 6);
    EXPECT_EQ(memcmp(buf, "abcdef", 6), 0);
    close(c);
    close(s);
}

TEST(stream_partial_recv)
{
    int c, s;
    stream_pair(&c, &s);
    send(c, "0123456789", 10, 0);
    char buf[16] = {0};
    EXPECT_EQ(recv(s, buf, 4, 0), 4);
    EXPECT_EQ(memcmp(buf, "0123", 4), 0);
    /* unlike dgram, the rest is kept */
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 6);
    EXPECT_EQ(memcmp(buf, "456789", 6), 0);
    close(c);
    close(s);
}

TEST(stream_not_connected)
{
    struct sockaddr_un a;
    tmp_addr(&a);
    int fd = ASSERT_OK(socket(AF_UNIX, SOCK_STREAM, 0));
    char c;
    EXPECT_ERR(send(fd, "x", 1, 0), ENOTCONN);
    EXPECT_ERR(recv(fd, &c, 1, 0), ENOTCONN);
    int l = listener(&a, 1);
    EXPECT_ERR(send(l, "x", 1, 0), ENOTCONN);
    EXPECT_ERR(recv(l, &c, 1, 0), ENOTCONN);
    close(l);
    close(fd);
}

#define BIG_LEN (256 * 1024)

static unsigned char pattern(size_t i)
{
    return (unsigned char)(i * 7 + i / 251);
}

/* larger than the ring: the writer must block and resume */
TEST_TIMEOUT(stream_large_transfer, 30)
{
    int c, s;
    stream_pair(&c, &s);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(s);
        static unsigned char out[BIG_LEN];
        for (size_t i = 0; i < BIG_LEN; i++)
            out[i] = pattern(i);
        size_t off = 0;
        while (off < BIG_LEN) {
            size_t n = BIG_LEN - off < 7000 ? BIG_LEN - off : 7000;
            ssize_t r = send(c, out + off, n, 0);
            if (r <= 0)
                _exit(1);
            off += r;
        }
        _exit(0);
    }
    close(c);

    static unsigned char in[BIG_LEN];
    size_t got = 0;
    while (got < BIG_LEN) {
        ssize_t r = recv(s, in + got, BIG_LEN - got, 0);
        ASSERT_GT(r, 0);
        got += r;
    }
    size_t bad = 0;
    for (size_t i = 0; i < BIG_LEN; i++)
        if (in[i] != pattern(i))
            bad++;
    EXPECT_EQ(bad, (size_t)0);
    int st = ktest_wait_timeout(pid, 10);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    close(s);
}

TEST(stream_recv_blocks)
{
    int c, s;
    stream_pair(&c, &s);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        _exit(send(c, "late", 4, 0) == 4 ? 0 : 1);
    }

    char buf[8] = {0};
    long long t0 = ktest_now_ns();
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 4);
    EXPECT_GE(ktest_now_ns() - t0, 50000000LL);
    EXPECT_EQ(memcmp(buf, "late", 4), 0);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    close(c);
    close(s);
}

TEST(stream_recv_eintr)
{
    int c, s;
    stream_pair(&c, &s);
    arm_interrupt(1);
    char ch;
    EXPECT_ERR(recv(s, &ch, 1, 0), EINTR);
    close(c);
    close(s);
}

/* interrupted after some bytes went out: the partial count, not EINTR */
TEST_TIMEOUT(stream_send_eintr_partial, 10)
{
    int c, s;
    stream_pair(&c, &s);
    static char big[BIG_LEN];
    arm_interrupt(1);
    ssize_t r = send(c, big, sizeof(big), 0);
    EXPECT_GT(r, 0);
    EXPECT_LT(r, (ssize_t)sizeof(big));
    close(c);
    close(s);
}

TEST(stream_eof_drains_first)
{
    int c, s;
    stream_pair(&c, &s);
    EXPECT_EQ(send(c, "bye", 3, 0), 3);
    close(c);
    char buf[8] = {0};
    /* queued data before EOF */
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 3);
    EXPECT_EQ(memcmp(buf, "bye", 3), 0);
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 0);
    EXPECT_EQ(recv(s, buf, sizeof(buf), 0), 0);
    close(s);
}

TEST_TIMEOUT(stream_close_wakes_reader, 10)
{
    int c, s;
    stream_pair(&c, &s);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        _exit(0); /* drops the last reference to c */
    }
    close(c);

    char ch;
    EXPECT_EQ(recv(s, &ch, 1, 0), 0);
    ktest_wait_timeout(pid, 5);
    close(s);
}

TEST(stream_send_after_peer_close)
{
    int c, s;
    stream_pair(&c, &s);
    signal(SIGPIPE, SIG_IGN);
    close(s);
    EXPECT_ERR(send(c, "x", 1, 0), EPIPE);
    close(c);
}

TEST(stream_sigpipe)
{
    int c, s;
    stream_pair(&c, &s);
    close(s);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        signal(SIGPIPE, SIG_DFL);
        send(c, "x", 1, 0);
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE);
    close(c);
}

/* writer blocked on a full ring must wake when the reader goes away */
TEST_TIMEOUT(stream_close_wakes_writer, 10)
{
    int c, s;
    stream_pair(&c, &s);

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        close(s);
        signal(SIGPIPE, SIG_IGN);
        static char big[BIG_LEN];
        ssize_t r = send(c, big, sizeof(big), 0);
        /* partial count for what fit, then EPIPE */
        if (r <= 0 || r >= (ssize_t)sizeof(big))
            _exit(1);
        _exit(send(c, big, 1, 0) < 0 && errno == EPIPE ? 0 : 2);
    }
    close(c);
    usleep(200000);
    close(s);

    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}
