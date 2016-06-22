#ifndef QEMU_ID_H
#define QEMU_ID_H 1

typedef enum IdSubSystems {
    ID_QDEV,
    ID_BLOCK,
    ID_JOB,
    ID_MAX      /* last element, used as array size */
} IdSubSystems;

char *id_generate(IdSubSystems id);
bool id_wellformed(const char *id);

#endif
