# Remaining work to pass the test suite

### Bugs, fixed
- [x] proc.vfork_parent_suspended: restored the musl asm vfork (`musl/src/process/x86_64/vfork.s`)
      and fixed `RESTORE_REGS` (entry/macros.S), which swapped rcx/rdx on return from fork, so the
      vfork child looped forever in the asm stub.
- [x] zz_hazard.oom_anon_memory: alloc_frames now returns NULL; page-table allocation failures in
      faults return FAULT_OOM (task gets SIGKILL). Unrecoverable sites (boot, fork page tables,
      pgd) still panic explicitly.
- [x] fat32 unlink/rmdir/rename and LFN creation (fs/fat32/namei.c), and vfs_rmdir now checks
      ENOENT/ENOTDIR before EPERM. An unlinked file's clusters are freed on its last close.
      Covers fs.fat32.{mkdir_rmdir, rmdir_*, unlink_*, rename_*, readdir_lists_entries,
      long_filename, filename_case_and_dots}

- [x] signal.registers_preserved_across_handler: signal frames didn't save FP/SSE state, so a
      handler using floating point corrupted the interrupted code. The frame now carries an
      FXSAVE area, and the handler starts from the init FP state. Per-delivery signal logging is
      now behind DEBUG_SIGNAL; it had slowed each delivery to about 1.6 ms, which hid the bug.
      A task with no saved FP state now gets a clean SSE state rather than the previous owner's.

- [x] Review fixes (signal.sa_restart_after_sigreturn, signal.kill_pgrp_many,
      thread.exit_group_kills_many_threads, zz_hazard.leader_reaped_with_live_threads,
      proc.exec_no_mm_leak, signal.ucontext_*, fs.*.rename_dir_updates_cwd,
      fs.*.dentry_no_leak):
      sigreturn no longer counts as a restartable syscall; kill(-pgid) and exit_group reach more
      than 64 tasks; a zombie thread-group leader isn't waitable until its last thread exits;
      exec frees the old mm and pgd; signal handlers get a Linux-layout ucontext (gregs and
      sigmask edits take effect on return); rename moves the dentry; dentries are refcounted
      and freed once dropped from the cache. Each dentry and each tmpfs directory entry
      holds a counted inode reference, and the last iput frees the inode
      (fs.*.hard_link_no_leak, fs.*.rename_over_open_target); sigreturn with `fpregs == NULL`
      resets the FP state as on Linux (it used to kill the process). A stopped child now wakes a parent blocked in waitpid(WUNTRACED). New
      `sysinfo` syscall (90) lets tests measure free memory.

- [x] Exec/fork leaks (proc.exec_no_mm_leak, now 0 KiB with a 32 KiB limit; proc.fork_no_leak):
      a successful execve never returned to free its argv/envp pointer arrays (512 bytes per
      exec), and freeing an fd table skipped its close-on-exec bitmap. exec also dropped the
      exec file reference inherited from fork without closing it, and a failed argument copy
      left `info->path`/`exec_file` dangling (freed again at exit); the copy is now
      all-or-nothing.
- [x] Stop signals (signal.sigstop_self_stops_before_returning, signal.sigkill_stopped_task):
      after a default-action signal, `do_kernel_exit_work` returned to userspace without
      rescheduling, so a stopped task ran until its next kernel entry; it now loops and
      switches out first. SIGKILL now wakes a stopped task so it can die (it used to hang
      until SIGCONT).

### Bugs, deferred (need a page cache or sharing mechanism)
- [ ] mem.memfd_write_read_mmap: there's no page cache, so MAP_SHARED file mappings are private
      copies that only write back at munmap or msync. `pread` sees stale data.
- [ ] mem.memfd_ftruncate_mmap: the same cause, plus `tmpfs_truncate` (fs/tmpfs/inode.c)
      only sets `i_size`. It never grows or zeroes `tmpfs_file->data`, so a later read runs past
      the buffer.

### Known bugs, not fixed (no failing test yet)
From the code reviews:
- [ ] fat32: `fat_new_entry` (fs/fat32/namei.c) ignores errors from `fat_dir_flush` and
      `fat_write_FAT`, so create/mkdir report success after a failed write and can leak clusters.
- [ ] fat32: names up to 255 chars are accepted, but `struct dirent.d_name` is 128 bytes, so
      getdents returns a 127-char name that open/stat can't find.
- [ ] fat32: readdir (`__fat32_read_all_dirent`, fs/fat32/dir.c) has its own entry parser and
      skips the LFN checksum/ordinal checks, so it can list names lookup can't find. Rebuild it on
      `fat_dir_load`/`fat_dir_next`.
- [ ] fat32: each new cluster in `fat32_write` is written twice (zeroed, then the data), and
      growing a file with ftruncate (`vfs_ftruncate`, fs/vfs.c) loops 4 KB writes.
- [ ] fat32: `fat_make_alias` tries up to 999999 `~N` suffixes, rescanning the directory each
      time. Real FAT drivers cap the search.
- [ ] fork: page-table allocation failures in `fork_copy_vm_area`/`make_64_bit_mmap` panic
      instead of fork returning -ENOMEM (arch/x86/kernel/paging64.c).
- [ ] rlimits are stored but not enforced: the fd table still uses FD_MAX/FD_AUTO_MAX, and
      stack/brk sizes use constants.

Found while fixing the above:
- [ ] `vfs_umount` (fs/mount.c) never works: looking up the target crosses into the mounted
      root, so it never sees `d_mount` and returns -EINVAL.
- [ ] The dcache never shrinks. Only unlink/rmdir/rename remove entries, so every lookup of a
      new missing name leaves a negative dentry behind for good.
- [ ] `d_move` briefly takes the dentry out of the cache between parents; a concurrent lookup
      of the new name in that window can create a second dentry for the same inode.
- [ ] tmpfs: if `tmpfs_dir_append` fails in `tmpfs_rename`, the entry is already removed from
      the old directory, so the file disappears while its dentry stays cached.
- [ ] tmpfs: create/mkdir/symlink don't check `alloc_inode`/`kzmalloc`/`kmalloc` for failure.
- [ ] ext2 builds a new inode on every lookup (no inode cache), so two names of one file get
      separate inodes. Harmless while ext2 is read-only.
- [ ] Fixed-size name buffers: `vfs_create` (dirname 64, basename 16) and `get_final_dentry`
      in fs/mount.c (basename 16) truncate longer paths; `vfs_create` also leaks its basename
      on early errors and `vfs_mount` leaks dentry references on its error paths (boot only).
- [ ] `dev_mknod` (kernel/device.c) is never called; mknod returns -ENOSYS.
- [ ] Signal frame details: `REG_CSGSFS` holds only cs, `REG_ERR`/`REG_TRAPNO` are always 0,
      and sigaltstack isn't supported (`uc_stack` is always zero). The 32-bit signal path still
      uses the old frame layout.

### Missing features, done
- [x] setitimer/getitimer (ITIMER_REAL, with intervals): signal.setitimer_real. alarm() now
      uses the same per-task timer (`task->itimer_real`).
- [x] clock_nanosleep, including TIMER_ABSTIME: time.clock_nanosleep_abs
- [x] getrlimit/setrlimit/prlimit64 (limits are stored per process but not enforced): misc.getrlimit_works
- [x] brk/sbrk through libc: musl's sbrk now grows and shrinks the break through SYS_brk

### Missing features, deferred
- [ ] ext2 write support: fs.ext2_write_support (skipped in the test for now)
- [ ] poll/ppoll syscall: pipe.poll_readable
- [ ] Single-step traps: zz_hazard.sigreturn_trap_flag_single_steps. `debug_handler`
      (arch/x86/entry/faults.c) ignores #DB, so user-mode single-steps never raise SIGTRAP.
      Handlers also start with the interrupted code's TF/DF; Linux clears both on handler entry.
- [ ] Real-time signals: thread.cancel_{blocked_in_read,async_spin,deferred_testcancel}.
      `_NSIG` is 32, so musl's SIGCANCEL (33) is rejected with EINVAL and pthread_cancel can't
      interrupt a blocking syscall or cancel asynchronously; only pthread_testcancel polling works.
      Needs `_NSIG` 65 and the tables sized by it.

### Expected skips (by design)
- fs.ext2_write_support: skipped until ext2 has write support
- fs.fat32.{hard_link, symlink_readlink, symlink_relative_and_dangling}: FAT has no links
