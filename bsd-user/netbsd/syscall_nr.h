/* $NetBSD: syscall.h,v 1.215 2008/06/17 16:07:57 tsutsui Exp $ */

/*
 * System call numbers.
 *
 * created from	NetBSD: syscalls.master,v 1.204 2008/06/17 16:05:23 tsutsui Exp
 */

#define TARGET_NETBSD_NR_syscall     0
#define TARGET_NETBSD_NR_exit        1
#define TARGET_NETBSD_NR_fork        2
#define TARGET_NETBSD_NR_read        3
#define TARGET_NETBSD_NR_write       4
#define TARGET_NETBSD_NR_open        5
#define TARGET_NETBSD_NR_close       6
#define TARGET_NETBSD_NR_wait4       7
#define TARGET_NETBSD_NR_compat_43_ocreat    8
#define TARGET_NETBSD_NR_link        9
#define TARGET_NETBSD_NR_unlink      10
#define TARGET_NETBSD_NR_chdir       12
#define TARGET_NETBSD_NR_fchdir      13
#define TARGET_NETBSD_NR_mknod       14
#define TARGET_NETBSD_NR_chmod       15
#define TARGET_NETBSD_NR_chown       16
#define TARGET_NETBSD_NR_break       17
#define TARGET_NETBSD_NR_compat_20_getfsstat 18
#define TARGET_NETBSD_NR_compat_43_olseek    19
#define TARGET_NETBSD_NR_getpid      20
#define TARGET_NETBSD_NR_getpid      20
#define TARGET_NETBSD_NR_compat_40_mount     21
#define TARGET_NETBSD_NR_unmount     22
#define TARGET_NETBSD_NR_setuid      23
#define TARGET_NETBSD_NR_getuid      24
#define TARGET_NETBSD_NR_getuid      24
#define TARGET_NETBSD_NR_geteuid     25
#define TARGET_NETBSD_NR_ptrace      26
#define TARGET_NETBSD_NR_recvmsg     27
#define TARGET_NETBSD_NR_sendmsg     28
#define TARGET_NETBSD_NR_recvfrom    29
#define TARGET_NETBSD_NR_accept      30
#define TARGET_NETBSD_NR_getpeername 31
#define TARGET_NETBSD_NR_getsockname 32
#define TARGET_NETBSD_NR_access      33
#define TARGET_NETBSD_NR_chflags     34
#define TARGET_NETBSD_NR_fchflags    35
#define TARGET_NETBSD_NR_sync        36
#define TARGET_NETBSD_NR_kill        37
#define TARGET_NETBSD_NR_compat_43_stat43    38
#define TARGET_NETBSD_NR_getppid     39
#define TARGET_NETBSD_NR_compat_43_lstat43   40
#define TARGET_NETBSD_NR_dup 41
#define TARGET_NETBSD_NR_pipe        42
#define TARGET_NETBSD_NR_getegid     43
#define TARGET_NETBSD_NR_profil      44
#define TARGET_NETBSD_NR_ktrace      45
#define TARGET_NETBSD_NR_compat_13_sigaction13       46
#define TARGET_NETBSD_NR_getgid      47
#define TARGET_NETBSD_NR_getgid      47
#define TARGET_NETBSD_NR_compat_13_sigprocmask13     48
#define TARGET_NETBSD_NR___getlogin  49
#define TARGET_NETBSD_NR___setlogin  50
#define TARGET_NETBSD_NR_acct        51
#define TARGET_NETBSD_NR_compat_13_sigpending13      52
#define TARGET_NETBSD_NR_compat_13_sigaltstack13     53
#define TARGET_NETBSD_NR_ioctl       54
#define TARGET_NETBSD_NR_compat_12_oreboot   55
#define TARGET_NETBSD_NR_revoke      56
#define TARGET_NETBSD_NR_symlink     57
#define TARGET_NETBSD_NR_readlink    58
#define TARGET_NETBSD_NR_execve      59
#define TARGET_NETBSD_NR_umask       60
#define TARGET_NETBSD_NR_chroot      61
#define TARGET_NETBSD_NR_compat_43_fstat43   62
#define TARGET_NETBSD_NR_compat_43_ogetkerninfo      63
#define TARGET_NETBSD_NR_compat_43_ogetpagesize      64
#define TARGET_NETBSD_NR_compat_12_msync     65
#define TARGET_NETBSD_NR_vfork       66
#define TARGET_NETBSD_NR_sbrk        69
#define TARGET_NETBSD_NR_sstk        70
#define TARGET_NETBSD_NR_compat_43_ommap     71
#define TARGET_NETBSD_NR_vadvise     72
#define TARGET_NETBSD_NR_munmap      73
#define TARGET_NETBSD_NR_mprotect    74
#define TARGET_NETBSD_NR_madvise     75
#define TARGET_NETBSD_NR_mincore     78
#define TARGET_NETBSD_NR_getgroups   79
#define TARGET_NETBSD_NR_setgroups   80
#define TARGET_NETBSD_NR_getpgrp     81
#define TARGET_NETBSD_NR_setpgid     82
#define TARGET_NETBSD_NR_setitimer   83
#define TARGET_NETBSD_NR_compat_43_owait     84
#define TARGET_NETBSD_NR_compat_12_oswapon   85
#define TARGET_NETBSD_NR_getitimer   86
#define TARGET_NETBSD_NR_compat_43_ogethostname      87
#define TARGET_NETBSD_NR_compat_43_osethostname      88
#define TARGET_NETBSD_NR_compat_43_ogetdtablesize    89
#define TARGET_NETBSD_NR_dup2        90
#define TARGET_NETBSD_NR_fcntl       92
#define TARGET_NETBSD_NR_select      93
#define TARGET_NETBSD_NR_fsync       95
#define TARGET_NETBSD_NR_setpriority 96
#define TARGET_NETBSD_NR_compat_30_socket    97
#define TARGET_NETBSD_NR_connect     98
#define TARGET_NETBSD_NR_compat_43_oaccept   99
#define TARGET_NETBSD_NR_getpriority 100
#define TARGET_NETBSD_NR_compat_43_osend     101
#define TARGET_NETBSD_NR_compat_43_orecv     102
#define TARGET_NETBSD_NR_compat_13_sigreturn13       103
#define TARGET_NETBSD_NR_bind        104
#define TARGET_NETBSD_NR_setsockopt  105
#define TARGET_NETBSD_NR_listen      106
#define TARGET_NETBSD_NR_compat_43_osigvec   108
#define TARGET_NETBSD_NR_compat_43_osigblock 109
#define TARGET_NETBSD_NR_compat_43_osigsetmask       110
#define TARGET_NETBSD_NR_compat_13_sigsuspend13      111
#define TARGET_NETBSD_NR_compat_43_osigstack 112
#define TARGET_NETBSD_NR_compat_43_orecvmsg  113
#define TARGET_NETBSD_NR_compat_43_osendmsg  114
#define TARGET_NETBSD_NR_gettimeofday        116
#define TARGET_NETBSD_NR_getrusage   117
#define TARGET_NETBSD_NR_getsockopt  118
#define TARGET_NETBSD_NR_readv       120
#define TARGET_NETBSD_NR_writev      121
#define TARGET_NETBSD_NR_settimeofday        122
#define TARGET_NETBSD_NR_fchown      123
#define TARGET_NETBSD_NR_fchmod      124
#define TARGET_NETBSD_NR_compat_43_orecvfrom 125
#define TARGET_NETBSD_NR_setreuid    126
#define TARGET_NETBSD_NR_setregid    127
#define TARGET_NETBSD_NR_rename      128
#define TARGET_NETBSD_NR_compat_43_otruncate 129
#define TARGET_NETBSD_NR_compat_43_oftruncate        130
#define TARGET_NETBSD_NR_flock       131
#define TARGET_NETBSD_NR_mkfifo      132
#define TARGET_NETBSD_NR_sendto      133
#define TARGET_NETBSD_NR_shutdown    134
#define TARGET_NETBSD_NR_socketpair  135
#define TARGET_NETBSD_NR_mkdir       136
#define TARGET_NETBSD_NR_rmdir       137
#define TARGET_NETBSD_NR_utimes      138
#define TARGET_NETBSD_NR_adjtime     140
#define TARGET_NETBSD_NR_compat_43_ogetpeername      141
#define TARGET_NETBSD_NR_compat_43_ogethostid        142
#define TARGET_NETBSD_NR_compat_43_osethostid        143
#define TARGET_NETBSD_NR_compat_43_ogetrlimit        144
#define TARGET_NETBSD_NR_compat_43_osetrlimit        145
#define TARGET_NETBSD_NR_compat_43_okillpg   146
#define TARGET_NETBSD_NR_setsid      147
#define TARGET_NETBSD_NR_quotactl    148
#define TARGET_NETBSD_NR_compat_43_oquota    149
#define TARGET_NETBSD_NR_compat_43_ogetsockname      150
#define TARGET_NETBSD_NR_nfssvc      155
#define TARGET_NETBSD_NR_compat_43_ogetdirentries    156
#define TARGET_NETBSD_NR_compat_20_statfs    157
#define TARGET_NETBSD_NR_compat_20_fstatfs   158
#define TARGET_NETBSD_NR_compat_30_getfh     161
#define TARGET_NETBSD_NR_compat_09_ogetdomainname    162
#define TARGET_NETBSD_NR_compat_09_osetdomainname    163
#define TARGET_NETBSD_NR_compat_09_ouname    164
#define TARGET_NETBSD_NR_sysarch     165
#define TARGET_NETBSD_NR_compat_10_osemsys   169
#define TARGET_NETBSD_NR_compat_10_omsgsys   170
#define TARGET_NETBSD_NR_compat_10_oshmsys   171
#define TARGET_NETBSD_NR_pread       173
#define TARGET_NETBSD_NR_pwrite      174
#define TARGET_NETBSD_NR_compat_30_ntp_gettime       175
#define TARGET_NETBSD_NR_ntp_adjtime 176
#define TARGET_NETBSD_NR_setgid      181
#define TARGET_NETBSD_NR_setegid     182
#define TARGET_NETBSD_NR_seteuid     183
#define TARGET_NETBSD_NR_lfs_bmapv   184
#define TARGET_NETBSD_NR_lfs_markv   185
#define TARGET_NETBSD_NR_lfs_segclean        186
#define TARGET_NETBSD_NR_lfs_segwait 187
#define TARGET_NETBSD_NR_compat_12_stat12    188
#define TARGET_NETBSD_NR_compat_12_fstat12   189
#define TARGET_NETBSD_NR_compat_12_lstat12   190
#define TARGET_NETBSD_NR_pathconf    191
#define TARGET_NETBSD_NR_fpathconf   192
#define TARGET_NETBSD_NR_getrlimit   194
#define TARGET_NETBSD_NR_setrlimit   195
#define TARGET_NETBSD_NR_compat_12_getdirentries     196
#define TARGET_NETBSD_NR_mmap        197
#define TARGET_NETBSD_NR___syscall   198
#define TARGET_NETBSD_NR_lseek       199
#define TARGET_NETBSD_NR_truncate    200
#define TARGET_NETBSD_NR_ftruncate   201
#define TARGET_NETBSD_NR___sysctl    202
#define TARGET_NETBSD_NR_mlock       203
#define TARGET_NETBSD_NR_munlock     204
#define TARGET_NETBSD_NR_undelete    205
#define TARGET_NETBSD_NR_futimes     206
#define TARGET_NETBSD_NR_getpgid     207
#define TARGET_NETBSD_NR_reboot      208
#define TARGET_NETBSD_NR_poll        209
#define TARGET_NETBSD_NR_compat_14___semctl  220
#define TARGET_NETBSD_NR_semget      221
#define TARGET_NETBSD_NR_semop       222
#define TARGET_NETBSD_NR_semconfig   223
#define TARGET_NETBSD_NR_compat_14_msgctl    224
#define TARGET_NETBSD_NR_msgget      225
#define TARGET_NETBSD_NR_msgsnd      226
#define TARGET_NETBSD_NR_msgrcv      227
#define TARGET_NETBSD_NR_shmat       228
#define TARGET_NETBSD_NR_compat_14_shmctl    229
#define TARGET_NETBSD_NR_shmdt       230
#define TARGET_NETBSD_NR_shmget      231
#define TARGET_NETBSD_NR_clock_gettime       232
#define TARGET_NETBSD_NR_clock_settime       233
#define TARGET_NETBSD_NR_clock_getres        234
#define TARGET_NETBSD_NR_timer_create        235
#define TARGET_NETBSD_NR_timer_delete        236
#define TARGET_NETBSD_NR_timer_settime       237
#define TARGET_NETBSD_NR_timer_gettime       238
#define TARGET_NETBSD_NR_timer_getoverrun    239
#define TARGET_NETBSD_NR_nanosleep   240
#define TARGET_NETBSD_NR_fdatasync   241
#define TARGET_NETBSD_NR_mlockall    242
#define TARGET_NETBSD_NR_munlockall  243
#define TARGET_NETBSD_NR___sigtimedwait      244
#define TARGET_NETBSD_NR_modctl      246
#define TARGET_NETBSD_NR__ksem_init  247
#define TARGET_NETBSD_NR__ksem_open  248
#define TARGET_NETBSD_NR__ksem_unlink        249
#define TARGET_NETBSD_NR__ksem_close 250
#define TARGET_NETBSD_NR__ksem_post  251
#define TARGET_NETBSD_NR__ksem_wait  252
#define TARGET_NETBSD_NR__ksem_trywait       253
#define TARGET_NETBSD_NR__ksem_getvalue      254
#define TARGET_NETBSD_NR__ksem_destroy       255
#define TARGET_NETBSD_NR_mq_open     257
#define TARGET_NETBSD_NR_mq_close    258
#define TARGET_NETBSD_NR_mq_unlink   259
#define TARGET_NETBSD_NR_mq_getattr  260
#define TARGET_NETBSD_NR_mq_setattr  261
#define TARGET_NETBSD_NR_mq_notify   262
#define TARGET_NETBSD_NR_mq_send     263
#define TARGET_NETBSD_NR_mq_receive  264
#define TARGET_NETBSD_NR_mq_timedsend        265
#define TARGET_NETBSD_NR_mq_timedreceive     266
#define TARGET_NETBSD_NR___posix_rename      270
#define TARGET_NETBSD_NR_swapctl     271
#define TARGET_NETBSD_NR_compat_30_getdents  272
#define TARGET_NETBSD_NR_minherit    273
#define TARGET_NETBSD_NR_lchmod      274
#define TARGET_NETBSD_NR_lchown      275
#define TARGET_NETBSD_NR_lutimes     276
#define TARGET_NETBSD_NR___msync13   277
#define TARGET_NETBSD_NR_compat_30___stat13  278
#define TARGET_NETBSD_NR_compat_30___fstat13 279
#define TARGET_NETBSD_NR_compat_30___lstat13 280
#define TARGET_NETBSD_NR___sigaltstack14     281
#define TARGET_NETBSD_NR___vfork14   282
#define TARGET_NETBSD_NR___posix_chown       283
#define TARGET_NETBSD_NR___posix_fchown      284
#define TARGET_NETBSD_NR___posix_lchown      285
#define TARGET_NETBSD_NR_getsid      286
#define TARGET_NETBSD_NR___clone     287
#define TARGET_NETBSD_NR_fktrace     288
#define TARGET_NETBSD_NR_preadv      289
#define TARGET_NETBSD_NR_pwritev     290
#define TARGET_NETBSD_NR_compat_16___sigaction14     291
#define TARGET_NETBSD_NR___sigpending14      292
#define TARGET_NETBSD_NR___sigprocmask14     293
#define TARGET_NETBSD_NR___sigsuspend14      294
#define TARGET_NETBSD_NR_compat_16___sigreturn14     295
#define TARGET_NETBSD_NR___getcwd    296
#define TARGET_NETBSD_NR_fchroot     297
#define TARGET_NETBSD_NR_compat_30_fhopen    298
#define TARGET_NETBSD_NR_compat_30_fhstat    299
#define TARGET_NETBSD_NR_compat_20_fhstatfs  300
#define TARGET_NETBSD_NR_____semctl13        301
#define TARGET_NETBSD_NR___msgctl13  302
#define TARGET_NETBSD_NR___shmctl13  303
#define TARGET_NETBSD_NR_lchflags    304
#define TARGET_NETBSD_NR_issetugid   305
#define TARGET_NETBSD_NR_utrace      306
#define TARGET_NETBSD_NR_getcontext  307
#define TARGET_NETBSD_NR_setcontext  308
#define TARGET_NETBSD_NR__lwp_create 309
#define TARGET_NETBSD_NR__lwp_exit   310
#define TARGET_NETBSD_NR__lwp_self   311
#define TARGET_NETBSD_NR__lwp_wait   312
#define TARGET_NETBSD_NR__lwp_suspend        313
#define TARGET_NETBSD_NR__lwp_continue       314
#define TARGET_NETBSD_NR__lwp_wakeup 315
#define TARGET_NETBSD_NR__lwp_getprivate     316
#define TARGET_NETBSD_NR__lwp_setprivate     317
#define TARGET_NETBSD_NR__lwp_kill   318
#define TARGET_NETBSD_NR__lwp_detach 319
#define TARGET_NETBSD_NR__lwp_park   320
#define TARGET_NETBSD_NR__lwp_unpark 321
#define TARGET_NETBSD_NR__lwp_unpark_all     322
#define TARGET_NETBSD_NR__lwp_setname        323
#define TARGET_NETBSD_NR__lwp_getname        324
#define TARGET_NETBSD_NR__lwp_ctl    325
#define TARGET_NETBSD_NR_sa_register 330
#define TARGET_NETBSD_NR_sa_stacks   331
#define TARGET_NETBSD_NR_sa_enable   332
#define TARGET_NETBSD_NR_sa_setconcurrency   333
#define TARGET_NETBSD_NR_sa_yield    334
#define TARGET_NETBSD_NR_sa_preempt  335
#define TARGET_NETBSD_NR_sa_unblockyield     336
#define TARGET_NETBSD_NR___sigaction_sigtramp        340
#define TARGET_NETBSD_NR_pmc_get_info        341
#define TARGET_NETBSD_NR_pmc_control 342
#define TARGET_NETBSD_NR_rasctl      343
#define TARGET_NETBSD_NR_kqueue      344
#define TARGET_NETBSD_NR_kevent      345
#define TARGET_NETBSD_NR__sched_setparam     346
#define TARGET_NETBSD_NR__sched_getparam     347
#define TARGET_NETBSD_NR__sched_setaffinity  348
#define TARGET_NETBSD_NR__sched_getaffinity  349
#define TARGET_NETBSD_NR_sched_yield 350
#define TARGET_NETBSD_NR_fsync_range 354
#define TARGET_NETBSD_NR_uuidgen     355
#define TARGET_NETBSD_NR_getvfsstat  356
#define TARGET_NETBSD_NR_statvfs1    357
#define TARGET_NETBSD_NR_fstatvfs1   358
#define TARGET_NETBSD_NR_compat_30_fhstatvfs1        359
#define TARGET_NETBSD_NR_extattrctl  360
#define TARGET_NETBSD_NR_extattr_set_file    361
#define TARGET_NETBSD_NR_extattr_get_file    362
#define TARGET_NETBSD_NR_extattr_delete_file 363
#define TARGET_NETBSD_NR_extattr_set_fd      364
#define TARGET_NETBSD_NR_extattr_get_fd      365
#define TARGET_NETBSD_NR_extattr_delete_fd   366
#define TARGET_NETBSD_NR_extattr_set_link    367
#define TARGET_NETBSD_NR_extattr_get_link    368
#define TARGET_NETBSD_NR_extattr_delete_link 369
#define TARGET_NETBSD_NR_extattr_list_fd     370
#define TARGET_NETBSD_NR_extattr_list_file   371
#define TARGET_NETBSD_NR_extattr_list_link   372
#define TARGET_NETBSD_NR_pselect     373
#define TARGET_NETBSD_NR_pollts      374
#define TARGET_NETBSD_NR_setxattr    375
#define TARGET_NETBSD_NR_lsetxattr   376
#define TARGET_NETBSD_NR_fsetxattr   377
#define TARGET_NETBSD_NR_getxattr    378
#define TARGET_NETBSD_NR_lgetxattr   379
#define TARGET_NETBSD_NR_fgetxattr   380
#define TARGET_NETBSD_NR_listxattr   381
#define TARGET_NETBSD_NR_llistxattr  382
#define TARGET_NETBSD_NR_flistxattr  383
#define TARGET_NETBSD_NR_removexattr 384
#define TARGET_NETBSD_NR_lremovexattr        385
#define TARGET_NETBSD_NR_fremovexattr        386
#define TARGET_NETBSD_NR___stat30    387
#define TARGET_NETBSD_NR___fstat30   388
#define TARGET_NETBSD_NR___lstat30   389
#define TARGET_NETBSD_NR___getdents30        390
#define TARGET_NETBSD_NR_compat_30___fhstat30        392
#define TARGET_NETBSD_NR___ntp_gettime30     393
#define TARGET_NETBSD_NR___socket30  394
#define TARGET_NETBSD_NR___getfh30   395
#define TARGET_NETBSD_NR___fhopen40  396
#define TARGET_NETBSD_NR___fhstatvfs140      397
#define TARGET_NETBSD_NR___fhstat40  398
#define TARGET_NETBSD_NR_aio_cancel  399
#define TARGET_NETBSD_NR_aio_error   400
#define TARGET_NETBSD_NR_aio_fsync   401
#define TARGET_NETBSD_NR_aio_read    402
#define TARGET_NETBSD_NR_aio_return  403
#define TARGET_NETBSD_NR_aio_suspend 404
#define TARGET_NETBSD_NR_aio_write   405
#define TARGET_NETBSD_NR_lio_listio  406
#define TARGET_NETBSD_NR___mount50   410
#define TARGET_NETBSD_NR_mremap      411
#define TARGET_NETBSD_NR_pset_create 412
#define TARGET_NETBSD_NR_pset_destroy        413
#define TARGET_NETBSD_NR_pset_assign 414
#define TARGET_NETBSD_NR__pset_bind  415
#define TARGET_NETBSD_NR___posix_fadvise50   416

/*	$NetBSD: trap.h,v 1.18 2011/03/27 18:47:08 martin Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)trap.h	8.1 (Berkeley) 6/11/93
 */
/*
 * Sun4m support by Aaron Brown, Harvard University.
 * Changes Copyright (c) 1995 The President and Fellows of Harvard College.
 * All rights reserved.
 */

/* flags to system call (flags in %g1 along with syscall number) */
#define	TARGET_NETBSD_SYSCALL_G2RFLAG	0x400	/* on success, return to %g2 rather than npc */
#define	TARGET_NETBSD_SYSCALL_G7RFLAG	0x800	/* use %g7 as above (deprecated) */
#define	TARGET_NETBSD_SYSCALL_G5RFLAG	0xc00	/* use %g5 as above (only ABI compatible way) */
