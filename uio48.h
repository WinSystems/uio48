///****************************************************************************
//	
//	Copyright 2011 by WinSystems Inc.
//
//	Permission is hereby granted to the purchaser of WinSystems GPIO cards 
//	and CPU products incorporating a GPIO device, to distribute any binary 
//	file or files compiled using this source code directly or in any work 
//	derived by the user from this file. In no case may the source code, 
//	original or derived from this file, be distributed to any third party 
//	except by explicit permission of WinSystems. This file is distributed 
//	on an "As-is" basis and no warranty as to performance or fitness of pur-
//	poses is expressed or implied. In no case shall WinSystems be liable for 
//	any direct or indirect loss or damage, real or consequential resulting 
//	from the usage of this source code. It is the user's sole responsibility 
//	to determine fitness for any considered purpose.
//
///****************************************************************************
//
//	Name	 : uio48.h
//
//	Project	 : UIO48 Linux Device Driver
//
//	Author	 : Paul DeMetrotion
//
///****************************************************************************
//
//	  Date		Revision	                Description
//	--------	--------	---------------------------------------------
//	07/21/10	  3.0		Original Release	
//	06/14/11	  4.0		Changes:
//								Added IOCTL_NUM
//								Support for up to 4 devices
//
///****************************************************************************

#ifndef CHARDEV_H
  #define CHARDEV_H

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

#endif
