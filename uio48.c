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
//	Name	 : uio48.c
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
//								Function ioctl deprecated for unlocked_ioctl
//								Added mutex/spinlock support
//								Removed pre-2.6.18 interrupt support
//								Support for up to 4 devices
//
///****************************************************************************

static char *RCSInfo = "$Id: uio48.c, v 4.0 2011-06-14 paul Exp $";

#ifndef __KERNEL__
	#define __KERNEL__
#endif

#ifndef MODULE
	#define MODULE
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	#include <linux/autoconf.h>
# else
	#include <generated/autoconf.h>
#endif

#include "uio48.h"

#define DRVR_NAME		"uio48"
#define DRVR_VERSION	"4.0"
#define DRVR_RELDATE	"1Dec2011"

//#define DEBUG 1

// Function prototypes for local functions
static void init_io(int chip_number, unsigned io_address);
static int read_bit(int chip_number, int bit_number);
static void write_bit(int chip_number, int bit_number, int val);
static void UIO48_set_bit(int chip_num, int bit_num);
static void clr_bit(int chip_num, int bit_num);
static void enab_int(int chip_number, int bit_number, int polarity);
static void disab_int(int chip_number, int bit_number);
static void clr_int(int chip_number, int bit_number);
static int get_int(int chip_number);
static int get_buffered_int(int chip_number);
static void clr_int_id(int chip_number, int port_number);
static void lock_port(int chip_number, int port_number);
static void unlock_port(int chip_number, int port_number);

// ******************* Device Declarations *****************************

#define MAX_INTS 1024

// Driver major number
static int uio48_init_major = 0;	// 0 = allocate dynamically
static int uio48_major;

// uio48 char device structure
static struct cdev uio48_cdev[MAX_CHIPS];
static int cdev_num;

// This holds the base addresses of the IO chips
static unsigned base_port[MAX_CHIPS];

// Page defintions
#define PAGE0		0x0
#define PAGE1		0x40
#define PAGE2		0x80
#define PAGE3		0xc0

// Our modprobe command line arguments
static unsigned io[MAX_CHIPS];
static unsigned irq[MAX_CHIPS];

module_param_array(io, uint, NULL, S_IRUGO);
module_param_array(irq, uint, NULL, S_IRUGO);

// We will buffer up the transition interrupts and will pass them on
// to waiting applications
static unsigned char int_buffer[MAX_CHIPS][MAX_INTS];
static int inptr[MAX_CHIPS];
static int outptr[MAX_CHIPS];

// These declarations create the wait queues. One for each supported device
static DECLARE_WAIT_QUEUE_HEAD(wq_1);
static DECLARE_WAIT_QUEUE_HEAD(wq_2);
static DECLARE_WAIT_QUEUE_HEAD(wq_3);
static DECLARE_WAIT_QUEUE_HEAD(wq_4);

// mutex & spinlock
static struct mutex mtx[MAX_CHIPS];
static spinlock_t spnlck[MAX_CHIPS];

// This is the common interrupt handler. It is called by the chip specific
// handlers with the device number as an argument
static irqreturn_t common_handler(int __irq, void *dev_id)
{
	int device = (unsigned long)dev_id;
	int my_interrupt, count;

	my_interrupt = irq[device];

	do {
		int x;

		count = 0;

		for (x = 0; x < MAX_CHIPS; x++) {
			int c;

			if (irq[x] != my_interrupt)
				continue;

			// obtain irq bit
			c = get_int(x + 1);

			if (c == 0)
				continue;

			clr_int(x+1, c);
			count++;

			#ifdef DEBUG
			printk("<1>UIO48 - Interrupt on chip %d, bit %d\n",x,c);
			#endif

			int_buffer[x][inptr[x]++] = c;

			if (inptr[x] == MAX_INTS)
				inptr[x] = 0;

			switch (x) {
			case 0:
				wake_up_interruptible(&wq_1);
				break;

			case 1:
				wake_up_interruptible(&wq_2);
				break;

			case 2:
				wake_up_interruptible(&wq_3);
				break;

			case 3:
				wake_up_interruptible(&wq_4);
					break;
			}
		}

	} while (count);

	return IRQ_HANDLED;
}

///**********************************************************************
//			DEVICE OPEN
///**********************************************************************
// called whenever a process attempts to open the device file
static int device_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);

	if(base_port[minor] == 0)
	{
		printk("<1>UIO48 **** OPEN ATTEMPT on uninitialized port *****\n");
		return -1;
	}

	#ifdef DEBUG
	printk ("<1>UIO48 - device_open(%p)\n", file);
	#endif

	return SUCCESS;
}

///**********************************************************************
//			DEVICE CLOSE
///**********************************************************************

static int device_release(struct inode *inode, struct file *file)
{
	#ifdef DEBUG
	printk ("<1>UIO48 - device_release(%p,%p)\n", inode, file);
	#endif

	return 0;
}

///**********************************************************************
//			DEVICE IOCTL
///**********************************************************************
static long device_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	int i, device, port, ret_val;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
	unsigned int minor = MINOR(file->f_dentry->d_inode->i_rdev);
#else
	unsigned int minor = MINOR(file_inode(file)->i_rdev);
#endif

	#ifdef DEBUG
	printk("<1>UIO48 - IOCTL call minor %d,IOCTL CODE %04X\n",minor,ioctl_num);
	#endif

	// Switch according to the ioctl called
	switch (ioctl_num) {
		case IOCTL_READ_PORT:
			device = minor;
			port = (ioctl_param & 0xff);
			ret_val = inb(base_port[device] + port);
			return ret_val;

	    case IOCTL_WRITE_PORT:
			device = minor;

			// obtain lock before writing
			mutex_lock_interruptible(&mtx[device]);

			port = (ioctl_param >> 8) & 0xff;
			ret_val = ioctl_param & 0xff;

			outb(ret_val, base_port[device] + port);

			//release lock
			mutex_unlock(&mtx[device]);

			#ifdef DEBUG
			printk("<1>UIO48 - Writing to I/O Port %04x with%02x\n", base_port[device]+port, ret_val);
			#endif

			return SUCCESS;

	    case IOCTL_READ_BIT:
			device = minor + 1;
		    ret_val = read_bit(device,ioctl_param & 0xff);
			return ret_val;

		case IOCTL_WRITE_BIT:
			device = minor + 1;
			write_bit(device, (ioctl_param >> 8) & 0xff, ioctl_param & 0xff);
			return SUCCESS;

		case IOCTL_SET_BIT:
			device = minor + 1;
			UIO48_set_bit(device, ioctl_param & 0xff);
			return SUCCESS;

		case IOCTL_CLR_BIT:
			device = minor + 1;
			clr_bit(device, ioctl_param & 0xff);
			return SUCCESS;

		case IOCTL_ENAB_INT:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Set_int Device %d Bit %d polarity %d\n",
					device, (int)(ioctl_param>>8), (int)(ioctl_param & 0xff));
			#endif

			enab_int(device, (int)(ioctl_param >> 8), (int)(ioctl_param&0xff));
			return SUCCESS;

		case IOCTL_DISAB_INT:
			device = minor +1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Disab_Int Device %d Bit %d\n",
					device, (int)(ioctl_param & 0xff));
			#endif

			disab_int(device, (int)(ioctl_param & 0xff));
			return SUCCESS;

	    case IOCTL_CLR_INT:
			device = minor +1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Clr_int Device %d Bit %d\n", device, (int)(ioctl_param & 0xff));
			#endif

			clr_int(device, (int)(ioctl_param & 0xff));
			return SUCCESS;

		case IOCTL_GET_INT:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL get_int device %d\n", device);
			#endif

			i = get_buffered_int(device);
			return i;

		case IOCTL_WAIT_INT:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL wait_int device %d\n", device);
			#endif

			if((i = get_buffered_int(minor + 1)))
				return i;

			// Code to put current process to sleep awaiting interrupt
			#ifdef DEBUG
			printk("<1>UIO48 : current process %i (%s) going to sleep\n",
					current->pid, current->comm);
			#endif

			switch (minor) {
			case 0:
				wait_event_interruptible(wq_1, 1);
				break;

			case 1:
				wait_event_interruptible(wq_2, 1);
				break;

			case 2:
				wait_event_interruptible(wq_3, 1);
				break;

			case 3:
				wait_event_interruptible(wq_4, 1);
				break;
			}

			#ifdef DEBUG
			printk("<1>UIO48 : awoken %i (%s)\n", current->pid, current->comm);
			#endif

			// Getting here does not guarantee that there's an in interrupt available
			// we may have been awakened by some other signal. In any case We'll
			// return whatever's available in the interrupt queue even if it's empty
			i = get_buffered_int(minor + 1);

			return i;

		case IOCTL_CLR_INT_ID:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Clr_Int_Id Device %d Port %d\n", device, (int)(ioctl_param & 0xff));
			#endif

			clr_int_id(device,(int)(ioctl_param&0xff));
			return SUCCESS;

		case IOCTL_LOCK_PORT:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Lock_Port Device %d Port %d\n", device, (int)(ioctl_param & 0xff));
			#endif

			lock_port(device,(int)(ioctl_param&0xff));
			return SUCCESS;

		case IOCTL_UNLOCK_PORT:
			device = minor + 1;

			#ifdef DEBUG
			printk("<1>UIO48 - IOCTL Unock_Port Device %d Port %d\n", device, (int)(ioctl_param & 0xff));
			#endif

			unlock_port(device,(int)(ioctl_param&0xff));
			return SUCCESS;

		// Catch all return
		default:
			return(-EINVAL);
	}

	return SUCCESS;
}

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
	unsigned long x;
	dev_t devno;

	// Sign-on
	printk("<1>WinSystems, Inc. UIO48 Linux Device Driver\n");
	printk("<1>Copyright 2002-2011, All rights reserved\n");
	printk("<1>%s\n", RCSInfo);

	// register the character device
	if(uio48_init_major)
	{
		uio48_major = uio48_init_major;
		devno = MKDEV(uio48_major, 0);
		ret_val = register_chrdev_region(devno, 1, DRVR_NAME);
	}
	else
	{
		ret_val = alloc_chrdev_region(&devno, 0, 1, DRVR_NAME);
		uio48_major = MAJOR(devno);
	}

	if(ret_val < 0)
	{
		printk("<1>UIO48 - Cannot obtain major number %d\n", uio48_major);
		return -ENODEV;
	}
	else
		printk("<1>UIO48 - Major number %d assigned\n", uio48_major);

	// initialize character devices
	for(x = 0, cdev_num = 0; x < MAX_CHIPS; x++)
	{
		if(io[x])	// is device required?
		{
			// add character device
			cdev_init(&uio48_cdev[x], &uio48_fops);
			uio48_cdev[x].owner = THIS_MODULE;
			uio48_cdev[x].ops = &uio48_fops;
			ret_val = cdev_add(&uio48_cdev[x], MKDEV(uio48_major, x), MAX_CHIPS);

			if(!ret_val)
			{
				printk("<1>UIO48 - Added character device %s node %ld\n", DRVR_NAME, x);
				cdev_num++;
			}
			else
			{
				printk("<1>UIO48 - Error %d adding character device %s node %ld\n", ret_val, DRVR_NAME, x);
				goto exit_cdev_delete;
			}
		}
	}

	for(x = 0, io_num = 0; x < MAX_CHIPS; x++)
	{
		if(io[x])	// is device required?
		{
			// initialize mutex array
			mutex_init(&mtx[x]);

			// initialize spinlock array
			spin_lock_init(&spnlck[x]);

			// check and map our I/O region requests
			if(request_region(io[x], 0x10, DRVR_NAME) == NULL)
			{
				printk("<1>UIO48 - Unable to use I/O Address %04X\n", io[x]);
				io[x] = 0;
				continue;
			}
			else
			{
				printk("<1>UIO48 - Base I/O Address = %04X\n", io[x]);
				init_io(x, io[x]);
				io_num++;
			}

			// check and map any interrupts
			if (irq[x]) {
				if (request_irq(irq[x], common_handler, IRQF_SHARED, DRVR_NAME, (void *)x))
					printk("<1>UIO48 - Unable to register IRQ %d\n", irq[x]);
				else
					printk("<1>UIO48 - IRQ %d registered to Chip %ld\n", irq[x], x + 1);
			}
		}
	}

	if (!io_num)	// no resources allocated
	{
		printk("<1>UIO48 - No resources available, driver terminating\n");
		goto exit_cdev_delete;
	}

	return SUCCESS;

exit_cdev_delete:
	while (cdev_num)
		cdev_del(&uio48_cdev[--cdev_num]);

	unregister_chrdev_region(devno, 1);
	uio48_major = 0;
	return -ENODEV;
}

///**********************************************************************
//			CLEANUP MODULE
///**********************************************************************
// unregister the appropriate file from /proc
void cleanup_module()
{
	int x;

	// unregister I/O port usage
	for(x=0; x < MAX_CHIPS; x++)
	{
		if(io[x])
		{
			release_region(io[x], 0x10);

			if(irq[x]) free_irq(irq[x], RCSInfo);
		}
	}

	for(x=0; x < cdev_num; x++)
	{
		// remove and unregister the device
		cdev_del(&uio48_cdev[x]);
	}

	unregister_chrdev_region(MKDEV(uio48_major, 0), 1);
	uio48_major = 0;
}

// ******************* Device Subroutines *****************************
// This array holds the image values of the last write to each I/O port
// This allows bit manipulation routines to work without having to actually do
// a read-modify-write to the I/O port
static unsigned char port_images[MAX_CHIPS][6];

static void init_io(int chip_number, unsigned io_address)
{
	int x;

	// obtain lock
	mutex_lock_interruptible(&mtx[chip_number]);

	// save the address for later use
	base_port[chip_number] = io_address;

	// Clear all of the I/O ports. This also makes them inputs
	for(x=0; x < 7; x++)
		outb(0,base_port[chip_number]+x);

	// Clear the image values as well
	for(x=0; x < 6; x++)
		port_images[chip_number][x] = 0;

	// Set page 2 access, for interrupt enables
	outb(PAGE2 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// Clear all interrupt enables
	outb(0,base_port[chip_number] + 8);
	outb(0,base_port[chip_number] + 9);
	outb(0,base_port[chip_number] + 0x0a);

	// Restore page 0 register access
	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static int read_bit(int chip_number, int bit_number)
{
	unsigned port;
	int val;

	// Adjust chip number to zero based addressing
	--chip_number;

	// Adjust bit_number to 0 to 47 numbering
	--bit_number;

	// Calculate the I/O port address based on the updated bit_number
	port = (bit_number / 8) + base_port[chip_number];

	// Get the current contents of the port
	val = inb(port);

	// Get just the bit we specified
	val = val & (1 << (bit_number % 8));

	if(val)
		return 1;

	return 0;
}

static void write_bit(int chip_number, int bit_number, int val)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust the chip number for 0 based numbering
	--chip_number;

	// Adjust bit number for 0 based numbering
	--bit_number;

	// obtain lock before writing
	mutex_lock_interruptible(&mtx[chip_number]);

	// Calculate the I/O address of the port based on the bit number
	port = (bit_number / 8) + base_port[chip_number];

	// Use the image value to avoid having to read the port first
	temp = port_images[chip_number][bit_number / 8];

	// Calculate a bit mask for the specified bit
	mask = (1 << (bit_number % 8));

	// check whether the request was to set or clear and mask accordingly
	if(val)		// If the bit needs to be set
		temp = temp | mask;
	else
		temp = temp & ~mask;

	// Update the image value with the value we're about to write
	port_images[chip_number][bit_number /8] = temp;

	// Now actally update the port. Only the specified bit is affected
	outb(temp, port);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static void UIO48_set_bit(int chip_num, int bit_num)
{
	write_bit(chip_num, bit_num, 1);
}

static void clr_bit(int chip_num, int bit_num)
{
	write_bit(chip_num, bit_num, 0);
}

static void enab_int(int chip_number, int bit_number, int polarity)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust for 0 based numbering
	--chip_number;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&mtx[chip_number]);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port[chip_number] + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Set the enable bit for our bit number
	temp = temp | mask;

	// Now update the interrupt enable register
	outb(temp,port);

	// Turn on access to page 1 for polarity control
	outb(PAGE1 | (~PAGE3 & inb(base_port[chip_number] + 7)), base_port[chip_number] + 7);

	// Get the current state of the polarity register
	temp = inb(port);

	// Set the polarity according to the argument in the image value
	if(polarity)
	    temp = temp | mask;
    else
	    temp = temp & ~mask;

	// Write out the new polarity value
	outb(temp, port);

	// Set access back to page 0
	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static void disab_int(int chip_number, int bit_number)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust for 0 based numbering
	--chip_number;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	mutex_lock_interruptible(&mtx[chip_number]);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port[chip_number] + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// clear the enable bit for our bit number
	temp = temp & ~mask;

	// Now update the interrupt enable register
	outb(temp, port);

	// Set access back to page 0
	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static void clr_int(int chip_number, int bit_number)
{
	unsigned port;
	unsigned temp;
	unsigned mask;

	// Adjust for 0 based numbering
	--chip_number;

	// Also adjust bit number
	--bit_number;

	// obtain lock
	spin_lock(&spnlck[chip_number]);
	//mutex_lock_interruptible(&mtx[chip_number]);

	// Calculate the I/O address based upon bit number
	port = (bit_number / 8) + base_port[chip_number] + 8;

	// Calculate a bit mask based upon the specified bit number
	mask = (1 << (bit_number % 8));

	// Turn on page 2 access
	outb(PAGE2 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Temporarily clear only our enable. This clears the interrupt
	temp = temp & ~mask;    // Clear the enable for this bit

	// Now update the interrupt enable register
	outb(temp, port);

	// Re-enable our interrupt bit
	temp = temp | mask;

	outb(temp, port);

	// Set access back to page 0
	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	spin_unlock(&spnlck[chip_number]);
	//mutex_unlock(&mtx[chip_number]);
}

static int get_int(int chip_number)
{
	int temp;
	int x;

	// Adjust the chip number for 0 based numbering
	--chip_number;

	// obtain lock
	spin_lock(&spnlck[chip_number]);
	//mutex_lock_interruptible(&mtx[chip_number]);

	// Read the master interrupt pending register,
	// mask off undefined bits
	temp = inb(base_port[chip_number]+6) & 0x07;

	// If there are no pending interrupts, return 0
	if((temp & 7) == 0)
	{
		spin_unlock(&spnlck[chip_number]);
		//mutex_unlock(&mtx[chip_number]);	//release lock
		return 0;
	}

	// Set access to page 3 for interrupt id register
	outb(PAGE3 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// Read the interrupt ID register for port 0
	temp = inb(base_port[chip_number]+8);

	// See if any bit set, if so return the bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
	    {
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);
				spin_unlock(&spnlck[chip_number]);
				//mutex_unlock(&mtx[chip_number]);	//release lock
				return(x+1);
            }
        }
	}

	// None in port 0, read port 1 interrupt ID register
	temp = inb(base_port[chip_number]+9);

	// See if any bit set, if so return the bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
		{
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);
				spin_unlock(&spnlck[chip_number]);
				//mutex_unlock(&mtx[chip_number]);	//release lock
				return(x+9);
			}
	    }
	}

	// Lastly, read the statur of port 2 interrupt ID register
	temp = inb(base_port[chip_number]+0x0a);

	// If any pending, return the appropriate bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
	    {
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);
				spin_unlock(&spnlck[chip_number]);
				//mutex_unlock(&mtx[chip_number]);	//release lock
				return(x+17);
			}
	    }
	}

	// We should never get here unless the hardware is seriously
	// misbehaving, but just to be sure, we'll turn the page access
	// back to 0 and return a 0 for no interrupt found

	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	spin_unlock(&spnlck[chip_number]);
	//mutex_unlock(&mtx[chip_number]);	//release lock

	return 0;
}

static int get_buffered_int(int chip_number)
{
	int temp;

	// Adjust for 0 based numbering
	--chip_number;

	if(irq[chip_number] == 0)
	{
	    temp = get_int(chip_number+1);

		if(temp)
			clr_int(chip_number+1, temp);

		return temp;
	}

	if(outptr[chip_number] != inptr[chip_number])
	{
	    temp = int_buffer[chip_number][outptr[chip_number]++];

		if(outptr[chip_number] == MAX_INTS)
			outptr[chip_number] = 0;

		return temp;
	}

	return 0;
}

static void clr_int_id(int chip_number, int port_number)
{
	// Adjust the chip number for 0 based numbering
	--chip_number;

	// obtain lock before writing
	mutex_lock_interruptible(&mtx[chip_number]);

	// Set access to page 3 for interrupt id register
	outb(PAGE3 | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	// write to specified int_id register
	outb(0, base_port[chip_number] + 8 + port_number);

	// Reset access to page 0
	outb(~PAGE3 & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static void lock_port(int chip_number, int port_number)
{
	// Adjust the chip number for 0 based numbering
	--chip_number;

	// obtain lock before writing
	mutex_lock_interruptible(&mtx[chip_number]);

	// write to specified int_id register
	outb(1 << port_number | inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

static void unlock_port(int chip_number, int port_number)
{
	// Adjust the chip number for 0 based numbering
	--chip_number;

	// obtain lock before writing
	mutex_lock_interruptible(&mtx[chip_number]);

	// write to specified int_id register
	outb(~(1 << port_number) & inb(base_port[chip_number] + 7), base_port[chip_number] + 7);

	//release lock
	mutex_unlock(&mtx[chip_number]);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WinSystems,Inc. UIO48 Digital I/O Driver");
MODULE_AUTHOR("Paul DeMetrotion");
