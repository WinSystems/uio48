//****************************************************************************
//	
//	Copyright 2010-20 by WinSystems Inc.
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
//****************************************************************************
//
//	Name	 : uio48.c
//
//	Project	 : PCM-UIO48 Linux Device Driver
//
//	Author	 : Paul DeMetrotion
//
//****************************************************************************
//
//        Date          Revision              Description
//      --------        --------        ---------------------------------------------
//          2011          1.0           Original Release	
//          2016          1.1
//      07/30/20          2.0           Added Suspend/Resum capability
//
//****************************************************************************

/* Helper to format our pr_* functions */
#define pr_fmt(__fmt) KBUILD_MODNAME ": " __fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/fs.h>

#include "uio48.h"

#define MOD_DESC "WinSystems, Inc. UIO48 Digital I/O Driver"
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(MOD_DESC);
MODULE_AUTHOR("Paul DeMetrotion");

//******************************************************************************
//
// Local preprocessor macros
//

#define MAX_INTS                    1024
#define NUMBER_OF_DIO_PORTS         6

//
// Page defintions
//
#define PAGE0           0x0
#define PAGE1           0x40
#define PAGE2           0x80
#define PAGE3           0xc0


//
// macro to turn a struct device's driver_data field into a void * suitable for
// casting to a pcmmio structure pointer...
//
#define to_uio48_dev( p )    dev_get_drvdata( p )


//******************************************************************************
//
// Local structures, typedefs and enums...
//

//
// This is it - the software representation of the uio48 device. It'll be
// instantiated as part of a system "struct device", and will have it's own
// unique system "struct class".
//

struct uio48_dev {
	char                   name[32];
	unsigned               irq;
	unsigned char          int_buffer[MAX_INTS];
	int                    inptr;
	int                    outptr;
	wait_queue_head_t      wq;
	struct mutex           mtx;
	spinlock_t             spnlck;
	struct cdev            cdev;
	unsigned               base_port;
	unsigned char          port_images[ NUMBER_OF_DIO_PORTS ];
	int                    ready;
	unsigned char          lock_image;
	unsigned char          irq_image[ 3 ];
        
        struct device         *pDev;       // added so we can self-reference from the
                                           // power management functions
        
        
};


typedef struct uio48_dev    *p_uio48_dev;        

//******************************************************************************
//
// local (static) variables
//

//
// Driver major number
//
static int uio48_init_major;           // 0 = allocate dynamically

//
// Our modprobe command line arguments
//
static unsigned     io_port[ MAX_CHIPS ];
static unsigned     irq[ MAX_CHIPS ];

MODULE_PARM_DESC( io_port, "Array of IO addresses for devices" );
module_param_array( io_port, uint, NULL, S_IRUGO );

MODULE_PARM_DESC( irq, "Array of IRQ routes for devices" );
module_param_array( irq, uint, NULL, S_IRUGO );

static struct uio48_dev     uiodevs[MAX_CHIPS];
static struct class        *p_uio48_class;
static dev_t                uio48_devno;

static unsigned char        BoardCount = 0;



//******************************************************************************
// global (exported) variables                        



//******************************************************************************
//
// prototypes for local (static) functions
//                        

static void clr_int( p_uio48_dev pUioDev, int bit_number);
static int get_int( p_uio48_dev pUioDev );


//******************************************************************************
//
// UIO48 ISR
//
static irqreturn_t irq_handler( int __irq, void *dev_id )
{
    struct uio48_dev *uiodev = dev_id;
    int i, j;
    bool irq = false;

    if(get_int(uiodev))
    {
        for (i = 0; i < 3; i++) 
        {	
            if (uiodev->irq_image[i] != 0)
            {
                irq = true;  // at least one irq

                for (j = 0; j < 8; j++)
                {
                    if ((uiodev->irq_image[i] >> j) & 1)
                    {
                        uiodev->int_buffer[uiodev->inptr++] = (i * 8) + j + 1;

                        if (uiodev->inptr == MAX_INTS)
                            uiodev->inptr = 0;
                    }
                }
            }
            else
                continue;
        }

        if (!uiodev->ready)
        {
            uiodev->ready = 1;
            wake_up(&uiodev->wq);
        }
    }
    
    return IRQ_HANDLED;

}

// ******************* Device Subroutines *****************************

static void init_io( p_uio48_dev pUioDev, unsigned base_port )
{
   int x;

   // obtain lock
   mutex_lock_interruptible( &pUioDev->mtx );

   // save the address for later use
   pUioDev->base_port = base_port;

   // Clear all of the I/O ports. This also makes them inputs
   for ( x = 0; x < NUMBER_OF_DIO_PORTS; x++ )
   {
      outb(0, base_port + x);
   }

   // Clear the image values as well
   for (x = 0; x < NUMBER_OF_DIO_PORTS; x++)
   {
      pUioDev->port_images[ x ] = 0;
   }

   // set lock image to default value in device
   pUioDev->lock_image = inb( base_port + 7 ) & 0x3F; // clear page bits

   // Set page 2 access, for interrupt enables
   outb( ( PAGE2 | pUioDev->lock_image ),( base_port + 7 ) );

   // Clear all interrupt enables
   outb( 0, base_port + 8 );
   outb( 0, base_port + 9 );
   outb( 0, base_port + 0x0a );

   // default to page 3 register access for fast isr
   outb( ( PAGE3 | pUioDev->lock_image ), ( base_port + 7) );

   //release lock
   mutex_unlock( &pUioDev->mtx );
}

//***********************************************************************

static int read_bit( p_uio48_dev pUioDev, int bit_number)
{
	unsigned port;
	int val;

	// Adjust bit_number to 0 to 47 numbering
	--bit_number;

	// Calculate the I/O port address based on the updated bit_number
	port = (bit_number / 8) + uiodev->base_port;

	// Get the current contents of the port
	val = inb(port);

	// Get just the bit we specified
	val = val & (1 << (bit_number % 8));

	if(val)
		return 1;

	return 0;
}

static void write_bit(struct uio48_dev *uiodev, int bit_number, int val)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust bit number for 0 based numbering
	--bit_number;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// Calculate the I/O address of the port based on the bit number
	port = (bit_number / 8) + uiodev->base_port;

	// Use the image value to avoid having to read the port first
	temp = uiodev->port_images[bit_number / 8];

	// Calculate a bit mask for the specified bit
	mask = (1 << (bit_number % 8));

	// check whether the request was to set or clear and mask accordingly
	if(val)		// If the bit needs to be set
		temp = temp | mask;
	else
		temp = temp & ~mask;

	// Update the image value with the value we're about to write
	uiodev->port_images[bit_number /8] = temp;

	// Now actally update the port. Only the specified bit is affected
	outb(temp, port);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void UIO48_set_bit(struct uio48_dev *uiodev, int bit_num)
{
	write_bit(uiodev, bit_num, 1);
}

static void clr_bit(struct uio48_dev *uiodev, int bit_num)
{
	write_bit(uiodev, bit_num, 0);
}

static void enab_int(struct uio48_dev *uiodev, int bit_number, int polarity)
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = uiodev->base_port;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&uiodev->mtx);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | uiodev->lock_image, base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Set the enable bit for our bit number
	temp = temp | mask;

	// Now update the interrupt enable register
	outb(temp,port);

	// Turn on access to page 1 for polarity control
	outb(PAGE1 | uiodev->lock_image, base_port + 7);

	// Get the current state of the polarity register
	temp = inb(port);

	// Set the polarity according to the argument in the image value
	if (polarity)
		temp = temp | mask;
	else
		temp = temp & ~mask;

	// Write out the new polarity value
	outb(temp, port);

	// Set access back to page 3
	outb(PAGE3 | uiodev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void disab_int(struct uio48_dev *uiodev, int bit_number)
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = uiodev->base_port;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&uiodev->mtx);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | uiodev->lock_image, base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// clear the enable bit for our bit number
	temp = temp & ~mask;

	// Now update the interrupt enable register
	outb(temp, port);

	// Set access back to page 3
	outb(PAGE3 | uiodev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void clr_int(struct uio48_dev *uiodev, int bit_number)
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = uiodev->base_port;

	// Adjust bit number
	--bit_number;

	// obtain lock
	spin_lock(&uiodev->spnlck);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | uiodev->lock_image, base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Temporarily clear only our enable. This clears the interrupt
	temp = temp & ~mask;    // Clear the enable for this bit

	// Now update the interrupt enable register
	outb(temp, port);

	// Re-enable our interrupt bit
	temp = temp | mask;

	outb(temp, port);

	// Set access back to page 3
	outb(PAGE3 | uiodev->lock_image, base_port + 7);

	//release lock
	spin_unlock(&uiodev->spnlck);
}

static int get_int(struct uio48_dev *uiodev)
{
	unsigned base_port = uiodev->base_port;
	int i, t;//, ret = 0;

	spin_lock(&uiodev->spnlck);

	/* Read the master interrupt pending register, mask off undefined
	 * bits. */
	t = inb(base_port + 6) & 0x07;

	/* If there are no pending interrupts, return 0. */
	if (t == 0) {
		spin_unlock(&uiodev->spnlck);
		return 0;
	}

	/* Check ports 0, 1, and 2 for interrupt ID register. */
	for (i = 0; i < 3; i++) {
		/* Read the interrupt ID register if necessary */
		if ((t >> i) & 1) {
   			uiodev->irq_image[i] = inb(base_port + 8 + i);

			// clear irq
   			outb(0, base_port + 8 + i);
		}
		else
			uiodev->irq_image[i] = 0;

	}

	spin_unlock(&uiodev->spnlck);

	return 1;
}

static int get_buffered_int(struct uio48_dev *uiodev)
{
	int temp;

	// for polled option, no irq selected
	if (uiodev->irq == 0) {
		temp = get_int(uiodev);

		return temp;
	}

	if (uiodev->outptr != uiodev->inptr) {
		temp = uiodev->int_buffer[uiodev->outptr++];

		if (uiodev->outptr == MAX_INTS)
			uiodev->outptr = 0;

		return temp;
	}

	return 0;
}

static void clr_int_id(struct uio48_dev *uiodev, int port_number)
{
	unsigned base_port = uiodev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// write to specified int_id register
	outb(0, base_port + 8 + port_number);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void lock_port(struct uio48_dev *uiodev, int port_number)
{
	unsigned base_port = uiodev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// write to specified int_id register
	uiodev->lock_image |= 1 << port_number;
	outb(PAGE3 | uiodev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void unlock_port(struct uio48_dev *uiodev, int port_number)
{
	unsigned base_port = uiodev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// write to specified int_id register
	uiodev->lock_image &= ~(1 << port_number);
	outb((PAGE3 | uiodev->lock_image), base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}
