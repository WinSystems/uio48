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
//	Name	 : test.c
//
//	Project	 : UIO48 Sample Application Program
//
//	Author	 : Luke DeMetrotion
//
//*****************************************************************************
//
//	  Date		Revision	                Description
//	--------	--------	---------------------------------------------
//	07/2/18	      1.0		             Original Release	
//
//*****************************************************************************
//*****************************************************************************


//*****************************************************************************


//*****************************************************************************


//*****************************************************************************




#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "uio48io.c"
#include "uio48.h"

// Function prototypes for local functions
int RSC_check();
int write_check();
int intTest();
int Byte_test();
int lock_test();

int main(int argc, char *argv[])
{
   if(RSC_check() == 1)
        fprintf(stderr, "PASS\n");
   else
        fprintf(stderr, "FAIl\n\n");

   if(write_check() == 1)
        fprintf(stderr, "PASS\n");
   else
        fprintf(stderr, "FAIl\n\n");

   if(intTest() ==1)
        fprintf(stderr, "PASS\n");
   else
        fprintf(stderr, "FAIL\n");
   if(Byte_test() ==1)
        fprintf(stderr, "PASS\n");
   else
        fprintf(stderr, "FAIL\n");
   if(lock_test() ==1)
        fprintf(stderr, "PASS\n");
   else
   {
        fprintf(stderr, "FAIL\n");
        for(int i = 0; i <= 5; i++)
        unlock_port(1,i);
   }
   return 1; 
}//end of main
//
//------------------------------------------------------------------------
//
// Description:         This function tests the uio48io functions set_bit,
//                      clr_bit,and read_bit validating their functionality.  
//
// Arguments:           None.
//
// Returns:             -1 if failed.
//                       1 if succesful.
//
//------------------------------------------------------------------------
//
int RSC_check()
{
    fprintf(stderr, "\n\nTesting read_bit, set_bit, and clr_bit....");
    
    //sets the bits and confrims their value, then clears and confirms their value.  
    for(int i = 1; i <= 8; i++)
    {
        set_bit(1, i);
        
        if(read_bit(1, i + 8) != 1)
            return(-1);

        clr_bit(1, i);
        clr_bit(1, i+8);

        if(read_bit(1, i+8) != 0)
            return(-1);

        set_bit(1, i+8);

        if(read_bit(1, i) != 1)
            return(-1);  
    }//bits 1-16

    for(int i = 17; i <= 20; i++)
        {
        set_bit(1, i);
        
        if(read_bit(1, i + 4) != 1)
            return(-1);

        clr_bit(1, i);
        clr_bit(1, i+4);
        if(read_bit(1, i+4) != 0)
            return(-1);

        set_bit(1, i+4);

        if(read_bit(1, i) != 1)
            return(-1);
    }//bits 17-24

    return 1;
}//end of RSC_check
//
//------------------------------------------------------------------------
//
// Description:         This function tests the uio48io functions write_bit
//                      validating it's functionality.  
//
// Arguments:           None.
//
// Returns:             -1 if failed.
//                       1 if succesful.
//
//------------------------------------------------------------------------
//
int write_check(){
    fprintf(stderr, "Testing wrtire_bit........................");

    //writes to the bits and confirms their value
    for(int i = 1; i <= 8; i++)
    {//sets the value of bits to 1 and checks.
        set_bit(1, i);
        
        if(read_bit(1, i + 8) != 1)
            return(-1);

        clr_bit(1, i);
        clr_bit(1, i+8);
        if(read_bit(1, i+8) != 0)
            return(-1);

        //writes the value of the bit to zero and checks.
        write_bit(1,1+8,0);

        if(read_bit(1, i) != 0)
            return(-1);
    }//bits 1-16

    for(int i = 17; i <= 20; i++)
    {//sets the value of bits to 1 and checks.
        set_bit(1, i);
        
        if(read_bit(1, i + 4) != 1)
            return(-1);

        clr_bit(1, i);
        clr_bit(1, i+4);

        if(read_bit(1, i+4) != 0)
            return(-1);

        //writes the value of the bit to zero and checks.
        write_bit(1,1+4,0);

        if(read_bit(1, i) != 0)
            return(-1);
    }//bits 17-24
    return 1;
}//end of write_check
//
//------------------------------------------------------------------------
//
// Description:         This function tests the uio48io functions get_int,
//                      disab_int,and enab_int validating their functionality.  
//
// Arguments:           None.
//
// Returns:             -1 if failed.
//                       1 if succesful.
//
//------------------------------------------------------------------------
//
int intTest()
{
    fprintf(stderr, "Testing get_int, disab_int, enab_int......");

    //clears out any lingering configuration changes on bits
    for(int i = 0; i <= 24; i++)
    {
        get_int(1);
        usleep(100);
        disab_int(1, i);
        usleep(100);
        clr_bit(1, i);       
    }

    for(int i = 1; i <= 8; i++)
    {
        //generates and checks for interrupts
        enab_int(1, i, RISING);
        set_bit(1, i+8);
        usleep(100);
        clr_bit(1, i+8);
        usleep(100);
        if(get_int(1) != i)
            return -1;

        //clears out interrupts
        while (get_int(1)) ;
        disab_int(1, i);

        //generates and checks for interrupts
        enab_int(1, i+8, RISING);
        set_bit(1, i);
        usleep(100);
        clr_bit(1, i);
        usleep(100);
        if(get_int(1) != (i+8))
            return -1;
    }//bits 1-16

    //clears out any lingering configuration changes on bits
    for(int i = 0; i <= 24; i++)
    {
        get_int(1);
        usleep(100);
        disab_int(1, i);
        usleep(100);
        clr_bit(1, i);       
    }
    

    for(int i = 17; i <= 20; i++)
    { 
        //generates and checks for interrupts
        enab_int(1, i, RISING);
        set_bit(1, i+4);
        usleep(100);
        clr_bit(1, i+4);
        usleep(100);
        if(get_int(1) != i)
            return -1;

        //clears out interrupts
        while (get_int(1)) ;
        disab_int(1, i);

        //clears out any lingering configuration changes on bits
        usleep(100);
        enab_int(1, i+4, RISING);
        set_bit(1, i);
        usleep(100);
        clr_bit(1, i);  
        usleep(100);
        if(get_int(1) != (i+4))
            return -1;
    }//bits 17-24
    return 1;
}//end of IntTest
//
//------------------------------------------------------------------------
//
// Description:         This function tests the uio48io functions write_byte,
//                      and read_byte validating their functionality.  
//
// Arguments:           None.
//
// Returns:             -1 if failed.
//                       1 if succesful.
//
//------------------------------------------------------------------------
//
int Byte_test()
{
    fprintf(stderr, "Testing write_byte, read_byte.............");
    int c;

    //writes to bytes and confirms the correct value.  
    for(int y = 1; y <= 5; y++)
    {
        for(int i = 1; i <= 127; i++)
        {
            if(y == 2)
                y = 3;
            write_byte(1, y, i);

            c = read_byte(1, y);

            if(c != i)
                 return -1;
        }
    }
    return 1;
}//end of Byte_test
//
//------------------------------------------------------------------------
//
// Description:         This function tests the uio48io functions lock_port,
//                      and unlock_port validating their functionality.  
//
// Arguments:           None.
//
// Returns:             -1 if failed.
//                       1 if succesful.
//
//------------------------------------------------------------------------
//
int lock_test()
{
    int x;
    fprintf(stderr, "Testing lock_port, unlock_port............");

    //writes to to certain ports, locks the port, and confirms the unchanged value
    //then unlocks the port, writes to the port, and confirms the changed value. 
    for(int y = 1; y <= 5; y++){
        for(int i = 1; i <= 127; i++){
            if(y == 2)
                y = 3;

            write_byte(1, y, i);
            x = read_byte(1, y);

            lock_port(1, y);

            write_byte(1, y, i+1);        
            
            if(x != i)
                return -1;

            unlock_port(1,y);

            write_byte(1, y, i+1);
            x = read_byte(1, y);

            if(x != i+1)
                return -1;
        }
    }
    return 1;
}//end of lock_test
