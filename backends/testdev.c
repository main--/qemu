/*
 * QEMU Char Device for testsuite control
 *
 * Copyright (c) 2014 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/char.h"

#define BUF_SIZE 32

typedef struct {
    Chardev parent;

    uint8_t in_buf[32];
    int in_buf_used;
} TestdevChardev;

/* Try to interpret a whole incoming packet */
static int testdev_eat_packet(TestdevChardev *testdev)
{
    const uint8_t *cur = testdev->in_buf;
    int len = testdev->in_buf_used;
    uint8_t c;
    int arg;

#define EAT(c) do { \
    if (!len--) {   \
        return 0;   \
    }               \
    c = *cur++;     \
} while (0)

    EAT(c);

    while (isspace(c)) {
        EAT(c);
    }

    arg = 0;
    while (isdigit(c)) {
        arg = arg * 10 + c - '0';
        EAT(c);
    }

    while (isspace(c)) {
        EAT(c);
    }

    switch (c) {
    case 'q':
        exit((arg << 1) | 1);
        break;
    default:
        break;
    }
    return cur - testdev->in_buf;
}

/* The other end is writing some data.  Store it and try to interpret */
static int testdev_write(Chardev *chr, const uint8_t *buf, int len)
{
    TestdevChardev *testdev = (TestdevChardev *)chr;
    int tocopy, eaten, orig_len = len;

    while (len) {
        /* Complete our buffer as much as possible */
        tocopy = MIN(len, BUF_SIZE - testdev->in_buf_used);

        memcpy(testdev->in_buf + testdev->in_buf_used, buf, tocopy);
        testdev->in_buf_used += tocopy;
        buf += tocopy;
        len -= tocopy;

        /* Interpret it as much as possible */
        while (testdev->in_buf_used > 0 &&
               (eaten = testdev_eat_packet(testdev)) > 0) {
            memmove(testdev->in_buf, testdev->in_buf + eaten,
                    testdev->in_buf_used - eaten);
            testdev->in_buf_used -= eaten;
        }
    }
    return orig_len;
}

static Chardev *chr_testdev_init(const CharDriver *driver,
                                 const char *id,
                                 ChardevBackend *backend,
                                 ChardevReturn *ret,
                                 bool *be_opened,
                                 Error **errp)
{
    TestdevChardev *testdev = g_new0(TestdevChardev, 1);;
    Chardev *chr = (Chardev *)testdev;

    chr->driver = driver;

    return chr;
}

static void register_types(void)
{
    static const CharDriver driver = {
        .instance_size = sizeof(TestdevChardev),
        .kind = CHARDEV_BACKEND_KIND_TESTDEV,
        .parse = NULL, .create = chr_testdev_init,
        .chr_write = testdev_write,
    };
    register_char_driver(&driver);
}

type_init(register_types);
