/*
 * CAN device - SJA1000 chip emulation for QEMU
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2017 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
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
#include "chardev/char.h"
#include "hw/hw.h"
#include "can/can_emu.h"

#include "can_sja1000.h"

#ifndef DEBUG_FILTER
#define DEBUG_FILTER 0
#endif /*DEBUG_FILTER*/

static void can_sja_software_reset(CanSJA1000State *s)
{
    s->mode        &= ~0x31;
    s->mode        |= 0x01;
    s->status_pel  &= ~0x37;
    s->status_pel  |= 0x34;

    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;
}

void can_sja_hardware_reset(CanSJA1000State *s)
{
    /* Reset by hardware, p10 */
    s->mode        = 0x01;
    s->status_pel  = 0x3c;
    s->interrupt_pel = 0x00;
    s->clock       = 0x00;
    s->rxbuf_start = 0x00;
    s->rxmsg_cnt   = 0x00;
    s->rx_cnt      = 0x00;

    s->control     = 0x01;
    s->status_bas  = 0x0c;
    s->interrupt_bas = 0x00;

    s->irq_lower(s->irq_opaque);
}

static
void can_sja_single_filter(struct qemu_can_filter *filter,
            const uint8_t *acr,  const uint8_t *amr, int extended)
{
    if (extended) {
        filter->can_id = (uint32_t)acr[0] << 21;
        filter->can_id |= (uint32_t)acr[1] << 13;
        filter->can_id |= (uint32_t)acr[2] << 5;
        filter->can_id |= (uint32_t)acr[3] >> 3;
        if (acr[3] & 4) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 21;
        filter->can_mask |= (uint32_t)amr[1] << 13;
        filter->can_mask |= (uint32_t)amr[2] << 5;
        filter->can_mask |= (uint32_t)amr[3] >> 3;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_EFF_MASK;
        if (!(amr[3] & 4)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    } else {
        filter->can_id = (uint32_t)acr[0] << 3;
        filter->can_id |= (uint32_t)acr[1] >> 5;
        if (acr[1] & 0x10) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 3;
        filter->can_mask |= (uint32_t)amr[1] << 5;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_SFF_MASK;
        if (!(amr[1] & 4)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    }
}

static
void can_sja_dual_filter(struct qemu_can_filter *filter,
            const uint8_t *acr,  const uint8_t *amr, int extended)
{
    if (extended) {
        filter->can_id = (uint32_t)acr[0] << 21;
        filter->can_id |= (uint32_t)acr[1] << 13;

        filter->can_mask = (uint32_t)amr[0] << 21;
        filter->can_mask |= (uint32_t)amr[1] << 13;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_EFF_MASK & ~0x1fff;
    } else {
        filter->can_id = (uint32_t)acr[0] << 3;
        filter->can_id |= (uint32_t)acr[1] >> 5;
        if (acr[1] & 0x10) {
            filter->can_id |= QEMU_CAN_RTR_FLAG;
        }

        filter->can_mask = (uint32_t)amr[0] << 3;
        filter->can_mask |= (uint32_t)amr[1] >> 5;
        filter->can_mask = ~filter->can_mask & QEMU_CAN_SFF_MASK;
        if (!(amr[1] & 0x10)) {
            filter->can_mask |= QEMU_CAN_RTR_FLAG;
        }
    }
}

/* Details in DS-p22, what we need to do here is to test the data. */
static
int can_sja_accept_filter(CanSJA1000State *s,
                                 const qemu_can_frame *frame)
{

    struct qemu_can_filter filter;

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & (1 << 3)) { /* Single mode. */
            if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
                can_sja_single_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 1);

                if (!can_bus_filter_match(&filter, frame->can_id)) {
                    return 0;
                }
            } else { /* SFF */
                can_sja_single_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 0);

                if (!can_bus_filter_match(&filter, frame->can_id)) {
                    return 0;
                }

                if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
                    return 1;
                }

                if (frame->can_dlc == 0) {
                    return 1;
                }

                if ((frame->data[0] & ~(s->code_mask[6])) !=
                   (s->code_mask[2] & ~(s->code_mask[6]))) {
                    return 0;
                }

                if (frame->can_dlc < 2) {
                    return 1;
                }

                if ((frame->data[1] & ~(s->code_mask[7])) ==
                    (s->code_mask[3] & ~(s->code_mask[7]))) {
                    return 1;
                }

                return 0;
            }
        } else { /* Dual mode */
            if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
                can_sja_dual_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 1);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                can_sja_dual_filter(&filter,
                    s->code_mask + 2, s->code_mask + 6, 1);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                return 0;
            } else {
                can_sja_dual_filter(&filter,
                    s->code_mask + 0, s->code_mask + 4, 0);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    uint8_t expect;
                    uint8_t mask;
                    expect = s->code_mask[1] << 4;
                    expect |= s->code_mask[3] & 0x0f;

                    mask = s->code_mask[5] << 4;
                    mask |= s->code_mask[7] & 0x0f;
                        mask = ~mask & 0xff;

                    if ((frame->data[0] & mask) ==
                        (expect & mask)) {
                        return 1;
                    }
                }

                can_sja_dual_filter(&filter,
                    s->code_mask + 2, s->code_mask + 6, 0);

                if (can_bus_filter_match(&filter, frame->can_id)) {
                    return 1;
                }

                return 0;
            }
        }
    }

    return 1;
}

static void can_display_msg(const qemu_can_frame *msg)
{
    int i;

    fprintf(stderr, "%03X [%01d] -", (msg->can_id & QEMU_CAN_EFF_MASK),
            msg->can_dlc);

    if (msg->can_id & QEMU_CAN_EFF_FLAG) {
        fprintf(stderr, "EFF ");
    } else {
        fprintf(stderr, "SFF ");
    }
    if (msg->can_id & QEMU_CAN_RTR_FLAG) {
        fprintf(stderr, "RTR-");
    } else {
        fprintf(stderr, "DAT-");
    }
    for (i = 0; i < msg->can_dlc; i++) {
        fprintf(stderr, "  %02X", msg->data[i]);
    }
    for (; i < 8; i++) {
        fprintf(stderr, "    ");
    }
    fflush(stdout);
}

static void buff2frame_pel(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->can_id = 0;
    if (buff[0] & 0x40) { /* RTR */
        frame->can_id = QEMU_CAN_RTR_FLAG;
    }
    frame->can_dlc = buff[0] & 0x0f;

    if (buff[0] & 0x80) { /* Extended */
        frame->can_id |= QEMU_CAN_EFF_FLAG;
        frame->can_id |= buff[1] << 21; /* ID.28~ID.21 */
        frame->can_id |= buff[2] << 13; /* ID.20~ID.13 */
        frame->can_id |= buff[3] <<  5;
        frame->can_id |= buff[4] >>  3;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[5 + i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    } else {
        frame->can_id |= buff[1] <<  3;
        frame->can_id |= buff[2] >>  5;
        for (i = 0; i < frame->can_dlc; i++) {
            frame->data[i] = buff[3 + i];
        }
        for (; i < 8; i++) {
            frame->data[i] = 0;
        }
    }
}


static void buff2frame_bas(const uint8_t *buff, qemu_can_frame *frame)
{
    uint8_t i;

    frame->can_id = ((buff[0] << 3) & (0xff << 3)) + ((buff[1] >> 5) & 0x07);
    if (buff[1] & 0x10) { /* RTR */
        frame->can_id = QEMU_CAN_RTR_FLAG;
    }
    frame->can_dlc = buff[1] & 0x0f;

    for (i = 0; i < frame->can_dlc; i++) {
        frame->data[i] = buff[2 + i];
    }
    for (; i < 8; i++) {
        frame->data[i] = 0;
    }
}


static int frame2buff_pel(const qemu_can_frame *frame, uint8_t *buff)
{
    int i;

    if (frame->can_id & QEMU_CAN_ERR_FLAG) { /* error frame, NOT support now. */
        return -1;
    }

    buff[0] = 0x0f & frame->can_dlc; /* DLC */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
        buff[0] |= (1 << 6);
    }
    if (frame->can_id & QEMU_CAN_EFF_FLAG) { /* EFF */
        buff[0] |= (1 << 7);
        buff[1] = extract32(frame->can_id, 21, 8); /* ID.28~ID.21 */
        buff[2] = extract32(frame->can_id, 13, 8); /* ID.20~ID.13 */
        buff[3] = extract32(frame->can_id, 5, 8);  /* ID.12~ID.05 */
        buff[4] = extract32(frame->can_id, 0, 5) << 3; /* ID.04~ID.00,x,x,x */
        for (i = 0; i < frame->can_dlc; i++) {
            buff[5 + i] = frame->data[i];
        }
        return frame->can_dlc + 5;
    } else { /* SFF */
        buff[1] = extract32(frame->can_id, 3, 8); /* ID.10~ID.03 */
        buff[2] = extract32(frame->can_id, 0, 3) << 5; /* ID.02~ID.00,x,x,x,x,x */
        for (i = 0; i < frame->can_dlc; i++) {
            buff[3 + i] = frame->data[i];
        }

        return frame->can_dlc + 3;
    }

    return -1;
}

static int frame2buff_bas(const qemu_can_frame *frame, uint8_t *buff)
{
    int i;

    if ((frame->can_id & QEMU_CAN_EFF_FLAG) || /* EFF, not support for BasicMode. */
       (frame->can_id & QEMU_CAN_ERR_FLAG)) {  /* or Error frame, NOT support now. */
        return -1;
    }

    buff[0] = extract32(frame->can_id, 3, 8); /* ID.10~ID.03 */
    buff[1] = extract32(frame->can_id, 0, 3) << 5; /* ID.02~ID.00,x,x,x,x,x */
    if (frame->can_id & QEMU_CAN_RTR_FLAG) { /* RTR */
        buff[1] |= (1 << 4);
    }
    buff[1] |= frame->can_dlc & 0x0f;
    for (i = 0; i < frame->can_dlc; i++) {
        buff[2 + i] = frame->data[i];
    }

    return frame->can_dlc + 2;
}

void can_sja_mem_write(CanSJA1000State *s, hwaddr addr, uint64_t val,
                       unsigned size)
{
    qemu_can_frame   frame;
    uint32_t         tmp;
    uint8_t          tmp8, count;


    DPRINTF("write 0x%02llx addr 0x%02x\n",
            (unsigned long long)val, (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return ;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register */
            s->mode = 0x1f & val;
            if ((s->mode & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                if (s->mode & (1 << 3)) { /* Single mode. */
                    /* For EFF */
                    can_sja_single_filter(&s->filter[0],
                        s->code_mask + 0, s->code_mask + 4, 1);

                    /* For SFF */
                    can_sja_single_filter(&s->filter[1],
                        s->code_mask + 0, s->code_mask + 4, 0);

                    can_bus_client_set_filters(&s->bus_client, s->filter, 2);
                } else { /* Dual mode */
                    /* For EFF */
                    can_sja_dual_filter(&s->filter[0],
                        s->code_mask + 0, s->code_mask + 4, 1);

                    can_sja_dual_filter(&s->filter[1],
                        s->code_mask + 2, s->code_mask + 6, 1);

                    /* For SFF */
                    can_sja_dual_filter(&s->filter[2],
                        s->code_mask + 0, s->code_mask + 4, 0);

                    can_sja_dual_filter(&s->filter[3],
                        s->code_mask + 2, s->code_mask + 6, 0);

                    can_bus_client_set_filters(&s->bus_client, s->filter, 4);
                }

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            }
            break;

        case SJA_CMR: /* Command register. */
            if (0x01 & val) { /* Send transmission request. */
                buff2frame_pel(s->tx_buff, &frame);
                if (DEBUG_FILTER) {
                    can_display_msg(&frame);
                    fprintf(stderr, "\n");
                }

                /*
                 * Clear transmission complete status,
                 * and Transmit Buffer Status.
                 * write to the backends.
                 */
                s->status_pel &= ~(3 << 2);

                can_bus_client_send(&s->bus_client, &frame, 1);
                s->status_pel |= (3 << 2); /* Set transmission complete status, */
                                       /* and Transmit Buffer Status. */
                s->status_pel &= ~(1 << 5); /* Clear transmit status. */
                s->interrupt_pel |= 0x02;
                if (s->interrupt_en & 0x02) {
                    s->irq_raise(s->irq_opaque);
                }
            } else if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                tmp8 = s->rx_buff[s->rxbuf_start]; count = 0;
                if (tmp8 & (1 << 7)) { /* EFF */
                    count += 2;
                }
                count += 3;
                if (!(tmp8 & (1 << 6))) { /* DATA */
                    count += (tmp8 & 0x0f);
                }
                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;

                s->rx_cnt -= count;
                s->rxmsg_cnt--;
                if (s->rxmsg_cnt == 0) {
                    s->status_pel &= ~(1 << 0);
                    s->interrupt_pel &= ~(1 << 0);
                }
                if ((s->interrupt_en & 0x01) && (s->interrupt_pel == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            } else if (0x08 & val) { /* Clear data overrun */
                s->status_pel &= ~(1 << 1);
                s->interrupt_pel &= ~(1 << 3);
                if ((s->interrupt_en & 0x80) && (s->interrupt_pel == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            }
            break;
        case SJA_SR: /* Status register */
        case SJA_IR: /* Interrupt register */
            break; /* Do nothing */
        case SJA_IER: /* Interrupt enable register */
            s->interrupt_en = val;
            break;
        case 16: /* RX frame information addr16-28. */
            s->status_pel |= (1 << 5); /* Set transmit status. */
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    s->code_mask[addr - 16] = val;
                }
            } else { /* Operation mode */
                s->tx_buff[addr - 16] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            if ((s->control & 0x01) && ((val & 0x01) == 0)) {
                /* Go to operation mode from reset mode. */
                s->filter[0].can_id = (s->code << 3) & (0xff << 3);
                tmp = (~(s->mask << 3)) & (0xff << 3);
                tmp |= QEMU_CAN_EFF_FLAG; /* Only Basic CAN Frame. */
                s->filter[0].can_mask = tmp;
                can_bus_client_set_filters(&s->bus_client, s->filter, 1);

                s->rxmsg_cnt = 0;
                s->rx_cnt = 0;
            } else if (!(s->control & 0x01) && !(val & 0x01)) {
                can_sja_software_reset(s);
            }

            s->control = 0x1f & val;
            break;
        case SJA_BCAN_CMR: /* Command register, addr 1 */
            if (0x01 & val) { /* Send transmission request. */
                buff2frame_bas(s->tx_buff, &frame);
                if (DEBUG_FILTER) {
                    can_display_msg(&frame);
                    fprintf(stderr, "\n");
                }

                /*
                 * Clear transmission complete status,
                 * and Transmit Buffer Status.
                 */
                s->status_bas &= ~(3 << 2);

                /* write to the backends. */
                can_bus_client_send(&s->bus_client, &frame, 1);
                s->status_bas |= (3 << 2); /* Set transmission complete status, */
                                       /* and Transmit Buffer Status. */
                s->status_bas &= ~(1 << 5); /* Clear transmit status. */
                s->interrupt_bas |= 0x02;
                if (s->control & 0x04) {
                    s->irq_raise(s->irq_opaque);
                }
            } else if (0x04 & val) { /* Release Receive Buffer */
                if (s->rxmsg_cnt <= 0) {
                    break;
                }

                qemu_mutex_lock(&s->rx_lock);
                tmp8 = s->rx_buff[(s->rxbuf_start + 1) % SJA_RCV_BUF_LEN];
                count = 2 + (tmp8 & 0x0f);

                if (DEBUG_FILTER) {
                    int i;
                    fprintf(stderr, "\nRelease");
                    for (i = 0; i < count; i++) {
                        fprintf(stderr, " %02X", s->rx_buff[(s->rxbuf_start + i) %
                                        SJA_RCV_BUF_LEN]);
                    }
                    for (; i < 11; i++) {
                        fprintf(stderr, "   ");
                    }
                    fprintf(stderr, "==== cnt=%d, count=%d\n",
                            s->rx_cnt, count);
                }

                s->rxbuf_start += count;
                s->rxbuf_start %= SJA_RCV_BUF_LEN;
                s->rx_cnt -= count;
                s->rxmsg_cnt--;
                qemu_mutex_unlock(&s->rx_lock);

                if (s->rxmsg_cnt == 0) {
                    s->status_bas &= ~(1 << 0);
                    s->interrupt_bas &= ~(1 << 0);
                }
                if ((s->control & 0x02) && (s->interrupt_bas == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            } else if (0x08 & val) { /* Clear data overrun */
                s->status_bas &= ~(1 << 1);
                s->interrupt_bas &= ~(1 << 3);
                if ((s->control & 0x10) && (s->interrupt_bas == 0)) {
                    /* no other interrupts. */
                    s->irq_lower(s->irq_opaque);
                }
            }
            break;
        case 4:
            s->code = val;
            break;
        case 5:
            s->mask = val;
            break;
        case 10:
            s->status_bas |= (1 << 5); /* Set transmit status. */
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
            if ((s->control & 0x01) == 0) { /* Operation mode */
                s->tx_buff[addr - 10] = val; /* Store to TX buffer directly. */
            }
            break;
        case SJA_CDR:
            s->clock = val;
            break;
        }
    }
}

uint64_t can_sja_mem_read(CanSJA1000State *s, hwaddr addr, unsigned size)
{
    uint64_t temp = 0;

    DPRINTF("read addr 0x%x", (unsigned int)addr);

    if (addr > CAN_SJA_MEM_SIZE) {
        return 0;
    }

    if (s->clock & 0x80) { /* PeliCAN Mode */
        switch (addr) {
        case SJA_MOD: /* Mode register, addr 0 */
            temp = s->mode;
            break;
        case SJA_CMR: /* Command register, addr 1 */
            temp = 0x00; /* Command register, cannot be read. */
            break;
        case SJA_SR: /* Status register, addr 2 */
            temp = s->status_pel;
            break;
        case SJA_IR: /* Interrupt register, addr 3 */
            temp = s->interrupt_pel;
            s->interrupt_pel = 0;
            if (s->rxmsg_cnt) {
                s->interrupt_pel |= (1 << 0); /* Receive interrupt. */
                break;
            }
            s->irq_lower(s->irq_opaque);
            break;
        case SJA_IER: /* Interrupt enable register, addr 4 */
            temp = s->interrupt_en;
            break;
        case 5: /* Reserved */
        case 6: /* Bus timing 0, hardware related, not support now. */
        case 7: /* Bus timing 1, hardware related, not support now. */
        case 8: /*
                 * Output control register, hardware related,
                 * not supported for now.
                 */
        case 9: /* Test. */
        case 10: /* Reserved */
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            temp = 0x00;
            break;

        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
            if (s->mode & 0x01) { /* Reset mode */
                if (addr < 24) {
                    temp = s->code_mask[addr - 16];
                } else {
                    temp = 0x00;
                }
            } else { /* Operation mode */
                temp = s->rx_buff[(s->rxbuf_start + addr - 16) %
                       SJA_RCV_BUF_LEN];
            }
            break;
        case SJA_CDR:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
        }
    } else { /* Basic Mode */
        switch (addr) {
        case SJA_BCAN_CTR: /* Control register, addr 0 */
            temp = s->control;
            break;
        case SJA_BCAN_SR: /* Status register, addr 2 */
            temp = s->status_bas;
            break;
        case SJA_BCAN_IR: /* Interrupt register, addr 3 */
            temp = s->interrupt_bas;
            s->interrupt_bas = 0;
            if (s->rxmsg_cnt) {
                s->interrupt_bas |= (1 << 0); /* Receive interrupt. */
                break;
            }
            s->irq_lower(s->irq_opaque);
            break;
        case 4:
            temp = s->code;
            break;
        case 5:
            temp = s->mask;
            break;
        case 20:
            if (DEBUG_FILTER) {
                printf("Read   ");
            }
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
            temp = s->rx_buff[(s->rxbuf_start + addr - 20) % SJA_RCV_BUF_LEN];
            if (DEBUG_FILTER) {
                fprintf(stderr, " %02X", (unsigned int)(temp & 0xff));
            }
            break;
        case 31:
            temp = s->clock;
            break;
        default:
            temp = 0xff;
            break;
        }
    }
    DPRINTF("     %d bytes of 0x%lx from addr %d\n",
            size, (long unsigned int)temp, (int)addr);

    return temp;
}

int can_sja_can_receive(CanBusClientState *client)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);

    if (s->clock & 0x80) { /* PeliCAN Mode */
        if (s->mode & 0x01) { /* reset mode. */
            return 0;
        }
    } else { /* BasicCAN mode */
        if (s->control & 0x01) {
            return 0;
        }
    }

    return 1; /* always return 1, when operation mode */
}

ssize_t can_sja_receive(CanBusClientState *client, const qemu_can_frame *frames,
                        size_t frames_cnt)
{
    CanSJA1000State *s = container_of(client, CanSJA1000State, bus_client);
    static uint8_t rcv[SJA_MSG_MAX_LEN];
    int i;
    int ret = -1;
    const qemu_can_frame *frame = frames;

    if (frames_cnt <= 0) {
        return 0;
    }
    if (DEBUG_FILTER) {
        fprintf(stderr, "#################################################\n");
        can_display_msg(frame);
    }

    qemu_mutex_lock(&s->rx_lock); /* Just do it quickly :) */
    if (s->clock & 0x80) { /* PeliCAN Mode */
        s->status_pel |= (1 << 4); /* the CAN controller is receiving a message */

        if (can_sja_accept_filter(s, frame) == 0) {
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                fprintf(stderr, "     NOT\n");
            }
            goto fail;
        }

        ret = frame2buff_pel(frame, rcv);
        if (ret < 0) {
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                fprintf(stderr, "     ERR\n");
            }
            goto fail; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->status_pel |= (1 << 1); /* Overrun status */
            s->interrupt_pel |= (1 << 3);
            if (s->interrupt_en & (1 << 3)) { /* Overrun interrupt enable */
                s->irq_raise(s->irq_opaque);
            }
            s->status_pel &= ~(1 << 4);
            if (DEBUG_FILTER) {
                fprintf(stderr, "     OVER\n");
            }
            goto fail;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;
        if (DEBUG_FILTER) {
            fprintf(stderr, "     OK\n");
        }

        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->status_pel |= 0x01; /* Set the Receive Buffer Status. DS-p23 */
        s->interrupt_pel |= 0x01;
        s->status_pel &= ~(1 << 4);
        s->status_pel |= (1 << 0);
        if (s->interrupt_en & 0x01) { /* Receive Interrupt enable. */
            s->irq_raise(s->irq_opaque);
        }
    } else { /* BasicCAN mode */
        s->status_bas |= (1 << 4); /* the CAN controller is receiving a message */

        ret = frame2buff_bas(frame, rcv);
        if (ret < 0) {
            s->status_bas &= ~(1 << 4);
            if (DEBUG_FILTER) {
                fprintf(stderr, "     NOT\n");
            }
            goto fail; /* maybe not support now. */
        }

        if (s->rx_cnt + ret > SJA_RCV_BUF_LEN) { /* Data overrun. */
            s->status_bas |= (1 << 1); /* Overrun status */
            s->status_bas &= ~(1 << 4);
            s->interrupt_bas |= (1 << 3);
            if (s->control & (1 << 4)) { /* Overrun interrupt enable */
                s->irq_raise(s->irq_opaque);
            }
            if (DEBUG_FILTER) {
                fprintf(stderr, "     OVER\n");
            }
            goto fail;
        }
        s->rx_cnt += ret;
        s->rxmsg_cnt++;

        if (DEBUG_FILTER) {
            fprintf(stderr, "     OK\n");
            fprintf(stderr, "RCV B ret=%2d, ptr=%2d cnt=%2d msg=%2d\n",
                   ret, s->rx_ptr, s->rx_cnt, s->rxmsg_cnt);
        }

        for (i = 0; i < ret; i++) {
            s->rx_buff[(s->rx_ptr++) % SJA_RCV_BUF_LEN] = rcv[i];
        }
        s->rx_ptr %= SJA_RCV_BUF_LEN; /* update the pointer. */

        s->status_bas |= 0x01; /* Set the Receive Buffer Status. DS-p15 */
        s->status_bas &= ~(1 << 4);
        s->interrupt_bas |= 0x01;
        if (s->control & 0x02) { /* Receive Interrupt enable. */
            s->irq_raise(s->irq_opaque);
        }
    }
    ret = 1;
fail:
    qemu_mutex_unlock(&s->rx_lock);

    return ret;
}

static CanBusClientInfo can_sja_bus_client_info = {
    .can_receive = can_sja_can_receive,
    .receive = can_sja_receive,
    .cleanup = NULL,
    .poll = NULL
};


int can_sja_connect_to_bus(CanSJA1000State *s, CanBusState *bus)
{
    s->bus_client.info = &can_sja_bus_client_info;

    if (can_bus_insert_client(bus, &s->bus_client) < 0) {
        return -1;
    }

    return 0;
}

void can_sja_disconnect(CanSJA1000State *s)
{
    can_bus_remove_client(&s->bus_client);
}

int can_sja_init(CanSJA1000State *s, CanSJAIrqRaiseLower *irq_raise,
                 CanSJAIrqRaiseLower *irq_lower, void *irq_opaque)
{
    qemu_mutex_init(&s->rx_lock);

    s->irq_raise = irq_raise;
    s->irq_lower = irq_lower;
    s->irq_opaque = irq_opaque;

    s->irq_lower(s->irq_opaque);

    can_sja_hardware_reset(s);

    return 0;
}

void can_sja_exit(CanSJA1000State *s)
{
    qemu_mutex_destroy(&s->rx_lock);
}

const VMStateDescription vmstate_qemu_can_filter = {
    .name = "qemu_can_filter",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(can_id, qemu_can_filter),
        VMSTATE_UINT32(can_mask, qemu_can_filter),
        VMSTATE_END_OF_LIST()
    }
};

/* VMState is needed for live migration of QEMU images */
const VMStateDescription vmstate_can_sja = {
    .name = "can_sja",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(mode, CanSJA1000State),

        VMSTATE_UINT8(status_pel, CanSJA1000State),
        VMSTATE_UINT8(interrupt_pel, CanSJA1000State),
        VMSTATE_UINT8(interrupt_en, CanSJA1000State),
        VMSTATE_UINT8(rxmsg_cnt, CanSJA1000State),
        VMSTATE_UINT8(rxbuf_start, CanSJA1000State),
        VMSTATE_UINT8(clock, CanSJA1000State),

        VMSTATE_BUFFER(code_mask, CanSJA1000State),
        VMSTATE_BUFFER(tx_buff, CanSJA1000State),

        VMSTATE_BUFFER(rx_buff, CanSJA1000State),

        VMSTATE_UINT32(rx_ptr, CanSJA1000State),
        VMSTATE_UINT32(rx_cnt, CanSJA1000State),

        VMSTATE_UINT8(control, CanSJA1000State),

        VMSTATE_UINT8(status_bas, CanSJA1000State),
        VMSTATE_UINT8(interrupt_bas, CanSJA1000State),
        VMSTATE_UINT8(code, CanSJA1000State),
        VMSTATE_UINT8(mask, CanSJA1000State),

        VMSTATE_STRUCT_ARRAY(filter, CanSJA1000State, 4, 0,
                             vmstate_qemu_can_filter, qemu_can_filter),


        VMSTATE_END_OF_LIST()
    }
};
