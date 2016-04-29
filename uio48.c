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
#endif

#include "uio48.h"

#define DRVR_NAME	"uio48"
#define DRVR_VERSION	"4.0"
#define DRVR_RELDATE	"1Dec2011"

// #define DEBUG 1

#ifdef DEBUG
#define pr_dbg(...) printk("<1>UIO48 -" __VA_ARGS__)
#else
#define pr_dbg(...) do{}while(0)
#endif

struct uio48_dev;

// Function prototypes for local functions
static void init_io(struct uio48_dev *uiodev, unsigned base_port);
static int read_bit(struct uio48_dev *uiodev, int bit_number);
static void write_bit(struct uio48_dev *uiodev, int bit_number, int val);
static void UIO48_set_bit(struct uio48_dev *uiodev, int bit_num);
static void clr_bit(struct uio48_dev *uiodev, int bit_num);
static void enab_int(struct uio48_dev *uiodev, int bit_number, int polarity);
static void disab_int(struct uio48_dev *uiodev, int bit_number);
static void clr_int(struct uio48_dev *uiodev, int bit_number);
static int get_int(struct uio48_dev *uiodev);
static int get_buffered_int(struct uio48_dev *uiodev);
static void clr_int_id(struct uio48_dev *uiodev, int port_number);
static void lock_port(struct uio48_dev *uiodev, int port_number);
static void unlock_port(struct uio48_dev *uiodev, int port_number);

// ******************* Device Declarations *****************************

#define MAX_INTS 1024

struct uio48_dev {
	int id;
	unsigned irq;
	unsigned char int_buffer[MAX_INTS];
	int inptr;
	int outptr;
	wait_queue_head_t wq;
	struct mutex mtx;
	spinlock_t spnlck;
	struct cdev uio48_cdev;
	unsigned base_port;
	unsigned char port_images[6];
};

// Driver major number
static int uio48_init_major;	// 0 = allocate dynamically
static int uio48_major;

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

static struct uio48_dev uiodevs[MAX_CHIPS];

/* UIO48 ISR */
static irqreturn_t irq_handler(int __irq, void *dev_id)
{
	struct uio48_dev *uiodev = dev_id;

	while (1) {
		int c;

		// obtain irq bit
		c = get_int(uiodev);

		if (c == 0)
			break;

		clr_int(uiodev, c);

		pr_dbg("Interrupt on chip %d, bit %d\n", uiodev->id, c);

		uiodev->int_buffer[uiodev->inptr++] = c;

		if (uiodev->inptr == MAX_INTS)
			uiodev->inptr = 0;

		wake_up_interruptible(&uiodev->wq);
	}

	return IRQ_HANDLED;
}

///**********************************************************************
//			DEVICE OPEN
///**********************************************************************
// called whenever a process attempts to open the device file
static int device_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct uio48_dev *uiodev = &uiodevs[minor];

	if (uiodev->base_port == 0) {
		printk("<1>UIO48 **** OPEN ATTEMPT on uninitialized port *****\n");
		return -1;
	}

	file->private_data = uiodev;

	pr_dbg("device_open(%p)\n", file);

	return SUCCESS;
}

///**********************************************************************
//			DEVICE CLOSE
///**********************************************************************

static int device_release(struct inode *inode, struct file *file)
{
	pr_dbg("device_release(%p,%p)\n", inode, file);

	file->private_data = NULL;

	return 0;
}

///**********************************************************************
//			DEVICE IOCTL
///**********************************************************************
static long device_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	struct uio48_dev *uiodev = file->private_data;
	int i, port, ret_val;
	int minor __attribute__((unused)) = uiodev->id;

	pr_dbg("IOCTL call minor %d,IOCTL CODE %04X\n", minor, ioctl_num);

	// Switch according to the ioctl called
	switch (ioctl_num) {
	case IOCTL_READ_PORT:
		port = (ioctl_param & 0xff);
		ret_val = inb(uiodev->base_port + port);
		return ret_val;

	case IOCTL_WRITE_PORT:
		// obtain lock before writing
		mutex_lock_interruptible(&uiodev->mtx);

		port = (ioctl_param >> 8) & 0xff;
		ret_val = ioctl_param & 0xff;

		outb(ret_val, uiodev->base_port + port);

		//release lock
		mutex_unlock(&uiodev->mtx);

		pr_dbg("Writing to I/O Port %04x with%02x\n", uiodev->base_port + port, ret_val);

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
		pr_dbg("IOCTL Set_int Device %d Bit %d polarity %d\n",
			minor + 1, (int)(ioctl_param >> 8), (int)(ioctl_param & 0xff));

		enab_int(uiodev, (int)(ioctl_param >> 8), (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_DISAB_INT:
		pr_dbg("IOCTL Disab_Int Device %d Bit %d\n",
			minor + 1, (int)(ioctl_param & 0xff));

		disab_int(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_CLR_INT:
		pr_dbg("IOCTL Clr_int Device %d Bit %d\n", minor + 1, (int)(ioctl_param & 0xff));

		clr_int(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_GET_INT:
		pr_dbg("IOCTL get_int device %d\n", minor + 1);

		i = get_buffered_int(uiodev);
		return i;

	case IOCTL_WAIT_INT:
		pr_dbg("IOCTL wait_int device %d\n", minor + 1);

		if ((i = get_buffered_int(uiodev)))
			return i;

		// Code to put current process to sleep awaiting interrupt
		pr_dbg("current process %i (%s) going to sleep\n",
			current->pid, current->comm);

		wait_event_interruptible(uiodev->wq, 1);

		pr_dbg("awoken %i (%s)\n", current->pid, current->comm);

		// Getting here does not guarantee that there's an in interrupt available
		// we may have been awakened by some other signal. In any case We'll
		// return whatever's available in the interrupt queue even if it's empty
		i = get_buffered_int(uiodev);

		return i;

	case IOCTL_CLR_INT_ID:
		pr_dbg("IOCTL Clr_Int_Id Device %d Port %d\n", minor + 1, (int)(ioctl_param & 0xff));

		clr_int_id(uiodev, (int)(ioctl_param&0xff));
		return SUCCESS;

	case IOCTL_LOCK_PORT:
		pr_dbg("IOCTL Lock_Port Device %d Port %d\n", minor + 1, (int)(ioctl_param & 0xff));

		lock_port(uiodev, (int)(ioctl_param & 0xff));
		return SUCCESS;

	case IOCTL_UNLOCK_PORT:
		pr_dbg("IOCTL Unock_Port Device %d Port %d\n", minor + 1, (int)(ioctl_param & 0xff));

		unlock_port(uiodev, (int)(ioctl_param & 0xff));
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
	dev_t devno;
	int x;

	// Sign-on
	printk("<1>WinSystems, Inc. UIO48 Linux Device Driver\n");
	printk("<1>Copyright 2002-2011, All rights reserved\n");
	printk("<1>%s\n", RCSInfo);

	// register the character device
	if (uio48_init_major) {
		uio48_major = uio48_init_major;
		devno = MKDEV(uio48_major, 0);
		ret_val = register_chrdev_region(devno, 1, DRVR_NAME);
	} else {
		ret_val = alloc_chrdev_region(&devno, 0, 1, DRVR_NAME);
		uio48_major = MAJOR(devno);
	}

	if (ret_val < 0) {
		printk("<1>UIO48 - Cannot obtain major number %d\n", uio48_major);
		return -ENODEV;
	} else {
		printk("<1>UIO48 - Major number %d assigned\n", uio48_major);
	}

	for (x = io_num = 0; x < MAX_CHIPS; x++) {
		struct uio48_dev *uiodev = &uiodevs[x];

		if (io[x] == 0)	// is device required?
			continue;

		// initialize mutex array
		mutex_init(&uiodev->mtx);

		// initialize spinlock array
		spin_lock_init(&uiodev->spnlck);

		uiodev->id = x;

		// Initialize char device
		cdev_init(&uiodev->uio48_cdev, &uio48_fops);
		uiodev->uio48_cdev.owner = THIS_MODULE;
		uiodev->uio48_cdev.ops = &uio48_fops;
		ret_val = cdev_add(&uiodev->uio48_cdev, MKDEV(uio48_major, x), MAX_CHIPS);

		if (!ret_val) {
			printk("<1>UIO48 - Added character device %s node %d\n", DRVR_NAME, x);
		} else {
			printk("<1>UIO48 - Error %d adding character device %s node %d\n", ret_val, DRVR_NAME, x);
			goto exit_cdev_delete;
		}

		// check and map our I/O region requests
		if (request_region(io[x], 0x10, DRVR_NAME) == NULL) {
			printk("<1>UIO48 - Unable to use I/O Address %04X\n", io[x]);
			io[x] = 0;
			continue;
		} else {
			printk("<1>UIO48 - Base I/O Address = %04X\n", io[x]);
			init_io(uiodev, io[x]);
			io_num++;
		}

		// check and map any interrupts
		if (irq[x]) {
			if (request_irq(irq[x], irq_handler, IRQF_SHARED, DRVR_NAME, uiodev)) {
				printk("<1>UIO48 - Unable to register IRQ %d\n", irq[x]);
			} else {
				uiodev->irq = irq[x];
				printk("<1>UIO48 - IRQ %d registered to Chip %d\n", irq[x], x + 1);
			}
		}
	}

	if (io_num)
		return SUCCESS;

	printk("<1>UIO48 - No resources available, driver terminating\n");

exit_cdev_delete:
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
	for (x = 0; x < MAX_CHIPS; x++) {
		struct uio48_dev *uiodev = &uiodevs[x];

		if (uiodev->base_port)
			release_region(uiodev->base_port, 0x10);

		if (uiodev->irq)
			free_irq(uiodev->irq, uiodev);
	}

	unregister_chrdev_region(MKDEV(uio48_major, 0), 1);
	uio48_major = 0;
}

// ******************* Device Subroutines *****************************

static void init_io(struct uio48_dev *uiodev, unsigned base_port)
{
	int x;

	// obtain lock
	mutex_lock_interruptible(&uiodev->mtx);

	// save the address for later use
	uiodev->base_port = base_port;

	// Clear all of the I/O ports. This also makes them inputs
	for (x = 0; x < 7; x++)
		outb(0, base_port + x);

	// Clear the image values as well
	for (x = 0; x < 6; x++)
		uiodev->port_images[x] = 0;

	// Set page 2 access, for interrupt enables
	outb(PAGE2 | inb(base_port + 7), base_port + 7);

	// Clear all interrupt enables
	outb(0, base_port + 8);
	outb(0, base_port + 9);
	outb(0, base_port + 0x0a);

	// Restore page 0 register access
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static int read_bit(struct uio48_dev *uiodev, int bit_number)
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
	outb(PAGE2 | inb(base_port + 7), base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// Set the enable bit for our bit number
	temp = temp | mask;

	// Now update the interrupt enable register
	outb(temp,port);

	// Turn on access to page 1 for polarity control
	outb(PAGE1 | (~PAGE3 & inb(base_port + 7)), base_port + 7);

	// Get the current state of the polarity register
	temp = inb(port);

	// Set the polarity according to the argument in the image value
	if (polarity)
		temp = temp | mask;
	else
		temp = temp & ~mask;

	// Write out the new polarity value
	outb(temp, port);

	// Set access back to page 0
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

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
	outb(PAGE2 | inb(base_port + 7), base_port + 7);

	// Get the current state of the interrupt enable register
	temp = inb(port);

	// clear the enable bit for our bit number
	temp = temp & ~mask;

	// Now update the interrupt enable register
	outb(temp, port);

	// Set access back to page 0
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

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
	outb(PAGE2 | inb(base_port + 7), base_port + 7);

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
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

	//release lock
	spin_unlock(&uiodev->spnlck);
}

static int get_int(struct uio48_dev *uiodev)
{
	unsigned base_port = uiodev->base_port;
	int temp;
	int x;

	// obtain lock
	spin_lock(&uiodev->spnlck);

	// Read the master interrupt pending register,
	// mask off undefined bits
	temp = inb(base_port + 6) & 0x07;

	// If there are no pending interrupts, return 0
	if ((temp & 7) == 0) {
		spin_unlock(&uiodev->spnlck);
		return 0;
	}

	// Set access to page 3 for interrupt id register
	outb(PAGE3 | inb(base_port + 7), base_port + 7);

	// Read the interrupt ID register for port 0
	temp = inb(base_port + 8);

	// See if any bit set, if so return the bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
	    {
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port + 7), base_port + 7);
				spin_unlock(&uiodev->spnlck);
				return(x+1);
            }
        }
	}

	// None in port 0, read port 1 interrupt ID register
	temp = inb(base_port + 9);

	// See if any bit set, if so return the bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
		{
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port + 7), base_port + 7);
				spin_unlock(&uiodev->spnlck);
				return(x+9);
			}
	    }
	}

	// Lastly, read the statur of port 2 interrupt ID register
	temp = inb(base_port + 0x0a);

	// If any pending, return the appropriate bit number
	if(temp != 0)
	{
	    for(x=0; x<=7; x++)
	    {
			if(temp & (1 << x))
			{
				outb(~PAGE3 & inb(base_port + 7), base_port + 7);
				spin_unlock(&uiodev->spnlck);
				return(x + 17);
			}
	    }
	}

	// We should never get here unless the hardware is seriously
	// misbehaving, but just to be sure, we'll turn the page access
	// back to 0 and return a 0 for no interrupt found

	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

	spin_unlock(&uiodev->spnlck);

	return 0;
}

static int get_buffered_int(struct uio48_dev *uiodev)
{
	int temp;

	if (uiodev->irq == 0) {
		temp = get_int(uiodev);

		if (temp)
			clr_int(uiodev, temp);

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

	// Set access to page 3 for interrupt id register
	outb(PAGE3 | inb(base_port + 7), base_port + 7);

	// write to specified int_id register
	outb(0, base_port + 8 + port_number);

	// Reset access to page 0
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void lock_port(struct uio48_dev *uiodev, int port_number)
{
	unsigned base_port = uiodev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// write to specified int_id register
	outb(1 << port_number | inb(base_port + 7), base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

static void unlock_port(struct uio48_dev *uiodev, int port_number)
{
	unsigned base_port = uiodev->base_port;

	// obtain lock before writing
	mutex_lock_interruptible(&uiodev->mtx);

	// write to specified int_id register
	outb(~(1 << port_number) & inb(base_port + 7), base_port + 7);

	//release lock
	mutex_unlock(&uiodev->mtx);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WinSystems,Inc. UIO48 Digital I/O Driver");
MODULE_AUTHOR("Paul DeMetrotion");
