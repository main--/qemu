/*
 * Compress filter block driver
 *
 * Copyright (c) 2019 Virtuozzo International GmbH
 *
 * Author:
 *   Andrey Shinkevich <andrey.shinkevich@virtuozzo.com>
 *   (based on block/copy-on-read.c by Max Reitz)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) any later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "block/block_int.h"
#include "qemu/module.h"


static int compress_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    bs->backing = bdrv_open_child(NULL, options, "file", bs, &child_file, false,
                                  errp);
    if (!bs->backing) {
        return -EINVAL;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        BDRV_REQ_WRITE_COMPRESSED |
        (BDRV_REQ_FUA & bs->backing->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->backing->bs->supported_zero_flags);

    return 0;
}


#define PERM_PASSTHROUGH (BLK_PERM_CONSISTENT_READ \
                          | BLK_PERM_WRITE \
                          | BLK_PERM_RESIZE)
#define PERM_UNCHANGED (BLK_PERM_ALL & ~PERM_PASSTHROUGH)

static void compress_child_perm(BlockDriverState *bs, BdrvChild *c,
                                const BdrvChildRole *role,
                                BlockReopenQueue *reopen_queue,
                                uint64_t perm, uint64_t shared,
                                uint64_t *nperm, uint64_t *nshared)
{
    *nperm = perm & PERM_PASSTHROUGH;
    *nshared = (shared & PERM_PASSTHROUGH) | PERM_UNCHANGED;

    /*
     * We must not request write permissions for an inactive node, the child
     * cannot provide it.
     */
    if (!(bs->open_flags & BDRV_O_INACTIVE)) {
        *nperm |= BLK_PERM_WRITE_UNCHANGED;
    }
}


static int64_t compress_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->backing->bs);
}


static int coroutine_fn compress_co_truncate(BlockDriverState *bs,
                                             int64_t offset, bool exact,
                                             PreallocMode prealloc,
                                             Error **errp)
{
    return bdrv_co_truncate(bs->backing, offset, exact, prealloc, errp);
}


static int coroutine_fn compress_co_preadv_part(BlockDriverState *bs,
                                                uint64_t offset, uint64_t bytes,
                                                QEMUIOVector *qiov,
                                                size_t qiov_offset,
                                                int flags)
{
    return bdrv_co_preadv_part(bs->backing, offset, bytes, qiov, qiov_offset,
                               flags);
}


static int coroutine_fn compress_co_pwritev_part(BlockDriverState *bs,
                                                 uint64_t offset,
                                                 uint64_t bytes,
                                                 QEMUIOVector *qiov,
                                                 size_t qiov_offset, int flags)
{
    return bdrv_co_pwritev_part(bs->backing, offset, bytes, qiov, qiov_offset,
                                flags | BDRV_REQ_WRITE_COMPRESSED);
}


static int coroutine_fn compress_co_pwrite_zeroes(BlockDriverState *bs,
                                                  int64_t offset, int bytes,
                                                  BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->backing, offset, bytes, flags);
}


static int coroutine_fn compress_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int bytes)
{
    return bdrv_co_pdiscard(bs->backing, offset, bytes);
}


static int compress_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->backing->bs, bdi);
}


static void compress_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BlockDriverInfo bdi;
    int ret;

    if (!bs->backing) {
        return;
    }

    ret = bdrv_get_info(bs->backing->bs, &bdi);
    if (ret < 0 || bdi.cluster_size == 0) {
        return;
    }

    bs->bl.request_alignment = bdi.cluster_size;
}


static void compress_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->backing->bs, eject_flag);
}


static void compress_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->backing->bs, locked);
}


static bool compress_recurse_is_first_non_filter(BlockDriverState *bs,
                                                 BlockDriverState *candidate)
{
    return bdrv_recurse_is_first_non_filter(bs->backing->bs, candidate);
}


static BlockDriver bdrv_compress = {
    .format_name                        = "compress",

    .bdrv_open                          = compress_open,
    .bdrv_child_perm                    = compress_child_perm,

    .bdrv_getlength                     = compress_getlength,
    .bdrv_co_truncate                   = compress_co_truncate,

    .bdrv_co_preadv_part                = compress_co_preadv_part,
    .bdrv_co_pwritev_part               = compress_co_pwritev_part,
    .bdrv_co_pwrite_zeroes              = compress_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   = compress_co_pdiscard,
    .bdrv_get_info                      = compress_get_info,
    .bdrv_refresh_limits                = compress_refresh_limits,

    .bdrv_eject                         = compress_eject,
    .bdrv_lock_medium                   = compress_lock_medium,

    .bdrv_co_block_status               = bdrv_co_block_status_from_backing,

    .bdrv_recurse_is_first_non_filter   = compress_recurse_is_first_non_filter,

    .has_variable_length                = true,
    .is_filter                          = true,
};

static void bdrv_compress_init(void)
{
    bdrv_register(&bdrv_compress);
}

block_init(bdrv_compress_init);
