/*
 * 9p  xattr callback
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 * Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "9p.h"
#include "fsdev/file-op-9p.h"
#include "9p-xattr.h"
#include "9p-util.h"

enum {
    XATTRAT_OP_GET = 0,
    XATTRAT_OP_LIST,
    XATTRAT_OP_SET,
    XATTRAT_OP_REMOVE
};

struct xattrat_data {
    ssize_t ret;
    int serrno;
    char value[0];
};

static void munmap_preserver_errno(void *addr, size_t length)
{
    int serrno = errno;
    munmap(addr, length);
    errno = serrno;
}

static ssize_t do_xattrat_op(int op_type, int dirfd, const char *path,
                             const char *name, void *value, size_t size,
                             int flags)
{
    struct xattrat_data *data;
    pid_t pid;
    ssize_t ret = -1;
    int wstatus;

    data = mmap(NULL, sizeof(*data) + size, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
        return -1;
    }
    data->ret = -1;

    pid = fork();
    if (pid < 0) {
        goto err_out;
    } else if (pid == 0) {
        if (fchdir(dirfd) == 0) {
            switch (op_type) {
            case XATTRAT_OP_GET:
                data->ret = lgetxattr(path, name, data->value, size);
                break;
            case XATTRAT_OP_LIST:
                data->ret = llistxattr(path, data->value, size);
                break;
            case XATTRAT_OP_SET:
                data->ret = lsetxattr(path, name, value, size, flags);
                break;
            case XATTRAT_OP_REMOVE:
                data->ret = lremovexattr(path, name);
                break;
            default:
                g_assert_not_reached();
            }
        }
        data->serrno = errno;
        _exit(0);
    }
    assert(waitpid(pid, &wstatus, 0) == pid && WIFEXITED(wstatus));

    ret = data->ret;
    if (ret < 0) {
        errno = data->serrno;
        goto err_out;
    }
    if (value) {
        memcpy(value, data->value, data->ret);
    }
err_out:
    munmap_preserver_errno(data, sizeof(*data) + size);
    return ret;
}

ssize_t fgetxattrat_nofollow(int dirfd, const char *path, const char *name,
                             void *value, size_t size)
{
    return do_xattrat_op(XATTRAT_OP_GET, dirfd, path, name, value, size, 0);
}

ssize_t local_getxattr_nofollow(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size)
{
    char *dirpath = g_path_get_dirname(path);
    char *filename = g_path_get_basename(path);
    int dirfd;
    ssize_t ret = -1;

    dirfd = local_opendir_nofollow(ctx, dirpath);
    if (dirfd == -1) {
        goto out;
    }

    ret = fgetxattrat_nofollow(dirfd, filename, name, value, size);
    close_preserve_errno(dirfd);
out:
    g_free(dirpath);
    g_free(filename);
    return ret;
}

int fsetxattrat_nofollow(int dirfd, const char *path, const char *name,
                         void *value, size_t size, int flags)
{
    return do_xattrat_op(XATTRAT_OP_SET, dirfd, path, name, value, size, flags);
}

ssize_t local_setxattr_nofollow(FsContext *ctx, const char *path,
                                const char *name, void *value, size_t size,
                                int flags)
{
    char *dirpath = g_path_get_dirname(path);
    char *filename = g_path_get_basename(path);
    int dirfd;
    ssize_t ret = -1;

    dirfd = local_opendir_nofollow(ctx, dirpath);
    if (dirfd == -1) {
        goto out;
    }

    ret = fsetxattrat_nofollow(dirfd, filename, name, value, size, flags);
    close_preserve_errno(dirfd);
out:
    g_free(dirpath);
    g_free(filename);
    return ret;
}

static ssize_t fremovexattrat_nofollow(int dirfd, const char *path,
                                       const char *name)
{
    return do_xattrat_op(XATTRAT_OP_GET, dirfd, path, name, NULL, 0, 0);
}

ssize_t local_removexattr_nofollow(FsContext *ctx, const char *path,
                                   const char *name)
{
    char *dirpath = g_path_get_dirname(path);
    char *filename = g_path_get_basename(path);
    int dirfd;
    ssize_t ret = -1;

    dirfd = local_opendir_nofollow(ctx, dirpath);
    if (dirfd == -1) {
        goto out;
    }

    ret = fremovexattrat_nofollow(dirfd, filename, name);
    close_preserve_errno(dirfd);
out:
    g_free(dirpath);
    g_free(filename);
    return ret;
}

static XattrOperations *get_xattr_operations(XattrOperations **h,
                                             const char *name)
{
    XattrOperations *xops;
    for (xops = *(h)++; xops != NULL; xops = *(h)++) {
        if (!strncmp(name, xops->name, strlen(xops->name))) {
            return xops;
        }
    }
    return NULL;
}

ssize_t v9fs_get_xattr(FsContext *ctx, const char *path,
                       const char *name, void *value, size_t size)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->getxattr(ctx, path, name, value, size);
    }
    errno = EOPNOTSUPP;
    return -1;
}

ssize_t pt_listxattr(FsContext *ctx, const char *path,
                     char *name, void *value, size_t size)
{
    int name_size = strlen(name) + 1;
    if (!value) {
        return name_size;
    }

    if (size < name_size) {
        errno = ERANGE;
        return -1;
    }

    /* no need for strncpy: name_size is strlen(name)+1 */
    memcpy(value, name, name_size);
    return name_size;
}


/*
 * Get the list and pass to each layer to find out whether
 * to send the data or not
 */
ssize_t v9fs_list_xattr(FsContext *ctx, const char *path,
                        void *value, size_t vsize)
{
    ssize_t size = 0;
    char *buffer;
    void *ovalue = value;
    XattrOperations *xops;
    char *orig_value, *orig_value_start;
    ssize_t xattr_len, parsed_len = 0, attr_len;

    /* Get the actual len */
    buffer = rpath(ctx, path);
    xattr_len = llistxattr(buffer, value, 0);
    if (xattr_len <= 0) {
        g_free(buffer);
        return xattr_len;
    }

    /* Now fetch the xattr and find the actual size */
    orig_value = g_malloc(xattr_len);
    xattr_len = llistxattr(buffer, orig_value, xattr_len);
    g_free(buffer);

    /* store the orig pointer */
    orig_value_start = orig_value;
    while (xattr_len > parsed_len) {
        xops = get_xattr_operations(ctx->xops, orig_value);
        if (!xops) {
            goto next_entry;
        }

        if (!value) {
            size += xops->listxattr(ctx, path, orig_value, value, vsize);
        } else {
            size = xops->listxattr(ctx, path, orig_value, value, vsize);
            if (size < 0) {
                goto err_out;
            }
            value += size;
            vsize -= size;
        }
next_entry:
        /* Got the next entry */
        attr_len = strlen(orig_value) + 1;
        parsed_len += attr_len;
        orig_value += attr_len;
    }
    if (value) {
        size = value - ovalue;
    }

err_out:
    g_free(orig_value_start);
    return size;
}

int v9fs_set_xattr(FsContext *ctx, const char *path, const char *name,
                   void *value, size_t size, int flags)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->setxattr(ctx, path, name, value, size, flags);
    }
    errno = EOPNOTSUPP;
    return -1;

}

int v9fs_remove_xattr(FsContext *ctx,
                      const char *path, const char *name)
{
    XattrOperations *xops = get_xattr_operations(ctx->xops, name);
    if (xops) {
        return xops->removexattr(ctx, path, name);
    }
    errno = EOPNOTSUPP;
    return -1;

}

ssize_t pt_getxattr(FsContext *ctx, const char *path, const char *name,
                    void *value, size_t size)
{
    char *buffer;
    ssize_t ret;

    buffer = rpath(ctx, path);
    ret = lgetxattr(buffer, name, value, size);
    g_free(buffer);
    return ret;
}

int pt_setxattr(FsContext *ctx, const char *path, const char *name, void *value,
                size_t size, int flags)
{
    char *buffer;
    int ret;

    buffer = rpath(ctx, path);
    ret = lsetxattr(buffer, name, value, size, flags);
    g_free(buffer);
    return ret;
}

int pt_removexattr(FsContext *ctx, const char *path, const char *name)
{
    char *buffer;
    int ret;

    buffer = rpath(ctx, path);
    ret = lremovexattr(path, name);
    g_free(buffer);
    return ret;
}

ssize_t notsup_getxattr(FsContext *ctx, const char *path, const char *name,
                        void *value, size_t size)
{
    errno = ENOTSUP;
    return -1;
}

int notsup_setxattr(FsContext *ctx, const char *path, const char *name,
                    void *value, size_t size, int flags)
{
    errno = ENOTSUP;
    return -1;
}

ssize_t notsup_listxattr(FsContext *ctx, const char *path, char *name,
                         void *value, size_t size)
{
    return 0;
}

int notsup_removexattr(FsContext *ctx, const char *path, const char *name)
{
    errno = ENOTSUP;
    return -1;
}

XattrOperations *mapped_xattr_ops[] = {
    &mapped_user_xattr,
    &mapped_pacl_xattr,
    &mapped_dacl_xattr,
    NULL,
};

XattrOperations *passthrough_xattr_ops[] = {
    &passthrough_user_xattr,
    &passthrough_acl_xattr,
    NULL,
};

/* for .user none model should be same as passthrough */
XattrOperations *none_xattr_ops[] = {
    &passthrough_user_xattr,
    &none_acl_xattr,
    NULL,
};
