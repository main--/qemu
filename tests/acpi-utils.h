/*
 * Utilities for working with ACPI tables
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TEST_ACPI_UTILS_H
#define TEST_ACPI_UTILS_H

#include "hw/acpi/acpi-defs.h"
#include "libqtest.h"

/* DSDT and SSDTs format */
typedef struct {
    AcpiTableHeader header;
    gchar *aml;            /* aml bytecode from guest */
    gsize aml_len;
    gchar *aml_file;
    gchar *asl;            /* asl code generated from aml */
    gsize asl_len;
    gchar *asl_file;
    bool tmp_files_retain;   /* do not delete the temp asl/aml */
} AcpiSdtTable;

#define ACPI_READ_FIELD(qts, field, addr)      \
    do {                                       \
        switch (sizeof(field)) {               \
        case 1:                                \
            field = readb(qts, addr);          \
            break;                             \
        case 2:                                \
            field = readw(qts, addr);          \
            break;                             \
        case 4:                                \
            field = readl(qts, addr);          \
            break;                             \
        case 8:                                \
            field = readq(qts, addr);          \
            break;                             \
        default:                               \
            g_assert(false);                   \
        }                                      \
        addr += sizeof(field);                 \
    } while (0);

#define ACPI_READ_ARRAY_PTR(qts, arr, length, addr)      \
    do {                                                 \
        int idx;                                         \
        for (idx = 0; idx < length; ++idx) {             \
            ACPI_READ_FIELD(qts, arr[idx], addr);        \
        }                                                \
    } while (0);

#define ACPI_READ_ARRAY(qts, arr, addr)                                 \
    ACPI_READ_ARRAY_PTR(qts, arr, sizeof(arr) / sizeof(arr[0]), addr)

#define ACPI_READ_TABLE_HEADER(qts, table, addr)                 \
    do {                                                         \
        ACPI_READ_FIELD(qts, (table)->signature, addr);          \
        ACPI_READ_FIELD(qts, (table)->length, addr);             \
        ACPI_READ_FIELD(qts, (table)->revision, addr);           \
        ACPI_READ_FIELD(qts, (table)->checksum, addr);           \
        ACPI_READ_ARRAY(qts, (table)->oem_id, addr);             \
        ACPI_READ_ARRAY(qts, (table)->oem_table_id, addr);       \
        ACPI_READ_FIELD(qts, (table)->oem_revision, addr);       \
        ACPI_READ_ARRAY(qts, (table)->asl_compiler_id, addr);    \
        ACPI_READ_FIELD(qts, (table)->asl_compiler_revision, addr);     \
    } while (0);

#define ACPI_ASSERT_CMP(actual, expected) do { \
    uint32_t ACPI_ASSERT_CMP_le = cpu_to_le32(actual); \
    char ACPI_ASSERT_CMP_str[5] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &ACPI_ASSERT_CMP_le, 4); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)

#define ACPI_ASSERT_CMP64(actual, expected) do { \
    uint64_t ACPI_ASSERT_CMP_le = cpu_to_le64(actual); \
    char ACPI_ASSERT_CMP_str[9] = {}; \
    memcpy(ACPI_ASSERT_CMP_str, &ACPI_ASSERT_CMP_le, 8); \
    g_assert_cmpstr(ACPI_ASSERT_CMP_str, ==, expected); \
} while (0)

#define ACPI_READ_GENERIC_ADDRESS(qts, field, addr)       \
    do {                                                  \
        ACPI_READ_FIELD(qts, (field).space_id, addr);     \
        ACPI_READ_FIELD(qts, (field).bit_width, addr);    \
        ACPI_READ_FIELD(qts, (field).bit_offset, addr);   \
        ACPI_READ_FIELD(qts, (field).access_width, addr); \
        ACPI_READ_FIELD(qts, (field).address, addr);      \
    } while (0);


uint8_t acpi_calc_checksum(const uint8_t *data, int len);
uint32_t acpi_find_rsdp_address(QTestState *qts);
void acpi_parse_rsdp_table(QTestState *qts, uint32_t addr,
                           AcpiRsdpDescriptor *rsdp_table);

#endif  /* TEST_ACPI_UTILS_H */
