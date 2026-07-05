#ifndef KERNEL_UNISTD_H
#define KERNEL_UNISTD_H

int isatty(int fd);
int access(const char *path, int mode);

#endif
