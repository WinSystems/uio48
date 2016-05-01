/*
 * uio48.h: UIO48 Digital I/O Driver
 *
 * (C) Copyright 2011, 2016 by WinSystems, Inc.
 * Author: Paul DeMetrotion <pdemetrotion@winsystems.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef __UIO48_H
#define __UIO48_H

#include <linux/ioctl.h> 

#define IOCTL_NUM 't'

#define MAX_CHIPS 4

#define SUCCESS 0

/* Read the Port fromn the UIO48 */
#define IOCTL_READ_PORT _IOWR(IOCTL_NUM, 1, int)

/* Write to the UIO48 Port */
#define IOCTL_WRITE_PORT _IOWR(IOCTL_NUM, 2, int)

/* Bit read function */
#define IOCTL_READ_BIT _IOWR(IOCTL_NUM, 3, int)

/* Write bit function */
#define IOCTL_WRITE_BIT _IOWR(IOCTL_NUM, 4, int)

/* Set Bit function */
#define IOCTL_SET_BIT _IOWR(IOCTL_NUM, 5, int)

/* Clear bit function */
#define IOCTL_CLR_BIT _IOWR(IOCTL_NUM, 6, int)

/* Enab_int function */
#define IOCTL_ENAB_INT _IOWR(IOCTL_NUM, 7, char *)

/* DISAB_INT function */
#define IOCTL_DISAB_INT _IOWR(IOCTL_NUM, 8, int)

/* CLR_INT function */
#define IOCTL_CLR_INT _IOWR(IOCTL_NUM, 9, int)

/* GET_INT function */
#define IOCTL_GET_INT _IOWR(IOCTL_NUM, 10, int)

/* WAIT_INT function */
#define IOCTL_WAIT_INT _IOWR(IOCTL_NUM, 11, int)

/* CLR_INT_ID function */
#define	IOCTL_CLR_INT_ID _IOWR(IOCTL_NUM, 12, int)

/* LOCK_PORT function */
#define	IOCTL_LOCK_PORT	_IOWR(IOCTL_NUM, 13, int)

/* UNLOCK_PORT function */
#define	IOCTL_UNLOCK_PORT _IOWR(IOCTL_NUM, 14, int)

#endif /* __UIO48_H */
