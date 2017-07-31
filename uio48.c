/*
 * uio48.c: UIO48 Digital I/O Driver
 *
 * (C) Copyright 2011, 2016 by WinSystems, Inc.
 * Author: Paul DeMetrotion <pdemetrotion@winsystems.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

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

// ******************* Device Declarations *****************************

#define MAX_INTS 1024

struct uio48_dev {
	char name[32];
	unsigned irq;
	unsigned char int_buffer[MAX_INTS];
	int inptr;
	int outptr;
	wait_queue_head_t wq;
	struct mutex mtx;
	spinlock_t spnlck;
	struct cdev cdev;
	unsigned base_port;
	unsigned char port_images[6];
	int ready;
};

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

// Driver major number
static int uio48_init_major;	// 0 = allocate dynamically

// Page defintions
#define PAGE0		0x0
#define PAGE1		0x40
#define PAGE2		0x80
#define PAGE3		0xc0

// Our modprobe command line arguments
static unsigned io[MAX_CHIPS];
static unsigned irq[MAX_CHIPS];

MODULE_PARM_DESC(io, "Array of IO addresses for devices");
module_param_array(io, uint, NULL, S_IRUGO);
MODULE_PARM_DESC(irq, "Array of IRQ routes for devices");
module_param_array(irq, uint, NULL, S_IRUGO);

static struct uio48_dev uiodevs[MAX_CHIPS];

static struct class *uio48_class;
static dev_t uio48_devno;

/* UIO48 ISR */
static irqreturn_t irq_handler(int __irq, void *dev_id)
{
	struct uio48_dev *uiodev = dev_id;
	int c;

	while ((c = get_int(uiodev))) {
		clr_int(uiodev, c);

		pr_devel("[%s] interrupt on bit %d\n", uiodev->name, c);

		uiodev->int_buffer[uiodev->inptr++] = c;

		if (uiodev->inptr == MAX_INTS)
			uiodev->inptr = 0;

		uiodev->ready = 1;
		wake_up(&uiodev->wq);
	}

	return IRQ_HANDLED;
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

	uio48_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(uio48_class)) {
		pr_err("Could not create module class\n");
		return PTR_ERR(uio48_class);
	}

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

	for (x = io_num = 0; x < MAX_CHIPS; x++) {
		struct uio48_dev *uiodev = &uiodevs[x];

		/* If no IO port, skip this idx. */
		if (io[x] == 0)
			continue;

		mutex_init(&uiodev->mtx);
		spin_lock_init(&uiodev->spnlck);
		init_waitqueue_head(&uiodev->wq);

		dev = uio48_devno + x;

		/* Initialize char device. */
		cdev_init(&uiodev->cdev, &uio48_fops);
		ret_val = cdev_add(&uiodev->cdev, dev, 1);

		if (ret_val) {
			pr_err("Error adding character device for node %d\n", x);
			return ret_val;
		}

		/* Check and map our I/O region requests. */
		if (request_region(io[x], 0x10, KBUILD_MODNAME) == NULL) {
			pr_err("Unable to use I/O Address %04X\n", io[x]);
			cdev_del(&uiodev->cdev);
			continue;
		}

		init_io(uiodev, io[x]);

		/* Check and map any interrupts. */
		if (irq[x]) {
			if (request_irq(irq[x], irq_handler, IRQF_SHARED, KBUILD_MODNAME, uiodev)) {
				pr_err("Unable to register IRQ %d\n", irq[x]);
				release_region(io[x], 0x10);
				cdev_del(&uiodev->cdev);
				continue;
			}

			uiodev->irq = irq[x];
		}

		io_num++;

		sprintf(uiodev->name, KBUILD_MODNAME "%c", 'a' + x);

		pr_info("[%s] Added new device\n", uiodev->name);

		device_create(uio48_class, NULL, dev, NULL, "%s", uiodev->name);
	}

	if (io_num)
		return 0;

	pr_warning("No resources available, driver terminating\n");

	class_destroy(uio48_class);
	unregister_chrdev_region(uio48_devno, MAX_CHIPS);

	return -ENODEV;
}

///**********************************************************************
//			CLEANUP MODULE
///**********************************************************************
// unregister the appropriate file from /proc
void cleanup_module()
{
	int x;

	/* Unregister I/O port usage and IRQ */
	for (x = 0; x < MAX_CHIPS; x++) {
		struct uio48_dev *uiodev = &uiodevs[x];

		if (uiodev->base_port)
			release_region(uiodev->base_port, 0x10);

		if (uiodev->irq)
			free_irq(uiodev->irq, uiodev);
	}

	device_destroy(uio48_class, uio48_devno);
	class_destroy(uio48_class);
	unregister_chrdev_region(uio48_devno, MAX_CHIPS);
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
	for (x = 0; x < 6; x++)
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
	int i, t, ret = 0;

	spin_lock(&uiodev->spnlck);

	/* Read the master interrupt pending register, mask off undefined
	 * bits. */
	t = inb(base_port + 6) & 0x07;

	/* If there are no pending interrupts, return 0. */
	if (t == 0) {
		spin_unlock(&uiodev->spnlck);
		return 0;
	}

	/* Set access to page 3 for interrupt id register. */
	outb(PAGE3 | inb(base_port + 7), base_port + 7);

	/* Check ports 0, 1, and 2 for interrupt ID register. */
	for (i = 0; i < 3; i++) {
		int j;

		/* Read the interrupt ID register for port. */
		t = inb(base_port + 8 + i);

		/* See if any bit set, if so return the bit number. */
		if (t == 0)
			continue;

		for (j = 0; j <= 7; j++) {
			if (!(t & (1 << j)))
				continue;

			ret = (j + 1 + (8 * i));
			goto isr_out;
		}
	}

	/* We should never get here unless the hardware is seriously
	 * misbehaving. */
	WARN_ONCE(1, KBUILD_MODNAME ": Encountered superflous interrupt");

isr_out:
	outb(~PAGE3 & inb(base_port + 7), base_port + 7);

	spin_unlock(&uiodev->spnlck);

	return ret;
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
