
#ifndef	__LILAC_USER_FCNTL_H
#ifdef __cplusplus
extern "C" {
#endif
#define	__LILAC_USER_FCNTL_H

#define O_ACCMODE   000000003
#define O_RDONLY    000000000
#define O_WRONLY    000000001
#define O_RDWR      000000002
#define O_APPEND    000000010
#define O_CREAT     000001000
#define O_TRUNC     000002000
#define O_EXCL      000004000
#define O_NONBLOCK  000040000
#define O_NOCTTY    000100000
#if __BSD_VISIBLE || defined(__KERNEL__)
#define	O_DIRECT    000400000
#endif
#if __POSIX_VISIBLE >= 200809 || defined(__KERNEL__)
#define O_CLOEXEC   001000000
#define O_NOFOLLOW  004000000
#define O_DIRECTORY 010000000
#define O_EXEC      020000000
#define O_SEARCH    020000000
#endif
#define O_SYNC      040000000

#define O_DSYNC O_SYNC
#define O_RSYNC O_SYNC

/* Encoding of the file mode.  */

#define S_IFMT      0170000 /* These bits determine file type.  */

/* File types.  */
#define S_IFDIR     0040000 /* Directory.  */
#define S_IFCHR     0020000 /* Character device.  */
#define S_IFBLK     0060000 /* Block device.  */
#define S_IFREG     0100000 /* Regular file.  */
#define S_IFIFO     0010000 /* FIFO.  */
#define S_IFLNK     0120000 /* Symbolic link.  */
#define S_IFSOCK    0140000 /* Socket.  */

/* Protection bits.  */

#define S_ISUID 04000 /* Set user ID on execution.  */
#define S_ISGID 02000 /* Set group ID on execution.  */
#define S_ISVTX 01000 /* Save swapped text after use (sticky).  */
#define S_IREAD  0400 /* Read by owner.  */
#define S_IWRITE 0200 /* Write by owner.  */
#define S_IEXEC  0100 /* Execute by owner.  */

/* XXX close on exec request; must match UF_EXCLOSE in user.h */
#define	FD_CLOEXEC	1	/* posix */

/* fcntl(2) requests */
#define	F_DUPFD		0	/* Duplicate fildes */
#define	F_GETFD		1	/* Get fildes flags (close on exec) */
#define	F_SETFD		2	/* Set fildes flags (close on exec) */
#define	F_GETFL		3	/* Get file flags */
#define	F_SETFL		4	/* Set file flags */
#define	F_GETOWN 	5	/* Get owner - for ASYNC */
#define	F_SETOWN 	6	/* Set owner - for ASYNC */
#define	F_GETLK  	7	/* Get record-locking information */
#define	F_SETLK  	8	/* Set or Clear a record-lock (Non-Blocking) */
#define	F_SETLKW 	9	/* Set or Clear a record-lock (Blocking) */
#define	F_RGETLK 	10	/* Test a remote lock to see if it is blocked */
#define	F_RSETLK 	11	/* Set or unlock a remote lock */
#define	F_CNVT 		12	/* Convert a fhandle to an open fd */
#define	F_RSETLKW 	13	/* Set or Clear remote record-lock(Blocking) */
#define	F_DUPFD_CLOEXEC	14	/* As F_DUPFD, but set close-on-exec flag */

/* fcntl(2) flags (l_type field of flock structure) */
#define	F_RDLCK		1	/* read lock */
#define	F_WRLCK		2	/* write lock */
#define	F_UNLCK		3	/* remove lock(s) */
#define	F_UNLKSYS	4	/* remove remote locks for a given system */

#if __BSD_VISIBLE || __POSIX_VISIBLE >= 200809
/* Special descriptor value to denote the cwd in calls to openat(2) etc. */
#define AT_FDCWD -2

/* Flag values for faccessat2) et al. */
#define AT_EACCESS                 0x0001
#define AT_SYMLINK_NOFOLLOW        0x0002
#define AT_SYMLINK_FOLLOW          0x0004
#define AT_REMOVEDIR               0x0008
#if __GNU_VISIBLE
#define AT_EMPTY_PATH              0x0010
#define _AT_NULL_PATHNAME_ALLOWED  0x4000 /* Internal flag used by futimesat */
#endif
#endif

#if __BSD_VISIBLE
/* lock operations for flock(2) */
#define	LOCK_SH		0x01		/* shared file lock */
#define	LOCK_EX		0x02		/* exclusive file lock */
#define	LOCK_NB		0x04		/* don't block when locking */
#define	LOCK_UN		0x08		/* unlock file */
#endif

/*#include <sys/stdtypes.h>*/

/* file segment locking set data type - information passed to system by user */
struct flock {
	short	l_type;		/* F_RDLCK, F_WRLCK, or F_UNLCK */
	short	l_whence;	/* flag to choose starting offset */
	long	l_start;	/* relative offset, in bytes */
	long	l_len;		/* length, in bytes; 0 means lock to EOF */
	short	l_pid;		/* returned with F_GETLK */
	short	l_xxx;		/* reserved for future use */
};

#ifdef __cplusplus
}
#endif
#endif	/* !__LILAC_USER_FCNTL_H */
