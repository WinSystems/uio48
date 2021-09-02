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
//	Name	 : poll.c
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

// Remove the comments from the following define for UIO96 or for other
// two chip scenarios
// #define TWO_CHIPS 1

#include <stdio.h>
#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdlib.h>
#include <pthread.h>

#include "uio48.h"    

int read_bit(int chip_number, int bit_number);
int enab_int(int chip_number, int bit_number, int polarity);
int clr_int(int chip_number, int bit_number);
int get_int(int chip_number);
int wait_int(int chip_number);

// These two functions will be sub-processes using the Posix threads 
// capability of Linux. These two threads will simulate a type of 
// Interrupt service routine in that they will start up and then suspend 
// until an interrupt occurs and the driver awakens them.
void thread_function(void *arg);

// Event count, counts the number of events we've handled
volatile int event_count;
volatile int exit_flag = 0;
char line[80];

int main(int argc, char *argv[])
{
	int res, res1;
	pthread_t a_thread;
	int c;
	int x;

	// Do a read_bit to test for port availability
	c = read_bit(1,1);
	if(c < 0)
	{
		printf("Unable to access device UIO48A - Aborting\n");
		exit(1);
	}

	// Here, we'll enable all 24 bits for falling edge interrupts on both 
	// chips. We'll also make sure that they're ready and armed by 
	// explicitly calling the clr_int() function.
    for(x=1; x < 25; x++)
    {
        enab_int(1,x,0);
		clr_int(1,x);
    }

    // We'll also clear out any events that are queued up within the 
    // driver and clear any pending interrupts
    while((x= get_int(1)))
    {
		printf("Clearing interrupt on Chip 1 bit %d\n",x);
		clr_int(1,x);
    }

    // Now the sub-threads will be started
    printf("Splitting off polling process\n");

    res = pthread_create(&a_thread,NULL,(void *)&thread_function,NULL);
    if(res != 0) 
	{
		perror("Thread creation failed");
		exit(EXIT_FAILURE);
    }

    // Both threads are now running in the background. They'll execute up
    // to the point were there are no interrupts and suspend. We as their
    // parent continue on. The nice thing about POSIX threads is that we're 
    // all in the same data space the parent and the children so we can 
    // share data directly. In this program we share the event_count 
    // variable.

    // We'll continue on in this loop until we're terminated
    while(1)

    {
		// Print Something so we know the foreground is alive
		printf("**\n");

		// The foreground will now wait for an input from the console
		// We could actually go on and do anything we wanted to at this 
		// point.
		printf("Press \'q' or \'Q' followed by the \'ENTER' key to exit polling loop\n");
		fgets(line,75,stdin);

		//sleep(2);

		if(line[0] == 'q' || line[0] == 'Q'){
		    printf("Exiting Loop\n");
		    break;
		}
		// Here's the actual exit. If we hit 'Q' and Enter. The program
		// terminates.
    }

    // This flag is a shared variable that the children can look at to
	// know we're finished and they can exit too.
    exit_flag = 1;

    // Display our event count total
    printf("Event count = %05d\r",event_count);

    printf("\n\nAttempting to cancel subthread\n");
    
    // If out children are not in a position to see the exit_flag, we
    // will use a more forceful technique to make sure they terminate with
    // us. If we leave them hanging and we try to re-run the program or
    // if another program wants to talk to the device they may be locked
    // out. This way everything cleans up much nicer.
    if(pthread_cancel(a_thread) != 0){
	printf("Could not cancel thread\n");
    }

    printf("\nExiting Now\n");
    printf("Generate another event to exit\n");

    fflush(NULL);
}

// This is the first of the sub-processes. For the purpose of this
// example, it does nothing but wait for an interrupt to be active on
// chip 1 and then reports that fact via the console. It also
// increments the shared data variable event_count.
void thread_function(void *arg)
{
	int c;

	while(1)
	{
	    pthread_testcancel();

	    if(exit_flag)
		break;

	    // This call will put THIS process to sleep until either an
	    // interrupt occurs or a terminating signal is sent by the 
	    // parent or the system.
	    c = wait_int(1);

	    // We check to see if it was a real interrupt instead of a
	    // termination request.
	    printf("Event sense occured on Chip 1 bit %d\n",c);
	    ++event_count;
	}
}
