//*****************************************************************************
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
//*****************************************************************************
//
//	Name	 : flash.c
//
//	Project	 : UIO48 Sample Application Program
//
//	Author	 : Paul DeMetrotion
//
//*****************************************************************************
//
//	  Date		Revision	                Description
//	--------	--------	---------------------------------------------
//	07/21/10	  3.0		Original Release	
//
//*****************************************************************************

// Remove the comments from around this define for the UIO96 or for any
// two chip scenario
// #define TWO_CHIPS 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "uio48.h"

main(int argc, char *argv[])
{
	int x;

	if(read_bit(1,1) < 0)	// Just a test for availability
	{	
	    fprintf(stderr, "Can't access device UIO48A - Aborting\n");
	    exit(1);
	}

#ifdef TWO_CHIPS
	if(read_bit(2,1) < 0)	// Test this one too 
	{
	    fprintf(stderr, "Can't access device UIO48B - Aborting\n");
	    exit(1);
	}
#endif

	while(1)
	{
		for(x=1; x<=48; x++)
		{
		    set_bit(1,x);	// Turn on the LED
		    usleep(50000);
		    clr_bit(1,x);	// Turn off the LED
		}

#ifdef TWO_CHIPS
		for(x=1; x<=48; x++) 
		{
		    set_bit(2,x);	// Turn on the LED
		    usleep(50000);
		    clr_bit(2,x);	// Turn off the LED
		}
#endif
	}
}
