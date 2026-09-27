# Remaining work to pass the test suite

### Bugs, fixed
- [x] proc.vfork_parent_suspended: restored the musl asm vfork (`musl/src/process/x86_64/vfork.s`)
      and fixed `RESTORE_REGS` (entry/macros.S), which swapped rcx/rdx on return from fork, so the
      vfork child looped forever in the asm stub.
- [x] zz_hazard.oom_anon_memory: alloc_frames now returns NULL; page-table allocation failures in
      faults return FAULT_OOM (task gets SIGKILL). Unrecoverable sites (boot, fork page tables,
      pgd) still panic explicitly.

### Bugs, deferred (need a page cache or sharing mechanism)
- [ ] mem.memfd_write_read_mmap: there's no page cache, so MAP_SHARED file mappings are private
      copies that only write back at munmap or msync. `pread` sees stale data.
- [ ] mem.memfd_ftruncate_mmap: the same cause, plus `tmpfs_truncate` (fs/tmpfs/inode.c:288)
      only sets `i_size`. It never grows or zeroes `tmpfs_file->data`, so a later read runs past
      the buffer.

### Missing features, deferred
- [ ] fat32 `unlink`, `rmdir`, `rename` inode ops. There's also a small VFS bug: the VFS returns
      EPERM for a missing op before it checks ENOENT or ENOTDIR. Covers
      fs.fat32.{mkdir_rmdir, rmdir_nonempty, rmdir_on_file_enotdir, unlink_file,
      unlink_open_file_readable, unlink_then_recreate, rename_file, rename_overwrites,
      rename_across_dirs}
- [ ] fat32 LFN entry creation on create/mkdir. Only 8.3 names are written, so mixed case, long
      names, spaces, multiple dots and leading dots are lost. Covers
      fs.fat32.{readdir_lists_entries ("Delta" → "DELTA"), long_filename,
      filename_case_and_dots}
- [ ] ext2 write support: fs.ext2_write_support (EROFS)
- [ ] poll/ppoll syscall: pipe.poll_readable
- [ ] setitimer/getitimer (ITIMER_REAL): signal.setitimer_real
- [ ] clock_nanosleep, including TIMER_ABSTIME: time.clock_nanosleep_abs
- [ ] getrlimit/prlimit64: misc.getrlimit_works
- [ ] brk/sbrk through libc: the mem.brk_sbrk_grow_shrink skip

### Expected skips (by design)
- fs.fat32.{hard_link, symlink_readlink, symlink_relative_and_dangling}: FAT has no links
