#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include<linux/slab.h>
#include <stdarg.h>

#define CARD_READER_VID  0x14cd
#define CARD_READER_PID  0x125d

#define SAMSUNG_MEDIA_VID  0x04e8
#define SAMSUNG_MEDIA_PID  0x6860

#define NXP_KEIL_VID 0xc251
#define NXP_MSD_PID 0x1303

#define SANDISK_MEDIA_VID  0x0781 
#define SANDISK_MEDIA_PID  0x558a

#define BULK_EP_IN    0x81
#define BULK_EP_OUT   0x02

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24
#define READ_CAPACITY_LENGTH          0x08

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])


static struct usb_device *udev;
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

// Section 5.2: Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

static uint8_t cdb_length[256] = {
//	 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  0
	06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,06,  //  1
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  2
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  3
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  4
	10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,  //  5
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  6
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  7
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  8
	16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,  //  9
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  A
	12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,  //  B
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  C
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  D
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  E
	00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,00,  //  F
};

///////////////////////////////*SEND MASS STORAGE COMMAND*///////////////////////////////////////////////////
static int send_mass_storage_command(struct usb_device *udev, uint8_t endpoint, uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len;
	int i, r, size;
	//struct command_block_wrapper cbw;
	struct command_block_wrapper *cbw;
    	cbw = (struct command_block_wrapper *)kmalloc(sizeof(struct command_block_wrapper), GFP_KERNEL);

	if (cdb == NULL) {
		return -1;
	}

	if (endpoint & BULK_EP_IN) {
		//perr("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}

	cdb_len = cdb_length[cdb[0]];
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw->CBWCB))) {
		//perr("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			//cdb[0], cdb_len);
		return -1;
	}

	memset(cbw, 0, sizeof(struct command_block_wrapper));  //check
	cbw->dCBWSignature[0] = 'U';
	cbw->dCBWSignature[1] = 'S';
	cbw->dCBWSignature[2] = 'B';
	cbw->dCBWSignature[3] = 'C';
	*ret_tag = tag;
	cbw->dCBWTag = tag++;
	cbw->dCBWDataTransferLength = data_length;
	cbw->bmCBWFlags = direction;
	cbw->bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw->bCBWCBLength = cdb_len;
	memcpy(cbw->CBWCB, cdb, cdb_len);

	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, endpoint), (void *)cbw, 31, &size, 1000);
		if (r != 0) {
			usb_clear_halt(udev, usb_sndbulkpipe(udev, endpoint));
		}
		i++;
	} while ((r != 0) && (i<RETRY_MAX));

	//printf("   sent %d CDB bytes\n", cdb_len);
	printk(KERN_INFO "read count = %d\n", size);
	return 0;
}
///////////////////////////////////*GET STATUS*///////////////////////////////////////////////////
static int get_mass_storage_status(struct usb_device *udev, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper *csw;
    	csw = (struct command_status_wrapper *)kmalloc(sizeof(struct command_status_wrapper), GFP_KERNEL);
	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, endpoint), (void *)csw, 13, &size, 1000);
		if (r != 0) {
			usb_clear_halt(udev, endpoint);
		}
		i++;
	} while ((r != 0) && (i<RETRY_MAX));
	if (r != 0) {
		printk(KERN_INFO " get_mass_storage_status: %d\n", r);
		//return -1;
	}
	if (size != 13) {
		printk(KERN_INFO " get_mass_storage_status: received %d bytes (expected 13)\n", size);
		//return -1;
	}
	if (csw->dCSWTag != expected_tag) {
		printk(KERN_INFO " get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n", expected_tag, csw->dCSWTag);
		//return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printk(KERN_INFO "Mass Storage Status: %02X (%s)\n", csw->bCSWStatus, csw->bCSWStatus?"FAILED":"Success");
	if (csw->dCSWTag != expected_tag)
		return -1;
	if (csw->bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw->bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}

//////////////////////////////////////////*GET SENSE*///////////////////////////////////////////////////////////
static void get_sense(struct usb_device *udev, uint8_t endpoint_in, uint8_t endpoint_out)
{
	uint8_t cdb[16];	// SCSI Command Descriptor Block
	//uint8_t sense[18];
	uint32_t expected_tag;
	uint8_t length;
	int size;
	int i, ret;
     	uint8_t *sense = (uint8_t *)kmalloc(18*sizeof(uint8_t), GFP_KERNEL);
    	//get sense
    	for(i=0; i<18; i++)
	{
		*(sense+i) = 0;
	}
	memset(cdb, 0, sizeof(cdb));
	length = REQUEST_SENSE_LENGTH;	
	printk(KERN_INFO "Request Sense:\n");
	cdb[0] = 0x03; //Request Sense
	cdb[4] = length; 
	
	ret = send_mass_storage_command(udev, endpoint_out, 0, cdb, 0x80, length, &expected_tag);
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, endpoint_in), (void *)sense, length, &size, 1000);
	printk(KERN_INFO "received %d bytes\n", size);
	for(i = 0;i < length; i++)
	{	
	printk(KERN_INFO "Buffer[%d] = %d",i, sense[i]);
	}
    	if ((sense[0] != 0x70) && (sense[0] != 0x71)) {
		printk(KERN_INFO "ERROR No sense data\n");
	} else {
		printk(KERN_INFO "ERROR Sense: %02X %02X %02X\n", sense[2]&0x0F, sense[12], sense[13]);
	}
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////

static void usbdev_disconnect(struct usb_interface *interface)
{
	printk(KERN_INFO "USBDEV Device Removed\n");
	//return;
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(CARD_READER_VID, CARD_READER_PID)},
	{USB_DEVICE(SAMSUNG_MEDIA_VID, SAMSUNG_MEDIA_PID)},
	{USB_DEVICE(NXP_KEIL_VID, NXP_MSD_PID)},
	{USB_DEVICE(SANDISK_MEDIA_VID, SANDISK_MEDIA_PID)},
	{} /*terminating entry*/	
};
MODULE_DEVICE_TABLE (usb, usbdev_table);
//////////////////////////////////*PROBE FUNCTION*//////////////////////////////////////////////////////
static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int i;
	unsigned char epAddr, epAttr;
	struct usb_endpoint_descriptor *ep_desc;
	//struct usb_host_interface *if_desc;
	uint8_t length;
	int size, ret, retval;
	uint32_t expected_tag;
	int max_lba, block_size;
	int temp1, temp2;
	int device_size;
	uint8_t cdb[16];
	char vid[9], pid[9], rev[5];
	uint8_t *buffer = (uint8_t *)kmalloc(64*sizeof(uint8_t), GFP_KERNEL); 
	uint8_t *lun = (uint8_t *)kmalloc(sizeof(uint8_t), GFP_KERNEL); 
	uint8_t endpoint_in = 0, endpoint_out = 0;
	
	//struct usb_device *udev = interface_to_usbdev(interface); //in built function to convert interface to usbdev
	udev = container_of(interface->dev.parent,struct usb_device,dev);  //or use container of

	for(i=0; i<64; i++)
	{
		*(buffer+i) = 0;
	}
	if(id->idProduct == CARD_READER_PID)
	{
		printk(KERN_INFO "Camera Plugged in\n");
	}
	else if(id->idProduct == SAMSUNG_MEDIA_PID)
	{
		printk(KERN_INFO "Media Plugged in\n");
	}
	else if(id->idVendor == NXP_KEIL_VID)
	{
		printk(KERN_INFO "Blueboard Plugged in\n");
	}
	else if(id->idProduct == SANDISK_MEDIA_PID)
	{
		printk(KERN_INFO "Known Device\n");
		printk(KERN_INFO "Sandisk Media Plugged in\n");
	}


	printk(KERN_INFO "No. of Altsettings = %d\n",interface->num_altsetting);

	printk(KERN_INFO "No. of Endpoints = %d\n", interface->cur_altsetting->desc.bNumEndpoints);

	for(i=0;i<interface->cur_altsetting->desc.bNumEndpoints;i++)
	{
		ep_desc = &interface->cur_altsetting->endpoint[i].desc;
		epAddr = ep_desc->bEndpointAddress;
		epAttr = ep_desc->bmAttributes;
	
		if((epAttr & USB_ENDPOINT_XFERTYPE_MASK)==USB_ENDPOINT_XFER_BULK)
		{
			if(epAddr & 0x80)
				{
				printk(KERN_INFO "EP %d is Bulk IN\n", i);
				endpoint_in = epAddr;
				}
			else
				{
				printk(KERN_INFO "EP %d is Bulk OUT\n", i);
				endpoint_out= epAddr;
				}
	
		}

	}
	printk(KERN_INFO "endpoint in: %X, out: %X\n",endpoint_in, endpoint_out);
	////////////////////////////////////////////*GET LUN*/////////////////////////////////////////////////////////
	//get max LUN
    	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0xFE, 0xA1, 0x0, 0x0, (void *)lun, sizeof(uint8_t), 1000);

	if (retval < 0)
	{
		printk(KERN_INFO "control message error (%d)\n", retval);
	}
	else
	{
		printk(KERN_INFO "MAX LUN = %d\n", *lun);
	}	
	
	//////////////////////////////////////////////*GET INQUIRY*///////////////////////////////////////////////////
	//read inquiry
    	for(i=0; i<64; i++)
	{
		*(buffer+i) = 0;
	}
	memset(cdb, 0, sizeof(cdb));
	length = INQUIRY_LENGTH;	
	printk(KERN_INFO "Reading inquiry:\n");
	cdb[0] = 0x12; //Read inquiry
	cdb[4] = length; 
	
	ret = send_mass_storage_command(udev, endpoint_out, 0, cdb, 0x80, length, &expected_tag);
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, endpoint_in), (void *)buffer, length, &size, 1000);
	printk(KERN_INFO "received %d bytes\n", size);
	for (i=0; i<8; i++) {
		vid[i] = buffer[8+i];
		pid[i] = buffer[16+i];
		rev[i/2] = buffer[32+i/2];	// instead of another loop
	}
	vid[8] = 0;
	pid[8] = 0;
	rev[4] = 0;
	printk(KERN_INFO "VID:PID:REV \"%8s\":\"%8s\":\"%4s\"\n", vid, pid, rev);
    	if (get_mass_storage_status(udev, endpoint_in, expected_tag) == -2) {
		get_sense(udev, endpoint_in, endpoint_out);
	}
	////////////////////////////////////////*REQUEST CAPACITY*///////////////////////////////////////////////////////
    	//request capacity
    	for(i=0; i<64; i++)
	{
		*(buffer+i) = 0;
	}
	memset(cdb, 0, sizeof(cdb));
	length = READ_CAPACITY_LENGTH;	
	printk(KERN_INFO "Reading capacity:\n");
	cdb[0] = 0x25;	// Read Capacity
	
	ret = send_mass_storage_command(udev, endpoint_out, 0, cdb, 0x80, length, &expected_tag);
	usb_bulk_msg(udev, usb_rcvbulkpipe(udev, endpoint_in), (void *)buffer, length, &size, 1000);
	printk(KERN_INFO "received %d bytes\n", size);
	for(i = 0;i < length; i++)
	{	
	printk(KERN_INFO "Buffer[%d] = %d",i, buffer[i]);
	}

	
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
	temp2 = 1024*1024;
	temp1 = ((max_lba+1)/temp2) * (block_size);
	device_size = temp1/1024;
	printk(KERN_INFO "Max LBA: %d, Block Size: %d (%d GB %d MB)\n", max_lba, block_size, device_size, (temp1%1000));
	if (get_mass_storage_status(udev, endpoint_in, expected_tag) == -2) {
		get_sense(udev, endpoint_in, endpoint_out);
	}

    	printk(KERN_INFO "Pendrive with VID:PID (%04X:%04X) plugged\n", id->idVendor, id->idProduct);

	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);
	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);
	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);

	return 0;
}

/*Operations structure*/
static struct usb_driver usbdev_driver = {
	name: "sandisk_dev",  //name of the device
	probe: usbdev_probe, // Whenever Device is plugged in
	disconnect: usbdev_disconnect, // When we remove a device
	id_table: usbdev_table, //  List of devices served by this driver
};


int device_init(void)
{
	printk(KERN_INFO "UAS READ Capacity Driver Inserted\n");
	usb_register(&usbdev_driver);
	return 0;
}

void device_exit(void)
{
	usb_deregister(&usbdev_driver);
	printk(KERN_NOTICE "Leaving Kernel\n");
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("TEJAS");
MODULE_DESCRIPTION("USB_SCSI");

