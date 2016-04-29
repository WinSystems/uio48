//
//------------------------------------------------------------------------
// WinSystems, Inc. UIO48 Linux Device Driver
// 
// ioctltest.c - UIO48 Sample Application Program
//
//------------------------------------------------------------------------
//
// History:
//
//	When		Who	What
//------------------------------------------------------------------------
//	07/21/2010	PBD	Original	
//------------------------------------------------------------------------
//

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// Include the WinSystems UIO48 definitions
#include "uio48.h"

/***************************** code **********************************/

int main(int argc, char *argv[])
{
	if(read_bit(1,1) < 0) {	// Just a test for availability
	    fprintf(stderr, "Can't access device UIO48A - Aborting\n");
	    exit(1);
	}
	else
	    printf("\nTest program to display WS16C48 Registers\n\n");

	printf ("Port 0      : %02Xh \n", read_byte(1, 0));
	printf ("Port 1      : %02Xh \n", read_byte(1, 1));
	printf ("Port 2      : %02Xh \n", read_byte(1, 2));
	printf ("Port 3      : %02Xh \n", read_byte(1, 3));
	printf ("Port 4      : %02Xh \n", read_byte(1, 4));
	printf ("Port 5      : %02Xh \n", read_byte(1, 5));
	printf ("Int Pending : %02Xh \n", read_byte(1, 6));
	printf ("Revision    : %04Xh \n", read_rev_reg(1));

	lock_port(1, 0);
	lock_port(1, 2);
	lock_port(1, 4);

	printf ("Page/Lock   : %02Xh -> ", read_byte(1, 7));

	unlock_port(1, 0);
	lock_port(1, 1);
	unlock_port(1, 2);
	lock_port(1, 3);
	unlock_port(1, 4);
	lock_port(1, 5);

	printf ("%02Xh -> ", read_byte(1, 7));

	unlock_port(1, 1);
	unlock_port(1, 3);
	unlock_port(1, 5);

	printf ("%02Xh \n\n", read_byte(1, 7));

	return(0);
}

