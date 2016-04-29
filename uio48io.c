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
//	Name	 : uio48io.c
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
//								Support for up to 4 devices
//
///****************************************************************************

#include <stdio.h>
#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */

// Include the WinSystems UIO48 definitions
#include "uio48.h"    

//**************************************************************************
//		USER LIBRARY FUNCTIONS
//**************************************************************************

// device handles
int handle[MAX_CHIPS] = {0,0,0,0};

// the names of our device nodes
char *device_id[MAX_CHIPS]={"/dev/uio48a",
							"/dev/uio48b",
							"/dev/uio48c",
							"/dev/uio48d"};

//
//------------------------------------------------------------------------
//
// read_bit - Reads the bit value of an input point
//
// Description:		This function will read the bit value of a single input
//					input point. It does this by calling the UIO48
//					device drivers IOCTL_READ_BIT method and returning
//					the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_READ_BIT call (the bit value)
//
//------------------------------------------------------------------------
//
int read_bit(int chip_number, int bit_number)
{
	int c;

    --chip_number;
   
    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_READ_BIT, bit_number);
    
    return c;
}

//
//------------------------------------------------------------------------
//
// write_bit - Write the bit value to a single output point
//
// Description:		This function will write the bit value of a single
//					output point. It does this by calling the UIO48
//					device drivers IOCTL_WRITE_BIT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//			val			The value to write to the bit (0 or 1)
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_WRITE_BIT call
//
//------------------------------------------------------------------------
//
int write_bit(int chip_number, int bit_number, int val)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_WRITE_BIT, bit_number << 8 | val);

    return c;
}

//
//------------------------------------------------------------------------
//
// set_bit - Sets a single output point
//
// Description:		This function will set a single	output point.
//					It does this by calling the UIO48
//					device drivers IOCTL_SET_BIT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_SET_BIT call
//
//------------------------------------------------------------------------
//
int set_bit(int chip_number, int bit_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_SET_BIT, bit_number);

    return c;
}

//
//------------------------------------------------------------------------
//
// clr_bit - Clear a single output point
//
// Description:		This function will clear a single output point.
//					It does this by calling the UIO48
//					device drivers IOCTL_CLR_BIT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_CLR_BIT call
//
//------------------------------------------------------------------------
//
int clr_bit(int chip_number, int bit_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_CLR_BIT, bit_number);

    return c;
}

//
//------------------------------------------------------------------------
//
// enab_int - Enable notification of an input points change to the specified
//				state.
//
// Description:		This function enables notification of a single input points
//					change to the specified state. It does this by calling
//					the UIO48 device drivers IOCTL_ENAB_INT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//			polarity	The state to look for (0 or 1)
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_ENAB_INT call
//
//------------------------------------------------------------------------
//
int enab_int(int chip_number, int bit_number, int polarity)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_ENAB_INT, bit_number<<8 | polarity);

    return c;
}

//
//------------------------------------------------------------------------
//
// disab_int - Disable notification on an input points state change.
//
// Description:		This function disable interrupts on a single input points
//					state change. It does this by calling
//					the UIO48 device drivers IOCTL_DISAB_INT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//			polarity	The state to look for (0 or 1)
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_DISAB_INT call
//
//------------------------------------------------------------------------
//
int disab_int(int chip_number, int bit_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_DISAB_INT, bit_number);

    return c;
}

//
//------------------------------------------------------------------------
//
// clr_int - Acknowledge the notification of an input points state change.
//
// Description:		This function acknowldedges notification of a single
//					input points state change. It does this by calling
//					the UIO48 device drivers IOCTL_CLR_INT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			bit_number	The 1 based index of the bit
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_CLR_INT call
//
//------------------------------------------------------------------------
//
int clr_int(int chip_number, int bit_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_CLR_INT, bit_number);

    return c;
}

//
//------------------------------------------------------------------------
//
// get_int - Poll for notification of an input points state change.
//
// Description:		This function polls for notification of a single
//					input points state change. It does this by calling
//					the UIO48 device drivers IOCTL_GET_INT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_GET_INT call
//
//------------------------------------------------------------------------
//
int get_int(int chip_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_GET_INT, 0);

    return c;
}

//
//------------------------------------------------------------------------
//
// wait_int - Wait for the notification of an input points state change.
//
// Description:		This function waits for notification of a single
//					input points state change. It does this by calling
//					the UIO48 device drivers IOCTL_WAIT_INT method and
//					returning the result
//
// Arguments:
//			chip_number	The 1 based index of the chip
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_WAIT_INT call
//
//------------------------------------------------------------------------
//
int wait_int(int chip_number)
{
	int c;

    --chip_number;

    if(check_handle(chip_number))   /* Check for chip available */
		return -1;

    c = ioctl(handle[chip_number], IOCTL_WAIT_INT, 0);

    return c;
}

//
//------------------------------------------------------------------------
//
// read_int_pending - Read the INT_PENDING register.
//
// Description:		This function reads the 3-bit interrupt pending code 
//					from the INT_PENDING Register.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_READ_INT_PENDING call
//
//------------------------------------------------------------------------
//
int read_int_pending(int chip_number)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_READ_PORT, 6));
}

//
//------------------------------------------------------------------------
//
// clr_int_id - Clear one of the INT_ID registers.
//
// Description:		This function writes a zero to the specified INT_IDx
//					register. This has the effect of clearing all pending
//					interrupts for this port.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			port_number	The index of the port to clear
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_CLR_INT_ID call
//
//------------------------------------------------------------------------
//
int clr_int_id(int chip_number, int port_number)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_CLR_INT_ID, port_number));
}

//
//------------------------------------------------------------------------
//
// read_byte - Read a byte from a specified port.
//
// Description:		This function returns the value from the register
//					specified by the port_number.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			port_number	The register offset to read
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_READ_PORT call
//
//------------------------------------------------------------------------
//
int read_byte(int chip_number, int port_number)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_READ_PORT, port_number));
}

//
//------------------------------------------------------------------------
//
// write_byte - Write a byte to a specified port.
//
// Description:		This function writes a specific value to the register
//					specified by the port_number.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			port_number	The register offset to write
//			val			The value to write
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_WRITE_PORT call
//
//------------------------------------------------------------------------
//
int write_byte(int chip_number, int port_number, int val)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_WRITE_PORT, ((port_number << 8) | val) ));
}

//
//------------------------------------------------------------------------
//
// lock_port - Lock one of the six I/O ports.
//
// Description:		This function locks a specific I/O port and prevents
//					further writes to the port. It sets the correspnding 
//					bit in the PAGE/LOCK Register.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			port_number	The index of the port to clear
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_LOCK_PORT call
//
//------------------------------------------------------------------------
//
int lock_port(int chip_number, int port_number)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_LOCK_PORT, port_number));
}

//
//------------------------------------------------------------------------
//
// unlock_port - Unlock one of the six I/O ports.
//
// Description:		This function unlocks a specific I/O port and permits
//					further writes to the port. It sets the correspnding 
//					bit in the PAGE/LOCK Register.
//
// Arguments:
//			chip_number	The 1 based index of the chip
//			port_number	The index of the port to clear
//
// Returns:
//			-1		If the chip does not exist or it's handle is invalid
//	or		The result of the IOCTL_UNLOCK_PORT call
//
//------------------------------------------------------------------------
//
int unlock_port(int chip_number, int port_number)
{
    if(check_handle(chip_number-1))		// Check for chip available
		return(-1);						// Return -1 if not

	// Call the drivers IOCTL method and return the result
	return(ioctl(handle[chip_number-1], IOCTL_UNLOCK_PORT, port_number));
}

//
//------------------------------------------------------------------------
//
// check_handle - Checks that a handle to the appropriate device file
//					exists. If it does not a file open is performed.
//
// Description:
//
// Arguments:
//			chip_number	The 1 based index of the chip
//
// Returns:
//			0		if handle is valid
//			-1		If the chip does not exist or it's handle is invalid
//
//------------------------------------------------------------------------
//
int check_handle(int chip_number)
{
    if(handle[chip_number] > 0)	// If it's already a valid handle
		return 0;

    if(handle[chip_number] == -1)	// If it's already been tried
		return -1;

	// Try opening the device file, in case it hasn't been opened yet
    handle[chip_number] = open(device_id[chip_number], O_RDWR);

    if(handle[chip_number] > 0)	// If it's now a validopen handle
		return 0;
    
    handle[chip_number] = -1;
		return -1;
}
