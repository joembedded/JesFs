#ifndef JESFS_SHELL_H
#define JESFS_SHELL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t jesfs_shell(uint8_t flags, char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* JESFS_SHELL_H */
