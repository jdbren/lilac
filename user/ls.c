#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int compare_names(const void *a, const void *b)
{
    const char *const *na = a;
    const char *const *nb = b;
    return strcmp(*na, *nb);
}

static void free_names(char **names, int count)
{
    if (!names)
        return;
    for (int i = 0; i < count; ++i)
        free(names[i]);
    free(names);
}

static void build_mode_string(mode_t mode, char *buf)
{
    buf[0] = S_ISDIR(mode)  ? 'd'
           : S_ISLNK(mode)  ? 'l'
           : S_ISCHR(mode)  ? 'c'
           : S_ISBLK(mode)  ? 'b'
           : S_ISFIFO(mode) ? 'p'
           : S_ISSOCK(mode) ? 's'
           : '-';

    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    buf[3] = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    buf[6] = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    buf[9] = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') : ((mode & S_ISVTX) ? 'T' : '-');
    buf[10] = '\0';
}

static long long file_size_digits(long long size)
{
    long long digits = 1;
    while (size >= 10) {
        size /= 10;
        digits++;
    }
    return digits;
}

static void print_long(const char *dir, const char *sep, char **names, int count)
{
    for (int i = 0; i < count; ++i) {
        char path[1024];
        int needed = snprintf(path, sizeof(path), "%s%s%s", dir, sep, names[i]);
        if (needed < 0 || needed >= (int)sizeof(path)) {
            fprintf(stderr, "ls: path too long: %s%s%s\n", dir, sep, names[i]);
            continue;
        }

        struct stat st;
        if (lstat(path, &st) == -1) {
            fprintf(stderr, "ls: lstat %s: %s\n", path, strerror(errno));
            continue;
        }

        char perms[11];
        build_mode_string(st.st_mode, perms);

        // struct passwd *pw = getpwuid(st.st_uid);
        // struct group *gr = getgrgid(st.st_gid);
        char timebuf[32] = "";
        struct tm *tm = localtime(&st.st_mtime);
        if (tm)
            strftime(timebuf, sizeof(timebuf), "%b %e %H:%M", tm);

        printf("%s %2ld %s %s %8lld %s %s",
                perms,
                (long)st.st_nlink,
                "root",
                "root",
                (long long)st.st_size,
                timebuf[0] ? timebuf : "???",
                names[i]);

        if (S_ISLNK(st.st_mode)) {
            char linkbuf[1024];
            ssize_t len = readlink(path, linkbuf, sizeof(linkbuf) - 1);
            if (len > 0) {
                linkbuf[len] = '\0';
                printf(" -> %s", linkbuf);
            }
        }

        putchar('\n');
    }
}

int main(int argc, char *argv[])
{
    int show_long = 0;
    int opt;
    while ((opt = getopt(argc, argv, "l")) != -1) {
        if (opt == 'l')
            show_long = 1;
        else {
            fprintf(stderr, "Usage: ls [-l] [directory]\n");
            return 1;
        }
    }

    const char *dir = optind < argc ? argv[optind] : ".";
    const size_t dir_len = strlen(dir);
    const char *sep = (dir_len > 0 && dir[dir_len - 1] != '/') ? "/" : "";

    DIR *dp = opendir(dir);
    if (!dp) {
        perror("ls: opendir");
        return 1;
    }

    const int INITIAL_CAPACITY = 16;
    char **names = malloc(INITIAL_CAPACITY * sizeof(char *));
    if (!names) {
        perror("ls: malloc");
        closedir(dp);
        return 1;
    }

    int count = 0;
    int capacity = INITIAL_CAPACITY;
    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dp))) {
        if (count >= capacity) {
            int new_capacity = capacity * 2;
            char **tmp = realloc(names, new_capacity * sizeof(char *));
            if (!tmp) {
                perror("ls: realloc");
                free_names(names, count);
                closedir(dp);
                return 1;
            }
            names = tmp;
            capacity = new_capacity;
        }

        char *name = strdup(entry->d_name);
        if (!name) {
            perror("ls: strdup");
            free_names(names, count);
            closedir(dp);
            return 1;
        }
        names[count++] = name;
    }

    if (errno != 0) {
        perror("ls: readdir");
        free_names(names, count);
        closedir(dp);
        return 1;
    }

    closedir(dp);

    qsort(names, count, sizeof(char *), compare_names);

    if (show_long) {
        print_long(dir, sep, names, count);
    } else {
        const int col_width = 14;
        const int cols = (80 + 1) / (col_width + 2);
        for (int i = 0; i < count; ++i) {
            printf("%-14s", names[i]);
            if ((i + 1) % cols == 0)
                putchar('\n');
        }
        if (count == 0 || count % cols != 0)
            putchar('\n');
    }

    free_names(names, count);
    return 0;
}
