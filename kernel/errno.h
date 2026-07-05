#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

#define ENOENT 2
#define EIO 5
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define ENOSPC 28
#define EISDIR 21

extern int errno;

#endif
