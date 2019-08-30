/*
 * This code implements an interface to create and fill jitdump files. These files
 * store information used by Linux Perf to enhance the presentation of jitted
 * code and to allow the disassembly of jitted code.
 *
 * The jitdump file specification can be found in the Linux Kernel Source tree:
 *    linux/tools/perf/Documentation/jitdump-specification.txt
 *
 * https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/jitdump-specification.txt
 */

#include "qemu/osdep.h"

#include <sys/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <elf.h>

#include "disas/disas.h"
#include "jitdump.h"
#include "qemu-common.h"

struct jitheader {
    uint32_t magic;     /* characters "jItD" */
    uint32_t version;   /* header version */
    uint32_t total_size;/* total size of header */
    uint32_t elf_mach;  /* elf mach target */
    uint32_t pad1;      /* reserved */
    uint32_t pid;       /* JIT process id */
    uint64_t timestamp; /* timestamp */
    uint64_t flags;     /* flags */
};

enum jit_record_type {
    JIT_CODE_LOAD       = 0,
    JIT_CODE_MOVE       = 1,
    JIT_CODE_DEBUG_INFO = 2,
    JIT_CODE_CLOSE      = 3,

    JIT_CODE_MAX,
};

/* record prefix (mandatory in each record) */
struct jr_prefix {
    uint32_t id;
    uint32_t total_size;
    uint64_t timestamp;
};

struct jr_code_load {
    struct jr_prefix p;

    uint32_t pid;
    uint32_t tid;
    uint64_t vma;
    uint64_t code_addr;
    uint64_t code_size;
    uint64_t code_index;
};

struct jr_code_close {
    struct jr_prefix p;
};

struct jr_code_move {
    struct jr_prefix p;

    uint32_t pid;
    uint32_t tid;
    uint64_t vma;
    uint64_t old_code_addr;
    uint64_t new_code_addr;
    uint64_t code_size;
    uint64_t code_index;
};

FILE *dumpfile;
void *perf_marker;

static uint64_t get_timestamp(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
        fprintf(stderr, "No support for CLOCK_MONOTONIC! -perf cannot be used!\n");
        exit(1);
    }
    return (uint64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static uint32_t get_e_machine(void)
{
    uint32_t e_machine = EM_NONE;
    Elf64_Ehdr elf_header;
    FILE *exe = fopen("/proc/self/exe", "r");

    if (exe == NULL) {
        return e_machine;
    }

    if (fread(&elf_header, sizeof(Elf64_Ehdr), 1, exe) != 1) {
        goto end;
    }

    e_machine = elf_header.e_machine;

end:
    fclose(exe);
    return e_machine;
}

void start_jitdump_file(void)
{
    gchar *dumpfile_name = g_strdup_printf("./jit-%d.dump", getpid());
    dumpfile = fopen(dumpfile_name, "w+");

    /* 'Perf record' saves mmaped files during the execution of a program and
     * 'perf inject' iterate over them to reconstruct all used/executed binary.
     * So, we create a mmap with the path of our jitdump that is processed
     * and used by 'perf inject' to reconstruct jitted binaries.
     */
    perf_marker = mmap(NULL, sysconf(_SC_PAGESIZE),
                          PROT_READ | PROT_EXEC,
                          MAP_PRIVATE,
                          fileno(dumpfile), 0);

    if (perf_marker == MAP_FAILED) {
        printf("Failed to create mmap marker file for perf %d\n", fileno(dumpfile));
        fclose(dumpfile);
        return;
    }

    g_free(dumpfile_name);

    struct jitheader header;
    header.magic = 0x4A695444;
    header.version = 1;
    header.elf_mach = get_e_machine();
    header.total_size = sizeof(struct jitheader);
    header.pid = getpid();
    header.timestamp = get_timestamp();
    header.flags = 0;

    fwrite(&header, header.total_size, 1, dumpfile);

    fflush(dumpfile);
}

void append_load_in_jitdump_file(TranslationBlock *tb)
{
    gchar *func_name = g_strdup_printf("TB virt:0x"TARGET_FMT_lx, tb->pc);

    struct jr_code_load load_event;
    load_event.p.id = JIT_CODE_LOAD;
    load_event.p.total_size =
        sizeof(struct jr_code_load) + strlen(func_name) + 1 + tb->tc.size;
    load_event.p.timestamp = get_timestamp();
    load_event.pid = getpid();
    load_event.tid = syscall(SYS_gettid);
    load_event.vma = tb->pc;
    load_event.code_addr = (uint64_t) tb->tc.ptr;
    load_event.code_size = tb->tc.size;
    load_event.code_index = tb->pc;

    fwrite(&load_event, sizeof(struct jr_code_load), 1, dumpfile);
    fwrite(func_name, strlen(func_name) + 1, 1, dumpfile);
    fwrite(tb->tc.ptr, tb->tc.size, 1, dumpfile);

    g_free(func_name);
    fflush(dumpfile);
}

void close_jitdump_file(void)
{
    fclose(dumpfile);
    if (perf_marker != MAP_FAILED) {
        munmap(perf_marker, sysconf(_SC_PAGESIZE));
    }
}

bool is_jitdump_enabled;

void enable_jitdump(void)
{
    is_jitdump_enabled = true;
}

bool jitdump_enabled(void)
{
    return is_jitdump_enabled;
}
