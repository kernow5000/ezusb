// USB Visoly GBA Flash Advance Xtreme Linker driver
// for kernel 2.6.x
// kernow 03/05
// kernow@stalemeat.net

// now loads and probes the hardware!! detects the linker properly :)

#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/usb.h>


MODULE_DESCRIPTION("USB Flash Linker Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shaun Bradley, kernow@stalemeat.net");


#define VENDOR_ID  0x5655
#define PRODUCT_ID 0x4149



static struct usb_device_id id_table [] = {
     { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
     { },
};

MODULE_DEVICE_TABLE (usb, id_table);





struct flashlinker
{
   struct usb_device *     udev;
   unsigned char           data;
};



static int linker_probe (struct usb_interface *interface, const struct usb_device_id *id)
{
   
   struct usb_device *udev = interface_to_usbdev(interface);
   struct flashlinker *dev = NULL;
   int retval = -ENOMEM;
   
   
   dev = kmalloc(sizeof(struct flashlinker), GFP_KERNEL);
   if (dev == NULL) 
     {	
	dev_err(&interface->dev, "Out of memory\n");
	goto error;
     }
   
   memset (dev, 0x00, sizeof (*dev));
   dev->udev = usb_get_dev(udev);
   usb_set_intfdata (interface, dev);
   
   dev_info(&interface->dev, "USB Visoly Flash Advance Xtreme now attached.\n");
   return 0;
   
   error:
   kfree(dev);
   return retval;
}



static void linker_disconnect (struct usb_interface *interface)
{
   
   // is this not running?
   // should it run when the linker is actually physically disconnected?
   // as I think its only running when the module is unloaded. :(
   
   struct flashlinker *dev;
   
   dev = usb_get_intfdata (interface);
   usb_set_intfdata (interface, NULL);
   
   //device_remove_file(&interface->dev, &dev_attr_green);
   
   usb_put_dev(dev->udev);
   
   kfree(dev);
   
   // hmm its not disconnected though, only the modules been taken out! :)
   //dev_info(&interface->dev, "USB Visoly Flash Advance Xtreme disconnected.\n");
   
}



static struct usb_driver flashlinker_driver = {
   .owner =        THIS_MODULE,
   .name =         "flashadvance",
   .probe =        linker_probe,
   .disconnect =   linker_disconnect,
   .id_table =     id_table,
};



static int __init mymodule_init(void)
{
   printk(KERN_ALERT "USB Visoly Flash Advance Xtreme driver, kernow@stalemeat.net\n");
   
   // do some init stuff here
   // 
   // 
   // 
   
   int retval = 0;
   
   retval = usb_register(&flashlinker_driver);
   if (retval)
     err("usb_register failed. Error number %d", retval);
   return retval;
   
   return 0; 
}

static void __exit mymodule_cleanup(void)
{
   //printk(KERN_ALERT "Goodbye, from kerns module.\n");
   //cleanup here
   usb_deregister(&flashlinker_driver);
}



module_init(mymodule_init);
module_exit(mymodule_cleanup);

