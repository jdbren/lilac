# Remaining work to pass the test suite

### Bugs, fixed
- [x] proc.vfork_parent_suspended: restored the musl asm vfork (`musl/src/process/x86_64/vfork.s`)
      and fixed `RESTORE_REGS` (entry/macros.S), which swapped rcx/rdx on return from fork, so the
      vfork child looped forever in the asm stub.
- [x] zz_hazard.oom_anon_memory: alloc_frames now returns NULL; page-table allocation failures in
      faults return FAULT_OOM (task gets SIGKILL). Unrecoverable sites (boot, fork page tables,
      pgd) still panic explicitly.
- [x] signal.registers_preserved_across_handler: signal frames didn't save FP/SSE state, so a
      handler using floating point corrupted the interrupted code. The frame now carries an
      FXSAVE area, and the handler starts from the init FP state. Per-delivery signal logging is
      now behind DEBUG_SIGNAL; it had slowed each delivery to about 1.6 ms, which hid the bug.
      A task with no saved FP state now gets a clean SSE state rather than the previous owner's.

- [x] Review fixes (signal.sa_restart_after_sigreturn, signal.kill_pgrp_many,
      thread.exit_group_kills_many_threads, zz_hazard.leader_reaped_with_live_threads,
      proc.exec_no_mm_leak, signal.ucontext_*):
      sigreturn no longer counts as a restartable syscall; kill(-pgid) and exit_group reach more
      than 64 tasks; a zombie thread-group leader isn't waitable until its last thread exits;
      exec frees the old mm and pgd; signal handlers get a Linux-layout ucontext (gregs and
      sigmask edits take effect on return); sigreturn with `fpregs == NULL`
      resets the FP state as on Linux (it used to kill the process). A stopped child now wakes a parent blocked in
      waitpid(WUNTRACED).

- [x] Exec/fork leaks (proc.exec_no_mm_leak, proc.fork_no_leak; both need `sysinfo`, not added yet):
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
- [x] fat32 unlink/rmdir/rename and LFN creation (fs/fat32/namei.c), and vfs_rmdir now checks
      ENOENT/ENOTDIR before EPERM. An unlinked file's clusters are freed on its last close.
      Covers fs.fat32.{mkdir_rmdir, rmdir_*, unlink_*, rename_*, readdir_lists_entries,
      long_filename, filename_case_and_dots}

- [x] signal.registers_preserved_across_handler: signal frames didn't save FP/SSE state, so a
      handler using floating point corrupted the interrupted code. The frame now carries an
      FXSAVE area, and the handler starts from the init FP state. Per-delivery signal logging is
      now behind DEBUG_SIGNAL; it had slowed each delivery to about 1.6 ms, which hid the bug.
      A task with no saved FP state now gets a clean SSE state rather than the previous owner's.

### Bugs, deferred (need a page cache or sharing mechanism)
- [ ] mem.memfd_write_read_mmap: there's no page cache, so MAP_SHARED file mappings are private
      copies that only write back at munmap or msync. `pread` sees stale data.
- [ ] mem.memfd_ftruncate_mmap: the same cause, plus `tmpfs_truncate` (fs/tmpfs/inode.c:288)
      only sets `i_size`. It never grows or zeroes `tmpfs_file->data`, so a later read runs past
      the buffer.

### Known bugs, not fixed (no failing test yet)
From the code reviews:
- [ ] fork: page-table allocation failures in `fork_copy_vm_area`/`make_64_bit_mmap` panic
      instead of fork returning -ENOMEM (arch/x86/kernel/paging64.c).

Found while fixing the above:
- [ ] `vfs_umount` (fs/mount.c) never works: looking up the target crosses into the mounted
      root, so it never sees `d_mount` and returns -EINVAL.
- [ ] The dcache never shrinks. Only unlink/rmdir/rename remove entries, so every lookup of a
      new missing name leaves a negative dentry behind for good.
- [ ] tmpfs: if `tmpfs_dir_append` fails in `tmpfs_rename`, the entry is already removed from
      the old directory, so the file disappears while its dentry stays cached.
- [ ] tmpfs: create/mkdir/symlink don't check `alloc_inode`/`kzmalloc`/`kmalloc` for failure.
- [ ] ext2 builds a new inode on every lookup (no inode cache), so two names of one file get
      separate inodes. Harmless while ext2 is read-only.
- [ ] Fixed-size name buffers: `get_final_dentry` in fs/mount.c (basename 16) truncates longer
      paths, and `vfs_mount` leaks dentry references on its error paths (boot only). (`vfs_create`
      now goes through the normal lookup and create path.)
- [ ] `dev_mknod` (kernel/device.c) is never called; mknod returns -ENOSYS.
- [ ] fat32 readdir gives every entry the directory's own `d_ino`; inode numbers are only
      assigned when a name is looked up.
- [ ] fat32 has no locking of its own. Changes to one directory are serialized by the VFS
      (the directory's `i_mutex`), but the FAT and FSInfo are shared: creates or writes in
      different directories at once can allocate clusters concurrently.
- [ ] fat32: if a write fails partway, clusters already linked past the end of the file keep
      stale data, and a later ftruncate growth would expose it (it trusts clusters on the chain
      to be zeroed).
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
- [ ] sysinfo: proc.exec_no_mm_leak and proc.fork_no_leak use it to measure free memory
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
