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
	port = (bit_number / 8) + pUioDev->base_port;

	// Get the current contents of the port
	val = inb(port);

	// Get just the bit we specified
	val = val & (1 << (bit_number % 8));

	if(val)
		return 1;

	return 0;
}

//***********************************************************************

static void write_bit( p_uio48_dev pUioDev, int bit_number, int val)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust bit number for 0 based numbering
	--bit_number;

	// obtain lock before writing
	mutex_lock_interruptible(&pUioDev->mtx);

	// Calculate the I/O address of the port based on the bit number
	port = (bit_number / 8) + pUioDev->base_port;

	// Use the image value to avoid having to read the port first
	temp = pUioDev->port_images[bit_number / 8];

	// Calculate a bit mask for the specified bit
	mask = (1 << (bit_number % 8));

	// check whether the request was to set or clear and mask accordingly
	if(val)		// If the bit needs to be set
		temp = temp | mask;
	else
		temp = temp & ~mask;

	// Update the image value with the value we're about to write
	pUioDev->port_images[bit_number /8] = temp;

	// Now actally update the port. Only the specified bit is affected
	outb(temp, port);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

//***********************************************************************

static void UIO48_set_bit( p_uio48_dev pUioDev, int bit_num)
{
	write_bit(pUioDev, bit_num, 1);
}

//***********************************************************************

static void clr_bit( p_uio48_dev pUioDev, int bit_num)
{
	write_bit(pUioDev, bit_num, 0);
}

//***********************************************************************

static void enab_int( p_uio48_dev pUioDev, int bit_number, int polarity )
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = pUioDev->base_port;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&pUioDev->mtx);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | pUioDev->lock_image, base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Set the enable bit for our bit number
	temp = temp | mask;

	// Now update the interrupt enable register
	outb(temp,port);

	// Turn on access to page 1 for polarity control
	outb(PAGE1 | pUioDev->lock_image, base_port + 7);

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
	outb(PAGE3 | pUioDev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

//***********************************************************************

static void disab_int( p_uio48_dev pUioDev, int bit_number )
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = pUioDev->base_port;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&pUioDev->mtx);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | pUioDev->lock_image, base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// clear the enable bit for our bit number
	temp = temp & ~mask;

	// Now update the interrupt enable register
	outb(temp, port);

	// Set access back to page 3
	outb(PAGE3 | pUioDev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

//***********************************************************************

static void clr_int( p_uio48_dev pUioDev, int bit_number)
{
	unsigned port;
	unsigned temp;
	unsigned mask;
	unsigned base_port = pUioDev->base_port;

	// Adjust bit number
	--bit_number;

	// obtain lock
	spin_lock(&pUioDev->spnlck);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | pUioDev->lock_image, base_port + 7);

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
	outb(PAGE3 | pUioDev->lock_image, base_port + 7);

	//release lock
	spin_unlock(&pUioDev->spnlck);
}

//***********************************************************************

static int get_int( p_uio48_dev pUioDev)
{
	unsigned base_port = pUioDev->base_port;
	int i, t;//, ret = 0;

	spin_lock(&pUioDev->spnlck);

	/* Read the master interrupt pending register, mask off undefined
	 * bits. */
	t = inb(base_port + 6) & 0x07;

	/* If there are no pending interrupts, return 0. */
	if (t == 0) {
		spin_unlock(&pUioDev->spnlck);
		return 0;
	}

	/* Check ports 0, 1, and 2 for interrupt ID register. */
	for (i = 0; i < 3; i++) {
		/* Read the interrupt ID register if necessary */
		if ((t >> i) & 1) {
   			pUioDev->irq_image[i] = inb(base_port + 8 + i);

			// clear irq
   			outb(0, base_port + 8 + i);
		}
		else
			pUioDev->irq_image[i] = 0;

	}

	spin_unlock(&pUioDev->spnlck);

	return 1;
}

//***********************************************************************

static int get_buffered_int( p_uio48_dev pUioDev)
{
	int temp;

	// for polled option, no irq selected
	if (pUioDev->irq == 0) {
		temp = get_int(pUioDev);

		return temp;
	}

	if (pUioDev->outptr != pUioDev->inptr) {
		temp = pUioDev->int_buffer[pUioDev->outptr++];

		if (pUioDev->outptr == MAX_INTS)
			pUioDev->outptr = 0;

		return temp;
	}

	return 0;
}

//***********************************************************************

static void clr_int_id( p_uio48_dev pUioDev, int port_number)
{
	unsigned base_port = pUioDev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&pUioDev->mtx);

	// write to specified int_id register
	outb(0, base_port + 8 + port_number);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

//***********************************************************************

static void lock_port( p_uio48_dev pUioDev, int port_number)
{
	unsigned base_port = pUioDev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&pUioDev->mtx);

	// write to specified int_id register
	pUioDev->lock_image |= 1 << port_number;
	outb(PAGE3 | pUioDev->lock_image, base_port + 7);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

//***********************************************************************

static void unlock_port( p_uio48_dev pUioDev, int port_number)
{
	unsigned base_port = pUioDev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&pUioDev->mtx);

	// write to specified int_id register
	pUioDev->lock_image &= ~(1 << port_number);
	outb((PAGE3 | pUioDev->lock_image), base_port + 7);

	//release lock
	mutex_unlock(&pUioDev->mtx);
}

///**********************************************************************
//			DEVICE OPEN
///**********************************************************************
static int device_open(struct inode *inode, struct file *file)
{
	struct uio48_dev *uiodev;

	uiodev = container_of(inode->i_cdev, struct uio48_dev, cdev);

	file->private_data = uiodev;

	pr_devel("[%s] device_open\n", uiodev->name);

	return 0;
}

///**********************************************************************
//			DEVICE CLOSE
///**********************************************************************
static int device_release(struct inode *inode, struct file *file)
{
	struct uio48_dev *uiodev;

	uiodev = container_of(inode->i_cdev, struct uio48_dev, cdev);

	pr_devel("[%s] device_release\n", uiodev->name);

	return 0;
}

///**********************************************************************
//			DEVICE IOCTL
///**********************************************************************
static long device_ioctl(struct file *file, unsigned int ioctl_num,
			 unsigned long ioctl_param)
{
	struct uio48_dev *uiodev = file->private_data;
	int i, port, ret_val;

	pr_devel("[%s] IOCTL CODE %04X\n", uiodev->name, ioctl_num);

	switch (ioctl_num) {
	case IOCTL_READ_PORT:
		port = (ioctl_param & 0xff);
		ret_val = inb(uiodev->base_port + port);
		return ret_val;

	case IOCTL_WRITE_PORT:
		mutex_lock_interruptible(&uiodev->mtx);

		port = (ioctl_param >> 8) & 0xff;
		ret_val = ioctl_param & 0xff;

		outb(ret_val, uiodev->base_port + port);

		mutex_unlock(&uiodev->mtx);

		return SUCCESS;

	case IOCTL_READ_BIT:
		ret_val = read_bit(uiodev, ioctl_param & 0xff);
		return ret_val;

	case IOCTL_WRITE_BIT:
		write_bit(uiodev, (ioctl_param >> 8) & 0xff, ioctl_param & 0xff);
		return SUCCESS;

	case IOCTL_SET_BIT:
		UIO48_set_bit(uiodev, ioctl_param & 0xff);
		return SUCCESS;

	case IOCTL_CLR_BIT:
		clr_bit(uiodev, ioctl_param & 0xff);
		return SUCCESS;

	case IOCTL_ENAB_INT:
		enab_int(uiodev, (int)(ioctl_param >> 8), (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_DISAB_INT:
		disab_int(uiodev, ioctl_param & 0xff);
		return SUCCESS;

	case IOCTL_CLR_INT:
		clr_int(uiodev, ioctl_param & 0xff);
		return SUCCESS;

	case IOCTL_GET_INT:
		i = get_buffered_int(uiodev);
		return i;

	case IOCTL_WAIT_INT:
		if ((i = get_buffered_int(uiodev)))
            return i;

		uiodev->ready = 0;
		wait_event(uiodev->wq, uiodev->ready);

		/* Getting here does not guarantee that there's an interrupt
		 * available we may have been awakened by some other signal.
		 * In any case We'll return whatever's available in the
		 * interrupt queue even if it's empty. */
		i = get_buffered_int(uiodev);

		return i;

	case IOCTL_CLR_INT_ID:
		clr_int_id(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_LOCK_PORT:
		lock_port(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_UNLOCK_PORT:
		unlock_port(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	default:
		return -EINVAL;
	}

	return SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////

//****************************************************************************
//
//! \fucntion  static InitializeIntRegs( p_uio48_dev pDev )
//
//! \brief  Initializes the HW's interrupt related registers to their runtime
//!         values. Called by the device's initialization code and by their
//!         power management "resume" routine when the system is coming out
//!         of suspend or hibernate
//
//! @param[in]      pDev        Pointer to struct pcmmio_device
//
//! \return     void
//
//****************************************************************************
static void InitializeIntRegs( p_uio48_dev pDev )
{
    
}

//////////////////////////////////////////////////////////////////////////////
//
// power management device operations & structure
//
//****************************************************************************
//
//! \fucntion   static int uio_suspend( struct device *pDev )
//
//! \brief  Called by the power management framework when the system is entering
//!         a "suspend" state.
//
//! @param[in]  pDev    Pointer to "parent" device for this pcmmio_device
//
//! \return     Always returns 0
//
//! \note       This call is from the power management capability of this
//!             device's class
//
//****************************************************************************
static int uio_suspend( struct device *pDev )
{
   p_uio48_dev pUioDev = ( p_uio48_dev ) to_uio48_dev( pDev );    // pointer to the uio device
                                                                     
   mutex_lock( &pUioDev->mtx );

   pr_info( "%s - /dev/%s\n", __func__, pUioDev->name );

   mutex_unlock( &pUioDev->mtx );

   return 0;
}

//****************************************************************************
//
//! \fucntion   static int uio_resume( struct device *pDev )
//
//! \brief  Called by the power management framework when the system is entering
//!         a "suspend" state.
//
//! @param[in]  pDev    Pointer to "parent" device for this pcmmio_device
//
//! \return     Always returns 0
//
//! \note       This call is from the power management capability of this
//!             device's class
//
//****************************************************************************
static int uio_resume( struct device *pDev )
{
   p_uio48_dev pUioDev = ( p_uio48_dev ) to_uio48_dev( pDev );    // pointer to the uio device
                                                                         
   mutex_lock( &pUioDev->mtx );
   
   pr_info( "%s - /dev/%s\n", __func__, pUioDev->name );
   
   InitializeIntRegs( pUioDev );

   mutex_unlock( &pUioDev->mtx );

   return 0;
}

//****************************************************************************
//
//! \fucntion   static int uio_idle( struct device *pDev )
//
//! \brief  Called by the power management framework when the system is entering
//!         a "suspend" state.
//
//! @param[in]  pDev    Pointer to "parent" device for this pcmmio_device
//
//! \return     Always returns 0
//
//! \note       This call is from the power management capability of this
//!             device's class
//
//****************************************************************************
static int uio_idle( struct device *pDev )
{
   p_uio48_dev pUioDev = ( p_uio48_dev ) to_uio48_dev( pDev );    // pointer to the uio device
                                                                          
   mutex_lock( &pUioDev->mtx );
   
   pr_info( "%s - /dev/%s\n", __func__, pUioDev->name );
                                                                  
   mutex_unlock( &pUioDev->mtx );

   return 0;
}

static UNIVERSAL_DEV_PM_OPS( uio_class_dev_pm_ops, uio_suspend, uio_resume, uio_idle );

////////////////////////////////////////////////////////////////////////////




///**********************************************************************
//			Module Declarations
// This structure will hold the functions to be called
// when a process does something to the device
///**********************************************************************
static struct file_operations uio48_fops = {
	owner:			THIS_MODULE,
	unlocked_ioctl:		device_ioctl,
	open:			device_open,
	release:		device_release,
};

///**********************************************************************
//			INIT MODULE
///**********************************************************************
// register the character device
int init_module()
{
	int ret_val, io_num;
	dev_t dev;
	int x;

	pr_info(MOD_DESC " loading\n");

	p_uio48_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(p_uio48_class)) {
		pr_err("Could not create module class\n");
		return PTR_ERR(p_uio48_class);
	}
	
	p_uio48_class->pm = &uio_class_dev_pm_ops;

	/* Register the character device. */
	if (uio48_init_major) {
		uio48_devno = MKDEV(uio48_init_major, 0);
		ret_val = register_chrdev_region(uio48_devno, MAX_CHIPS, KBUILD_MODNAME);
	} else {
		ret_val = alloc_chrdev_region(&uio48_devno, 0, MAX_CHIPS, KBUILD_MODNAME);
		uio48_init_major = MAJOR(uio48_devno);
	}

	if (ret_val < 0) {
		pr_err("Cannot obtain major number (%d)\n", uio48_init_major);
		return ret_val;
	}

	pr_info("Major number %d assigned\n", uio48_init_major);

        for ( x = io_num = 0; x < MAX_CHIPS; x++ ) 
        {
           p_uio48_dev pUioDev = &uiodevs[ x ];

           // 
           // If no IO port, skip this idx
           //
           
           if ( io_port[ x ] == 0 )
           {
              continue;
           }

           mutex_init( &pUioDev->mtx );
           spin_lock_init( &pUioDev->spnlck );
           init_waitqueue_head( &pUioDev->wq );

           dev = uio48_devno + x;

           /* Initialize char device. */
           cdev_init( &pUioDev->cdev, &uio48_fops );
           ret_val = cdev_add( &pUioDev->cdev, dev, 1 );

           if ( ret_val ) 
           {
              pr_err("Error adding character device for node %d\n", x);
              return ret_val;
           }

           /* Check and map our I/O region requests. */
           if ( request_region( io_port[ x ], 0x10, KBUILD_MODNAME ) == NULL) 
           {
              pr_err("Unable to use I/O Address %04X\n", io_port[ x ] );
              cdev_del( &pUioDev->cdev );
              continue;
           }

           init_io( pUioDev, io_port[ x ] );

           
           if ( irq[ x ] )      // use an interrupt?
           {
              //
              // map interrupt
              //
               
              if ( request_irq( irq[ x ], irq_handler, IRQF_SHARED, KBUILD_MODNAME, pUioDev ) ) 
              {
                 pr_err("Unable to register IRQ %d\n", irq[x]);
                 release_region( io_port[ x ], 0x10 );
                 cdev_del( &pUioDev->cdev );
                 continue;
              }

              pUioDev->irq = irq[ x ];
           }

           io_num++;
           BoardCount++;

           sprintf(pUioDev->name, KBUILD_MODNAME "%c", 'a' + x);

           pr_info("[%s] Added new device\n", pUioDev->name);

           pUioDev->pDev = device_create(p_uio48_class, NULL, dev, NULL, "%s", pUioDev->name);
                
           pUioDev->pDev->driver_data = ( void *)( pUioDev );    // pUioDev->pDev is a pointer to the system device
                                                                 // returned by device_create. We're setting that
                                                                 // device's driver_data to be a pointer to the
                                                                 // current pcmmio_device
        }

        if ( io_num )
        {
           return 0;
        }

        pr_warning( "No resources available, driver terminating\n" );

        class_destroy( p_uio48_class );
        unregister_chrdev_region( uio48_devno, MAX_CHIPS );

        return -ENODEV;
}

///**********************************************************************
//			CLEANUP MODULE
///**********************************************************************
// unregister the appropriate file from /proc
void cleanup_module()
{
   int x;

   //
   // Unregister I/O port usage and IRQ
   //
   
   for (x = 0; x < MAX_CHIPS; x++) 
   {
      p_uio48_dev pUioDev = &uiodevs[ x ];
      
      if ( io_port[ x ] == 0 )
      {
         continue;
      }
        
      if ( pUioDev->base_port )
      {
         release_region( pUioDev->base_port, 0x10 );
      }

      if ( pUioDev->irq )
      {
         free_irq( pUioDev->irq, pUioDev );
      }

      cdev_del( &pUioDev->cdev );
      device_destroy( p_uio48_class, ( uio48_devno + x ) );
      
      pr_info( "[%s] Removed existing device\n", pUioDev->name );

   }

   class_destroy( p_uio48_class );
   unregister_chrdev_region( uio48_devno, MAX_CHIPS );
}

