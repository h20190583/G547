//KERNEL VERSION 4.15.0.-20-generic
//////////////////////////////////*HEADERS*//////////////////////////////////////
#include<linux/kernel.h>
#include<linux/module.h>
#include<linux/usb.h>
#include<linux/slab.h>
#include <stdarg.h>
#include<linux/blkdev.h>
#include<linux/genhd.h>
#include<linux/spinlock.h>
#include<linux/init.h>
#include<linux/workqueue.h>

////////////////////////////*DEFINE CONSTANTS*/////////////////////////////////////
#define SAMSUNG_MEDIA_VID  0x04e8
#define SAMSUNG_MEDIA_PID  0x6860

#define SANDISK_MEDIA_VID  0x0781 
#define SANDISK_MEDIA_PID  0x558a

#define BULK_EP_IN    0x81
#define BULK_EP_OUT   0x02

#define RETRY_MAX                     5
#define REQUEST_SENSE_LENGTH          0x12
#define INQUIRY_LENGTH                0x24
#define READ_CAPACITY_LENGTH          0x08

#define be_to_int32(buf) (((buf)[0]<<24)|((buf)[1]<<16)|((buf)[2]<<8)|(buf)[3])

#define DEVICE_NAME "PENDRIVE"
#define NR_OF_SECTORS 30031872
#define SECTOR_SIZE 512
#define CARD_CAPACITY  (NR_OF_SECTORS*SECTOR_SIZE)
#define MAJOR_NO 166
#define FIRST_MINOR 1
#define MINORS 2

/////////////////////* SCSI Commands */////////////////////////////////////////
#define SCSI_TEST_UNIT_READY            0x00
#define SCSI_REQUEST_SENSE              0x03
#define SCSI_FORMAT_UNIT                0x04
#define SCSI_INQUIRY                    0x12
#define SCSI_MODE_SELECT6               0x15
#define SCSI_MODE_SENSE6                0x1A
#define SCSI_START_STOP_UNIT            0x1B
#define SCSI_MEDIA_REMOVAL              0x1E
#define SCSI_READ_FORMAT_CAPACITIES     0x23
#define SCSI_READ_CAPACITY              0x25
#define SCSI_READ10                     0x28
#define SCSI_WRITE10                    0x2A
#define SCSI_VERIFY10                   0x2F
#define SCSI_MODE_SELECT10              0x55
#define SCSI_MODE_SENSE10               0x5A
#define SCSI_WRITE6                     0x0A
/////////////////////////////////////////////////////////////////////////////////

struct blkdev_private{
	struct request_queue *queue;       //Request queue
	struct gendisk *gd;                //Gendisk
	spinlock_t lock;		   //Spin lock
	struct workqueue_struct *myqueue;  //Local workqueue
};

struct my_work{                    	  //work structure
	struct work_struct work;
	void *data;                      //data to be deferred is passed
};

//static struct request *req;
static struct blkdev_private *device = NULL;

unsigned int pipe;
sector_t start_sector;
sector_t xfer_sectors;
char* buffer = NULL;
unsigned int offset;
size_t xfer_len;
int err =0;
int major;
////////////////////////////////////////////////////////////////////////////////////

static struct usb_device *udev;

//Command Block Wrapper (CBW)
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

static struct block_device_operations blkdev_ops =     //Block device operation
{
	owner: THIS_MODULE,
	//open: blkdev_open,
	//release: blkdev_release
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
/////////////////////////////////////////*GET STATUS*///////////////////////////////////////////////////
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
///////////////////////////////*BOTTOM_HALF*///////////////////////////////////////////////////////////////
static void deferred_work(struct work_struct *work)
{
	printk(KERN_INFO "Entered bottom half\n");
	struct my_work *mwp = container_of(work, struct my_work, work);  //get pointer to work_struct 
	struct request *req = mwp->data;                                 //get deferred data into request

	struct req_iterator iter;
	struct bio_vec bvec;                                      //define bio vector structure
	uint32_t expected_tag;
	uint8_t cdb[16];
	buffer = (char *)kmalloc(sizeof(char), GFP_KERNEL);        //allocate memory for buffer
	int size, dir;
	int r = 0;
	memset(buffer, 0, sizeof(char));
	rq_for_each_segment(bvec, req, iter)
	{
		start_sector = iter.iter.bi_sector;                   //start sector
		buffer = kmap_atomic(bvec.bv_page);                   //page address
		offset = bvec.bv_offset;                              //page offser
		xfer_len = bvec.bv_len;                               //number of bytes to be transfered
		xfer_sectors = xfer_len/ SECTOR_SIZE;                 //number of sectors to be transfered
		dir = rq_data_dir(req);
		//printk(KERN_INFO "len = %d, sectors = %d\n", xfer_len, xfer_sectors);
		printk(KERN_INFO "start_sector = %d, dir = %d\n", start_sector, dir);
		//printk(KERN_INFO "buffer = %d, offset = %ld\n", buffer, offset);
		memset(cdb, 0, sizeof(cdb));
			
		cdb[2] = (start_sector >> 24) & 0xFF ;                 //fill cdb with starting sector 
        	cdb[3] = (start_sector >> 16) & 0xFF ;
        	cdb[4] = (start_sector >> 8) & 0xFF ;
       		cdb[5] = (start_sector >> 0) & 0xFF ;
        	cdb[7] = (xfer_sectors >> 8) & 0xFF ;                 //fill cdb with number of sectors to be trasnfered
        	cdb[8] = (xfer_sectors >> 0) & 0xFF ;

		//if dir == 1 , then write SCSI
		if(dir == 1)             
		{
			cdb[0] = SCSI_WRITE10;             
			//expected_tag = 0x0005;
			r = send_mass_storage_command(udev, BULK_EP_OUT, 0, cdb, 0x00, xfer_len, &expected_tag);                           //send cdb
			r = usb_bulk_msg(udev, usb_sndbulkpipe(udev, BULK_EP_OUT), ((void *) (buffer+offset)) , xfer_len, &size, 0);       //write to buffer+offset
			if(r != 0)
			{
				printk(KERN_INFO "writing into drive failed\n");
				usb_clear_halt(udev, usb_sndbulkpipe(udev, BULK_EP_OUT));
			}
			if (get_mass_storage_status(udev, BULK_EP_IN, expected_tag) == -2) {
				get_sense(udev, BULK_EP_IN, BULK_EP_OUT);
			}
			printk(KERN_INFO "write to disk complete\n");
		}
		else
		{
			cdb[0] = SCSI_READ10;
			//expected_tag = 0x0006;
			r = send_mass_storage_command(udev, BULK_EP_OUT, 0, cdb, 0x80, xfer_len, &expected_tag);                          //send cdb
			r = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, BULK_EP_IN), ((void *) (buffer+offset)), xfer_len, &size, 0);        //read from buffer + offset
			if(r != 0)
			{
				printk(KERN_INFO "reading from drive failed\n");
				usb_clear_halt(udev, usb_rcvbulkpipe(udev, BULK_EP_IN));
			}
			if (get_mass_storage_status(udev, BULK_EP_IN, expected_tag) == -2) {
				get_sense(udev, BULK_EP_IN, BULK_EP_OUT);
			}
			printk(KERN_INFO "reading from disk complete\n");
		}
	}
	__blk_end_request_cur(req, 0);            //end current request
	kunmap_atomic(buffer);                    //unmap buffer
	kfree(mwp);
	return;
}
///////////////////////////////*REQUEST FUNCTION*////////////////////////////////////////////////////////////
void mmc_request(struct request_queue *q)
{
	struct request *rq;
	struct my_work *usb_work = NULL;
	while((rq = blk_fetch_request(q)) != NULL)                       //fetch request from request queue
	{
		if(rq == NULL || blk_rq_is_passthrough(rq) == 1)            //check if the request is a valid FS 
		{
			printk(KERN_INFO "non FS request");
			__blk_end_request_all(rq, -EIO);
			continue;
		}
		else
		{

			printk(KERN_ALERT "Inside request function\n");
			printk(KERN_ALERT "Target Sector No. %d ", rq->__sector);
			usb_work = (struct my_work*)kmalloc(sizeof(struct my_work), GFP_ATOMIC);

			if(usb_work == NULL)
			{
				printk(KERN_INFO "memory allocation for deferred work failed\n");
				__blk_end_request_all(rq, -EIO);
				continue;
			}

			usb_work->data = rq;                                  //defer request into data 
			INIT_WORK(&usb_work->work, deferred_work);            //initialise work
			queue_work(device->myqueue, &usb_work->work);         //queue work into myqueue
			//schedule_work(&usb_work->work);		
		}
	}	
}
//////////////////////////////////*PROBE FUNCTION*//////////////////////////////////////////////////////
static int usbdev_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	//struct gendisk *mmc_disk = NULL;
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
	//struct my_work *usb_work = NULL;
	udev = interface_to_usbdev(interface); //in built function to convert interface to usbdev
	//udev = container_of(interface->dev.parent,struct usb_device,dev);  //or use container of

	for(i=0; i<64; i++)
	{
		*(buffer+i) = 0;
	}
	if(id->idProduct == SAMSUNG_MEDIA_PID)
	{
		printk(KERN_INFO "Media Plugged in\n");
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
				printk(KERN_INFO "EP %d is Bulk IN\n", i);          //print bulk endpoint in address
				endpoint_in = epAddr;
				}
			else
				{
				printk(KERN_INFO "EP %d is Bulk OUT\n", i);        //print bulk endpoint out address
				endpoint_out= epAddr;
				}
	
		}

	}
	printk(KERN_INFO "endpoint in: %X, out: %X\n",endpoint_in, endpoint_out);
	////////////////////////////////////////////*GET LUN*/////////////////////////////////////////////////////////
	//get max LUN
    	retval = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0xFE, 0xA1, 0x0, 0x0, (void *)lun, sizeof(uint8_t), 1000);   //send control msg to get logical unit number

	if (retval < 0)
	{
		printk(KERN_INFO "control message error (%d)\n", retval);
	}
	else
	{
		printk(KERN_INFO "MAX LUN = %d\n", *lun);        //print LUN
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
	printk(KERN_INFO "VID:PID:REV \"%8s\":\"%8s\":\"%4s\"\n", vid, pid, rev);      //print VID PID VERSION of pendrive
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
	//calculate approximate capacity of the pendrive
	max_lba = be_to_int32(&buffer[0]);
	block_size = be_to_int32(&buffer[4]);
	temp2 = 1024*1024;
	temp1 = ((max_lba+1)/temp2) * (block_size);
	device_size = temp1/1024;
	printk(KERN_INFO "Max LBA: %d, Block Size: %d (%d GB)\n", max_lba, block_size, device_size);         //print  capacity
	if (get_mass_storage_status(udev, endpoint_in, expected_tag) == -2) {
		get_sense(udev, endpoint_in, endpoint_out);
	}
    	printk(KERN_INFO "Pendrive with VID:PID (%04X:%04X) plugged\n", id->idVendor, id->idProduct);

	printk(KERN_INFO "USB DEVICE CLASS : %x", interface->cur_altsetting->desc.bInterfaceClass);         //print device class
  	printk(KERN_INFO "USB DEVICE SUB CLASS : %x", interface->cur_altsetting->desc.bInterfaceSubClass);  //print device subclass
  	printk(KERN_INFO "USB DEVICE Protocol : %x", interface->cur_altsetting->desc.bInterfaceProtocol);   //print device protocol
    	////////////////////////////////////////////////////////////////////////////////////////////////////////

  	device = kmalloc(sizeof(struct blkdev_private),GFP_KERNEL);                 //initialize block device

	if( (major=register_blkdev(0,DEVICE_NAME))<0)
    	{
    		printk(KERN_INFO"Unable to register block device : PENDRIVE\n");
    		return -EBUSY;
    	}
    
    	printk("Major_number:%d\n",major);
    	spin_lock_init(&device->lock);                                            //get spin lock
    	device->gd = alloc_disk(MINORS);                                          //allocate gendisk with the number of MINORS
    	if(!device->gd)
    	{
    		printk(KERN_INFO"Gendisk is not allocated\n");
    		unregister_blkdev(major,DEVICE_NAME);
    		kfree(device);
    		return -ENOMEM;
    	}	
    	strcpy(device->gd->disk_name,DEVICE_NAME);                               //copy device name
    	device->gd->first_minor = FIRST_MINOR;                                   //set first minor number
    	device->gd->major = major;                                               //set major number
    	device->gd->fops = &blkdev_ops;                                          //set block device operation


    	if(!(device->queue = blk_init_queue(mmc_request ,&device->lock)))        //set request function
    	{
    		printk("Request_queue allocated failed\n");
    		del_gendisk(device->gd);
    		unregister_blkdev(major,DEVICE_NAME);
    		kfree(device);
    		return -ENOMEM;
    	}
    	//blk_queue_logical_block_size(device->gd->queue, SECTOR_SIZE);
    	device->gd->queue = device->queue;
    	blk_queue_logical_block_size(device->gd->queue, SECTOR_SIZE);             //set logical block size
	//device->gd->queue->queuedata = path;
    	set_capacity(device->gd, NR_OF_SECTORS);                                  //set capacity using number of sectors
    	device->gd->private_data = device;                                       
	device->myqueue = create_workqueue("myqueue");                            //create local workqueue
    	add_disk(device->gd);                                                     //add disk 
    	printk(KERN_INFO"Block device successfully registered\n");
    
  	return 0;
}  

////////////////////////////////*PENDRIVE DISCONNECT*///////////////////////////////////////////////////////////
static void usbdev_disconnect(struct usb_interface *interface)
{
	struct gendisk *mmc_disk = device->gd;
	del_gendisk(mmc_disk);                       //delete gendisk
	blk_cleanup_queue(device->queue);            
	flush_workqueue(device->myqueue);           //empty workqueue
	destroy_workqueue(device->myqueue);         //destory myqueue
	kfree(device);                              //free memory allocated for device
	printk(KERN_INFO "USBDEV Device Removed\n");
}

static struct usb_device_id usbdev_table [] = {
	{USB_DEVICE(SAMSUNG_MEDIA_VID, SAMSUNG_MEDIA_PID)},
	{USB_DEVICE(SANDISK_MEDIA_VID, SANDISK_MEDIA_PID)},
	{} /*terminating entry*/	
};
MODULE_DEVICE_TABLE (usb, usbdev_table);

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
	usb_register(&usbdev_driver);        //register USB driver
	return 0;
}

void device_exit(void)
{
	usb_deregister(&usbdev_driver);      //de-register USB driver
	printk(KERN_NOTICE "Leaving Kernel\n");
}

module_init(device_init);
module_exit(device_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("TEJAS");
MODULE_DESCRIPTION("USB_DRIVER");
