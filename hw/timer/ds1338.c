/*
 * MAXIM DS1338 I2C RTC+NVRAM
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "hw/registerfields.h"
#include "qemu/bcd.h"

/* Size of NVRAM including both the user-accessible area and the
 * secondary register area.
 */
#define NVRAM_SIZE 64

#define CTRL_OSF   0x20

#define TYPE_DS1338 "ds1338"
#define DS1338(obj) OBJECT_CHECK(DS1338State, (obj), TYPE_DS1338)

/* values stored in BCD */
/* 00-59 */
#define R_SEC   (0x0)
/* 00-59 */
#define R_MIN   (0x1)
#define R_HOUR  (0x2)
/* 1-7 */
#define R_WDAY  (0x3)
/* 0-31 */
#define R_DATE  (0x4)
#define R_MONTH (0x5)
/* 0-99 */
#define R_YEAR  (0x6)

#define R_CTRL  (0x7)

/* use 12 hour mode when set */
FIELD(HOUR, SET12, 6, 1)
/* 00-23 */
FIELD(HOUR, HOUR24, 0, 6)
/* PM when set */
FIELD(HOUR, AMPM, 5, 1)
/* 1-12 (not 0-11!) */
FIELD(HOUR, HOUR12, 0, 5)

/* 1-12 */
FIELD(MONTH, MONTH, 0, 5)
FIELD(MONTH, CENTURY, 7, 1)

FIELD(CTRL, OSF, 5, 1)

typedef struct DS1338State {
    I2CSlave parent_obj;

    int64_t offset;
    uint8_t wday_offset;
    uint8_t nvram[NVRAM_SIZE];
    int32_t ptr;
    bool addr_byte;
} DS1338State;

static const VMStateDescription vmstate_ds1338 = {
    .name = "ds1338",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, DS1338State),
        VMSTATE_INT64(offset, DS1338State),
        VMSTATE_UINT8_V(wday_offset, DS1338State, 2),
        VMSTATE_UINT8_ARRAY(nvram, DS1338State, NVRAM_SIZE),
        VMSTATE_INT32(ptr, DS1338State),
        VMSTATE_BOOL(addr_byte, DS1338State),
        VMSTATE_END_OF_LIST()
    }
};

static void capture_current_time(DS1338State *s)
{
    /* Capture the current time into the secondary registers
     * which will be actually read by the data transfer operation.
     */
    struct tm now;
    bool mode12 = ARRAY_FIELD_EX32(s->nvram, HOUR, SET12);
    qemu_get_timedate(&now, s->offset);

    s->nvram[R_SEC] = to_bcd(now.tm_sec);
    s->nvram[R_MIN] = to_bcd(now.tm_min);
    s->nvram[R_HOUR] = 0;
    if (mode12) {
        /* map 0-23 to 1-12 am/pm */
        ARRAY_FIELD_DP32(s->nvram, HOUR, SET12, 1);
        ARRAY_FIELD_DP32(s->nvram, HOUR, AMPM, now.tm_hour >= 12u);
        now.tm_hour %= 12u; /* wrap 0-23 to 0-11 */
        if (now.tm_hour == 0u) {
            /* midnight/noon stored as 12 */
            now.tm_hour = 12u;
        }
        ARRAY_FIELD_DP32(s->nvram, HOUR, HOUR12, to_bcd(now.tm_hour));

    } else {
        ARRAY_FIELD_DP32(s->nvram, HOUR, HOUR24, to_bcd(now.tm_hour));
    }
    s->nvram[R_WDAY] = (now.tm_wday + s->wday_offset) % 7;
    if (s->nvram[R_WDAY] == 0) {
        s->nvram[R_WDAY] = 7;
    }
    s->nvram[R_DATE] = to_bcd(now.tm_mday);
    s->nvram[R_MONTH] = to_bcd(now.tm_mon + 1);
    s->nvram[R_YEAR] = to_bcd(now.tm_year - 100);
}

static void inc_regptr(DS1338State *s)
{
    /* The register pointer wraps around after 0x3F; wraparound
     * causes the current time/date to be retransferred into
     * the secondary registers.
     */
    s->ptr = (s->ptr + 1) & (NVRAM_SIZE - 1);
    if (!s->ptr) {
        capture_current_time(s);
    }
}

static int ds1338_event(I2CSlave *i2c, enum i2c_event event)
{
    DS1338State *s = DS1338(i2c);

    switch (event) {
    case I2C_START_RECV:
        /* In h/w, capture happens on any START condition, not just a
         * START_RECV, but there is no need to actually capture on
         * START_SEND, because the guest can't get at that data
         * without going through a START_RECV which would overwrite it.
         */
        capture_current_time(s);
        break;
    case I2C_START_SEND:
        s->addr_byte = true;
        break;
    default:
        break;
    }

    return 0;
}

static int ds1338_recv(I2CSlave *i2c)
{
    DS1338State *s = DS1338(i2c);
    uint8_t res;

    res  = s->nvram[s->ptr];
    inc_regptr(s);
    return res;
}

/* call after guest writes to current time registers
 * to re-compute our offset from host time.
 */
static void ds1338_update(DS1338State *s)
{

    struct tm now;
    memset(&now, 0, sizeof(now));

    /* TODO: Implement CH (stop) bit?  */
    now.tm_sec = from_bcd(s->nvram[R_SEC] & 0x7f);
    now.tm_min = from_bcd(s->nvram[R_MIN] & 0x7f);
    if (ARRAY_FIELD_EX32(s->nvram, HOUR, SET12)) {
        /* 12 hour (1-12) */
        /* read and wrap 1-12 -> 0-11 */
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(s->nvram, HOUR, HOUR12)) % 12u;
        if (ARRAY_FIELD_EX32(s->nvram, HOUR, AMPM)) {
            now.tm_hour += 12;
        }

    } else {
        now.tm_hour = from_bcd(ARRAY_FIELD_EX32(s->nvram, HOUR, HOUR24));
    }
    now.tm_wday = from_bcd(s->nvram[R_WDAY]) - 1u;
    now.tm_mday = from_bcd(s->nvram[R_DATE] & 0x3f);
    now.tm_mon = from_bcd(s->nvram[R_MONTH] & 0x1f) - 1;
    now.tm_year = from_bcd(s->nvram[R_YEAR]) + 100;
    s->offset = qemu_timedate_diff(&now);

    {
        /* Round trip to get real wday_offset based on time delta and
         * ref. timezone.
         * Race if midnight (in ref. timezone) happens here.
         */
        int user_wday = now.tm_wday;
        qemu_get_timedate(&now, s->offset);

        s->wday_offset = (user_wday - now.tm_wday) % 7 + 1;
    }
}

static int ds1338_send(I2CSlave *i2c, uint8_t data)
{
    DS1338State *s = DS1338(i2c);

    if (s->addr_byte) {
        s->ptr = data & (NVRAM_SIZE - 1);
        s->addr_byte = false;
        return 0;
    }
    if (s->ptr == R_CTRL) {
        /* Control register. */

        /* Ensure bits 2, 3 and 6 will read back as zero. */
        data &= 0xB3;

        /* Attempting to write the OSF flag to logic 1 leaves the
           value unchanged. */
        data = (data & ~CTRL_OSF) | (data & s->nvram[s->ptr] & CTRL_OSF);

    }
    s->nvram[s->ptr] = data;
    if (s->ptr <= R_YEAR) {
        ds1338_update(s);
    }
    inc_regptr(s);
    return 0;
}

static void ds1338_reset(DeviceState *dev)
{
    DS1338State *s = DS1338(dev);

    /* The clock is running and synchronized with the host */
    s->offset = 0;
    s->wday_offset = 0;
    memset(s->nvram, 0, NVRAM_SIZE);
    s->ptr = 0;
    s->addr_byte = false;
}

static void ds1338_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = ds1338_event;
    k->recv = ds1338_recv;
    k->send = ds1338_send;
    dc->reset = ds1338_reset;
    dc->vmsd = &vmstate_ds1338;
}

static const TypeInfo ds1338_info = {
    .name          = TYPE_DS1338,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DS1338State),
    .class_init    = ds1338_class_init,
};

static void ds1338_register_types(void)
{
    type_register_static(&ds1338_info);
}

type_init(ds1338_register_types)
