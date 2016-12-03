// USB Visoly GBA Flash Advance Xtreme Linker Driver
// for kernel 2.6.x
// kernow 04/05
// kernow@stalemeat.net
// 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <asm/uaccess.h>
#include <linux/usb.h>



// product and vendor ID information
#define FLASHADVANCE_VENDOR_ID	0x5655
#define FLASHADVANCE_PRODUCT_ID	0x4144

// after upload of firmware to the EZ-USB microcontroller, I believe the PRODUCT ID changes to 0x4144
// it DOES!, thing is we use flxload to load the firmware, and not ourselves :(



/* table of devices that work with this driver */
static struct usb_device_id id_table [] = {
     { USB_DEVICE(FLASHADVANCE_VENDOR_ID, FLASHADVANCE_PRODUCT_ID) },
     { }					/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, id_table);


/* Get a minor range for your devices from the usb maintainer */
#define FLASHADVANCE_MINOR_BASE	192


/* Structure to hold all of our device specific stuff */
struct flashadvance {
   struct usb_device *	dev;			/* the usb device for this device */
   struct usb_interface *	interface;		/* the interface for this device */
   unsigned char *		bulk_in_buffer;		/* the buffer to receive data */
   size_t			bulk_in_size;		/* the size of the receive buffer */
   __u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
   __u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
   struct kref		kref;
};
#define to_flashadvance_dev(d) container_of(d, struct flashadvance, kref)


static struct usb_driver flashadvance_driver;



static void flashadvance_delete(struct kref *kref)
{	
  struct flashadvance *dev = to_flashadvance_dev(kref);
  
  usb_put_dev(dev->dev);
  kfree (dev->bulk_in_buffer);
  kfree (dev);
}

static int flashadvance_open(struct inode *inode, struct file *file)
{
   struct flashadvance *dev;
   struct usb_interface *interface;
   int subminor;
   int retval = 0;
   
   subminor = iminor(inode);
   
   interface = usb_find_interface(&flashadvance_driver, subminor);
   if (!interface) {
      err ("%s - error, can't find device for minor %d",
	   __FUNCTION__, subminor);
      retval = -ENODEV;
      goto exit;
   }
   
   dev = usb_get_intfdata(interface);
   if (!dev) {
      retval = -ENODEV;
      goto exit;
   }
   
   /* increment our usage count for the device */
   kref_get(&dev->kref);
   
   /* save our object in the file's private structure */
   file->private_data = dev;
   
   exit:
   return retval;
}

static int flashadvance_release(struct inode *inode, struct file *file)
{
   struct flashadvance *dev;
   
   dev = (struct flashadvance *)file->private_data;
   if (dev == NULL)
     return -ENODEV;
   
   /* decrement the count on our device */
   kref_put(&dev->kref, flashadvance_delete);
   return 0;
}

static ssize_t flashadvance_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
   struct flashadvance *dev;
   int retval = 0;
   
   dev = (struct flashadvance *)file->private_data;
   
   /* do a blocking bulk read to get data from the device */
   retval = usb_bulk_msg(dev->dev,
			 usb_rcvbulkpipe(dev->dev, dev->bulk_in_endpointAddr),
			 dev->bulk_in_buffer,
			 min(dev->bulk_in_size, count),
			 &count, HZ*3);
   // 3 seconds? 3HZ
   
   /* if the read was successful, copy the data to userspace */
   if (!retval) {
      if (copy_to_user(buffer, dev->bulk_in_buffer, count))
	retval = -EFAULT;
      else
	retval = count;
   }
   
   return retval;
}

static void flashadvance_write_bulk_callback(struct urb *urb, struct pt_regs *regs)
{
   struct flashadvance *dev;
   
   dev = (struct flashadvance *)urb->context;
   
   /* sync/async unlink faults aren't errors */
   if (urb->status && 
       !(urb->status == -ENOENT || 
	 urb->status == -ECONNRESET ||
	 urb->status == -ESHUTDOWN)) {
      dbg("%s - nonzero write bulk status received: %d",
	  __FUNCTION__, urb->status);
   }
   
   /* free up our allocated buffer */
   usb_buffer_free(urb->dev, urb->transfer_buffer_length, 
		   urb->transfer_buffer, urb->transfer_dma);
}

static ssize_t flashadvance_write(struct file *file, const char *user_buffer, size_t count, loff_t *ppos)
{
   struct flashadvance *dev;
   int retval = 0;
   struct urb *urb = NULL;
   char *buf = NULL;
   
   dev = (struct flashadvance *)file->private_data;
   
   /* verify that we actually have some data to write */
   if (count == 0)
     goto exit;
   
   /* create a urb, and a buffer for it, and copy the data to the urb */
   urb = usb_alloc_urb(0, GFP_KERNEL);
   if (!urb) {
      retval = -ENOMEM;
      goto error;
   }
   
   buf = usb_buffer_alloc(dev->dev, count, GFP_KERNEL, &urb->transfer_dma);
   if (!buf) {
      retval = -ENOMEM;
      goto error;
   }
   
   if (copy_from_user(buf, user_buffer, count)) {
      retval = -EFAULT;
      goto error;
   }
   
   /* initialize the urb properly */
   usb_fill_bulk_urb(urb, dev->dev,
		     usb_sndbulkpipe(dev->dev, dev->bulk_out_endpointAddr),
		     buf, count, flashadvance_write_bulk_callback, dev);
   urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
   
   /* send the data out the bulk port */
   retval = usb_submit_urb(urb, GFP_KERNEL);
   if (retval) {
      err("%s - failed submitting write urb, error %d", __FUNCTION__, retval);
		goto error;
   }
   
   /* release our reference to this urb, the USB core will eventually free it entirely */
   usb_free_urb(urb);
   
   exit:
   return count;
   
   error:
   usb_buffer_free(dev->dev, count, buf, urb->transfer_dma);
   usb_free_urb(urb);
   return retval;
}

static struct file_operations flashadvance_fops = {
   .owner =	THIS_MODULE,
     .read =		flashadvance_read,
     .write =	flashadvance_write,
     .open =		flashadvance_open,
     .release =	flashadvance_release,
};

/* 
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver flashadvance_class = {
   .name =		"usb/fax%d",
     .fops =		&flashadvance_fops,
     .mode =		S_IFCHR | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH,
     .minor_base =	FLASHADVANCE_MINOR_BASE,
};

static int flashadvance_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
   struct flashadvance *dev = NULL;
   struct usb_host_interface *iface_desc;
   struct usb_endpoint_descriptor *endpoint;
   size_t buffer_size;
   int i;
   int retval = -ENOMEM;
   
   /* allocate memory for our device state and initialize it */
   dev = kmalloc(sizeof(*dev), GFP_KERNEL);
   if (dev == NULL) {
      err("Out of memory");
      goto error;
   }
   memset(dev, 0x00, sizeof(*dev));
   kref_init(&dev->kref);
   
   dev->dev = usb_get_dev(interface_to_usbdev(interface));
   dev->interface = interface;
   
   /* set up the endpoint information */
   /* use only the first bulk-in and bulk-out endpoints */
   iface_desc = interface->cur_altsetting;
   for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
      endpoint = &iface_desc->endpoint[i].desc;
      
      if (!dev->bulk_in_endpointAddr &&
	  (endpoint->bEndpointAddress & USB_DIR_IN) &&
	  ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	   == USB_ENDPOINT_XFER_BULK)) {
	 /* we found a bulk in endpoint */
	 buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
	 dev->bulk_in_size = buffer_size;
	 dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
	 dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
	 if (!dev->bulk_in_buffer) {
	    err("Could not allocate bulk_in_buffer");
	    goto error;
	 }
      }
      
      if (!dev->bulk_out_endpointAddr &&
	  !(endpoint->bEndpointAddress & USB_DIR_IN) &&
	  ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
	   == USB_ENDPOINT_XFER_BULK)) {
	 /* we found a bulk out endpoint */
	 dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
      }
   }
   if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
      err("Could not find both bulk-in and bulk-out endpoints");
      goto error;
      
   }
   
   
   // now we have the ezusb controller functions in here ^_^ we can load the firmware maybe
   // and reset the cpu, that is if the firmware is in the right bloody header file format
   
   // hmm why does this code complain?
   // because the firmware isnt loaded yet, and its not a real linker until it is.. doh!  
   
   /* save our data pointer in this interface device */
   usb_set_intfdata(interface, dev);
   
   /* we can register the device now, as it is ready */
   retval = usb_register_dev(interface, &flashadvance_class);
   if (retval) {
      /* something prevented us from registering this driver */
      err("Not able to get a minor for this device.");
      usb_set_intfdata(interface, NULL);
      goto error;
   }
   
   dev_info(&interface->dev, "USB Visoly Flash Advance Xtreme now attached.\n");
   return 0;
   
   error:
   if (dev)
     kref_put(&dev->kref, flashadvance_delete);
   return retval;
}

static void flashadvance_disconnect(struct usb_interface *interface)
{
   struct flashadvance *dev;
   
   /* prevent skel_open() from racing skel_disconnect() */
   lock_kernel();
   
   dev = usb_get_intfdata(interface);
   usb_set_intfdata(interface, NULL);
   
   /* give back our minor */
   usb_deregister_dev(interface, &flashadvance_class);
   
   unlock_kernel();
   
   /* decrement our usage count */
   kref_put(&dev->kref, flashadvance_delete);
   
   info("USB Visoly Flash Advance Xtreme now disconnected.");
}

static struct usb_driver flashadvance_driver = {
   .owner =	THIS_MODULE,
     .name =		"flashadvance",
     .probe =	flashadvance_probe,
     .disconnect =	flashadvance_disconnect,
     .id_table =	id_table,
};

static int __init flashadvance_init(void)
{
   int result;
   
   /* register this driver with the USB subsystem */
   result = usb_register(&flashadvance_driver);
   if (result)
     err("usb_register failed. Error number %d", result);
   
   return result;
}

static void __exit flashadvance_exit(void)
{
   /* deregister this driver with the USB subsystem */
   usb_deregister(&flashadvance_driver);
}

module_init (flashadvance_init);
module_exit (flashadvance_exit);

MODULE_DESCRIPTION("USB Flash Linker Driver : Ver 0.0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shaun Bradley, kernow@stalemeat.net");

