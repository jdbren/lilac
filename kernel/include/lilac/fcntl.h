
#ifndef	__LILAC_USER_FCNTL_H
#ifdef __cplusplus
extern "C" {
#endif
#define	__LILAC_USER_FCNTL_H

#define O_ACCMODE   03
#define O_RDONLY    00
#define O_WRONLY    01
#define O_RDWR      02

#define O_CREAT        0100
#define O_EXCL         0200
#define O_NOCTTY       0400
#define O_TRUNC       01000
#define O_APPEND      02000
#define O_NONBLOCK    04000
#define O_DSYNC      010000
#define O_SYNC     04010000
#define O_RSYNC    04010000
#define O_DIRECTORY 0200000
#define O_NOFOLLOW  0400000
#define O_CLOEXEC  02000000

#define O_ASYNC      020000
#define O_DIRECT     040000
#define O_LARGEFILE 0100000
#define O_NOATIME  01000000
#define O_PATH    010000000
#define O_TMPFILE 020200000
#define O_NDELAY O_NONBLOCK

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

#define F_SETOWN 8
#define F_GETOWN 9
#define F_SETSIG 10
#define F_GETSIG 11

#if __LONG_MAX__ == 0x7fffffffL
#define F_GETLK 12
#define F_SETLK 13
#define F_SETLKW 14
#else
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#endif

#define F_SETOWN_EX 15
#define F_GETOWN_EX 16

#define F_GETOWNER_UIDS 17

#define F_DUPFD_CLOEXEC 1030

/* fcntl(2) flags (l_type field of flock structure) */
#define	F_RDLCK		1	/* read lock */
#define	F_WRLCK		2	/* write lock */
#define	F_UNLCK		3	/* remove lock(s) */
#define	F_UNLKSYS	4	/* remove remote locks for a given system */

/* Special descriptor value to denote the cwd in calls to openat(2) etc. */
#define AT_FDCWD -100

/* Flag values for faccessat2) et al. */
#define AT_EACCESS                 0x0001
#define AT_SYMLINK_NOFOLLOW        0x0002
#define AT_SYMLINK_FOLLOW          0x0004
#define AT_REMOVEDIR               0x0008
#define AT_EMPTY_PATH              0x0010

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
