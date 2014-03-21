//##########################################################################
//#                                                                        #
//#        Copyright (C) 2011 by Beijing IRTOUCHSYSTEMS Co., Ltd           #
//#                                                                        #
//#        IRTOUCH Optical USB Touchscreen                                 #    
//#                                                                        #
//#        Create by Yin Xingjie                                             #
//#                                                                        #
//#        Email: yinxj@unitop.cc                                             #
//#                                                                        #
//##########################################################################
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <asm/uaccess.h> 


/*
 * Version Information
 */
#define DRIVER_VERSION "v14.2.0.0005 for android"
#define DRIVER_AUTHOR "Beijing IRTOUCH SYSTEMS Co.,Ltd"
#define DRIVER_DESC "IRTOUCH USB touchscreen driver for multi point"
#define DRIVER_LICENSE "GPL"
#define TOUCH_DRIVER_NAME "irtouch"

#define SET_COMMAND 0x01
#define GET_COMMAND 0x02

#define SET_COMMAND_MODE 0x71
#define COMMAND_GET_CAMERAPARA 0x10
#define COMMAND_GET_CALIBPARA 0x14
#define COMMAND_SET_CALIBPARA 0x15
#define SET_WORK_MODE	0x93
#define	GET_CALIB_X	0x2D
#define	SET_CALIB_X	0x2E
#define	GET_CALIB_Y	0x2B
#define	SET_CALIB_Y	0x2C
#define	GET_DEVICE_CONFIG	0x30
#define	SET_DEVICE_CONFIG	0x31
#define	GET_DEVICE_SN	0x34
#define	GET_DEVICE_BARCODE	0x17
#define	GET_DEVICE_BUS	0x1C

#define COMMAND_RETRY_COUNT  5
#define PRIMARYID 	0x00
#define SECONDARYID 	0x01

#define MAJOR_DEVICE_NUMBER 47
#define MINOR_DEVICE_NUMBER 193
#define TOUCH_POINT_COUNT  10
#define USB_TRANSFER_LENGTH			2048
#define TOUCH_DEVICE_NAME "irtouch"

#define CTLCODE_GET_COORDINATE 0xc0
#define CTLCODE_SET_CALIB_PARA_X 0xc1
#define CTLCODE_SET_CALIB_PARA_Y 0xc2
#define CTLCODE_GET_CALIB_PARA_X 0xc3
#define CTLCODE_GET_CALIB_PARA_Y 0xc4
#define CTLCODE_START_CALIB 0xc5
#define CTLCODE_GET_CALIB_STATUS 0xc6
#define CTLCODE_DEVICE_RESET 0xc7
#define CTLCODE_GET_SCR_PARA 0xc8
#define CTLCODE_GET_PRODUCT_SN 0xc9
#define CTLCODE_GET_PRODUCT_ID 0xca
#define CTLCODE_SET_SCAN_MODE 0xcb
#define CTLCODE_GET_PRODUCT_BAR 0xcc

#define CTLCODE_SCREEN_PROP 0xF0
#define CTLCODE_PRIMARY_CAMERA 0xF1
#define CTLCODE_SECONDLY_CAMERA 0xF2

#pragma pack(1)
struct irtouch_point
{
	unsigned char	PointId;
	unsigned char	status;
	short	x;
	short	y;
};

struct irtouch_pkt
{	
	union
	{
		struct
		{
			unsigned char startBit;
			unsigned char commandType;
			unsigned char command;
			unsigned char pkgLength;
			unsigned char deviceId;
			union
			{
				struct
				{
					struct
					{
						unsigned char  ID;
						short X;
						short Y;
						short Z;
						short Width;
						short Height;
						unsigned char  Status;
						unsigned char  Reserved;
					}points[0x03];
					struct
					{
						unsigned char PointCount:5;
						unsigned char PointIndex:3;
					}PacketStatus;
				}IRPacket;
				unsigned char CRC;
			}DataPacket;
		}Packets;
		unsigned char RawInput[USB_TRANSFER_LENGTH];
	};
};

struct points
{
	int	x;
	int	y;
};

struct calib_param{
	long	A00;
	long	A01;
	long	A10;
	unsigned char reserve[46];
};

struct	device_config{
	unsigned char deviceID;
	unsigned char monitorID;
	unsigned char reserved[8];
	unsigned char calibrateStatus;
	unsigned char reserved1[47];
};

struct scr_props
{
	char DefaultOrigin	: 4;
	char PhysicalOrigin	: 4;
    unsigned char s1;		//红外未使用
    int width;				//unit:mm/s
    int height;				//unit:mm/s
 	short lightNo_h;		//横轴灯数
	short lightNo_v;		//纵轴灯数
	unsigned char maxcount;	//支持最大点数
	unsigned char ScanMethod;	//扫描方式 如1对3、1对5等
	unsigned char ScanInterv;	//发射接收最大偏灯数
	union
	{
		unsigned char nCustomized;	//特定版本号
		struct
		{
			unsigned char flags	: 2;	//特定版
			unsigned char Size	: 3;	//屏尺寸类型
			unsigned char Style	: 3;	//直灯/偏灯
		}TypeStatus;
	};

    unsigned short dHLInvalidInterv;	//X方向左边框附近无效区域（mm）(0.00~127.99)
	unsigned short dHRInvalidInterv;	//X方向右边框附近无效区域（mm）(0.00~127.99)
	unsigned short dVTInvalidInterv;	//V方向上边框附近无效区域（mm）(0.00~127.99)
	unsigned short dVBInvalidInterv;	//V方向下边框附近无效区域（mm）(0.00~127.99)
    unsigned short dHLightInterv;	//H灯间距 (0.00~127.99)
	unsigned short dVLightInterv;	//V灯间距 (0.00~127.99)
	unsigned short dMargin;	//边框特殊处理区域大小	(0.00~127.99)
	unsigned short dOffset;	//外扩强度	(0.00~127.99)
	
	int reserve1;
    int reserve2;
    int reserve3;
    int reserve4;

    float reserve5;
    float reserve6;
};

struct product_descriptor	//RKA100W021S
{
	unsigned char 	ProductType;
	unsigned char 	ProductSeries;
	unsigned char 	ProductReplace[4];
	unsigned char 	ScreenType;
	unsigned char 	size[3];
	unsigned char 	standardType;
	unsigned char	Reserved[9];
};

struct AlgContext
{
	int	actualCounter;
	struct irtouch_point inPoint[TOUCH_POINT_COUNT];
};

struct scan_range
{
    unsigned char Status;
    unsigned short   Start;
    unsigned short   End;
};

struct irscan_mode
{
    unsigned char Enable_Switch;   //0:stop 1:direct 2:Oblique 3:direct and Oblique
    unsigned char Scan_Mode;   //0:all 1:trace
    unsigned char Being_Num;   //0:1on1 1:1on3 2:1on5
    unsigned char Offset; 
    unsigned char Track_Width;
    unsigned char DataType;
    struct scan_range HRange[2];
    struct scan_range VRange[2];
    unsigned char ScanOffset;  //0:nothing 1:ScanInterv
    unsigned char reserve;
};

typedef struct _OptCamProps
{
	unsigned char Pos;	//'L' or 'R'
	unsigned char ID;	//'0'='Primary' or '1'='Secondary'
	int 	lowEdge;
	int 	highEdge;
	float 	a0;		//default = 0;
	float 	a1;		//default = 1.15;
	float 	a2;		//default = 0;
	float 	a3;		//default = 0;
	float 	a4;		//default = 0;
	float 	a5;		//default = 0;
	float 	a6;		//default = 0;
	float 	a7;		//default = 0;
	float 	b0;		//default = 0;
	float 	b1;		//default = 1;
	float 	reserve0;
	float 	reserve1;
}OptCamProps, *POptCamProps;

typedef struct _OptScrProps
{
	unsigned char pos;
	unsigned char s1;
	int 	width;		//unit:mm/s
	int 	height;		//unit:mm/s
	int 	fps;
	int 	maxSpeed;	//unit:mm/s
	int 	camOffsetY;	//unit:mm
	int 	camLOffsetX;	//unit:mm
	int 	camROffsetX;	//unit:mm	
	int 	velocityRefCount;    
	float 	veloctiyWeighing;    
	float 	reserve0;
	float 	reserve1;
	float 	reserve2;
	float 	reserve3;
	float 	reserve4;
}OptScrProps, *POptScrProps;

#pragma pack()

struct irtouchusb{
	char name[128];
	char phys[64];
	int pipe;
	struct usb_device *udev;
	struct input_dev *dev;
	char inbuf[USB_TRANSFER_LENGTH];
};

struct device_context{
	bool startCalib;
	bool isCalibX;
	bool isCalibY;
	u16  productid;
	struct points points;
	struct AlgContext algCtx;
	struct calib_param calibX;
	struct calib_param calibY;
	struct device_config devConfig;
	struct scr_props     ScreenProps;
	struct product_descriptor	ProductSN;
	unsigned char	BarCodeInfo[40];
	OptScrProps optScreenProps;
	OptCamProps PrimaryCamProps;
	OptCamProps SecondaryCamProps;
};

struct cdev irtouch_cdev;
struct device_context *irtouchdev_context = NULL;
static struct class *irser_class;
static struct usb_driver irtouch_driver;
static struct file_operations irtouch_fops;
static unsigned char SetScanModeBuffer[2][64];

static struct usb_class_driver irtouch_class_driver = {
	.name = TOUCH_DRIVER_NAME,
	.fops = &irtouch_fops,
	.minor_base = MINOR_DEVICE_NUMBER,
};

static void irtouch_translatePoint(struct irtouch_point pt, int *x, int *y)
{
	*x = ((irtouchdev_context->calibX.A01 * pt.y) / 10000) + ((irtouchdev_context->calibX.A10 * pt.x) / 10000) + irtouchdev_context->calibX.A00;
	*y = ((irtouchdev_context->calibY.A01 * pt.y) / 10000) + ((irtouchdev_context->calibY.A10 * pt.x) / 10000) + irtouchdev_context->calibY.A00;
}

static int irtouch_build_packet(
		unsigned char 	command_type, 
		unsigned char 	command, 
		int 		device_id, 
		unsigned char * data, 
		int 		length, 
		unsigned char * out_data
		)
{
	unsigned char 	tmp = 0x55;
	unsigned char 	package[64] = {0};
	int		out_length = 0;
	int i;

	package[0] = 0xaa;
	package[1] = command_type;
	package[2] = command;
	package[3] = (unsigned char)length + 1;
	package[4] = (unsigned char)device_id;

	memcpy(&package[5],data,length);

	for(i = 0; i < 5 + length; i++)
	{
		tmp = package[i] + tmp;
	}

	package[5 + length] = (unsigned char)tmp;

	out_length = 6 + length;

	memcpy(out_data, package, out_length);

	return out_length;
}

unsigned char irtouch_send_command(struct usb_device * udev,
			unsigned char 	command_type,
			unsigned char * in_data,
			unsigned char * out_data,
			int 		length
		)
{
	unsigned char	buf[64];
	int		count = 0;
	int		ret = 0;
	unsigned char	result = 0x00;
	
	memset(buf,0,sizeof(buf));

	do{
		ret = usb_control_msg(udev, 
					usb_sndctrlpipe(udev, 0), 
					0, 
					USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					0, 
					0, 
					(char *)in_data,
					length, 
					1000);
		msleep(100);

		ret = usb_control_msg(udev, 
					usb_rcvctrlpipe(udev, 0), 
					0, 
					0xc0, 
					0, 
					0, 
					(char *)buf, 
					64, 
					1000);
		msleep(100);

		count++;
		result = (buf[1] >> 4) & 0x0f;
	}while((0x01 != result) && (count < COMMAND_RETRY_COUNT));

	if(count >= COMMAND_RETRY_COUNT)
	{
		return false;
	}

	memcpy(out_data,buf,sizeof(buf));

	return true;
	
}

static void irtouch_set_scanmode(struct usb_device * udev, struct irscan_mode *scanmode)
{
	int	ret = 0;
	unsigned char inBuffer[64] = { 0 };
	unsigned char inBufferLen = 0;

	inBufferLen = irtouch_build_packet(SET_COMMAND, 0x39, 0, (unsigned char *)scanmode, sizeof(struct irscan_mode), inBuffer);
	if(memcmp(SetScanModeBuffer[scanmode->Scan_Mode], inBuffer, inBufferLen) == 0)
	{
		return;
	}

	memcpy(SetScanModeBuffer[scanmode->Scan_Mode], inBuffer, inBufferLen);
	ret = usb_control_msg(udev, 
					usb_sndctrlpipe(udev, 0), 
					0, 
					USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					0, 
					0, 
					(char *)inBuffer,
					inBufferLen, 
					1000);
}

static void irtouch_get_device_param(struct usb_device * udev)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	//set touchscreen to command mode
	inData[0] = 0x01;

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Set command mode failed.",__func__);
	}
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0xF4;

	length = irtouch_build_packet(SET_COMMAND,SET_WORK_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Set device to work status failed.",__func__);
	}

	//get device configuration
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get device configuration failed.",__func__);
	}
	memcpy(&irtouchdev_context->ScreenProps,&getData[5],sizeof(struct scr_props));
	printk("maxcount=%d!\n", irtouchdev_context->ScreenProps.maxcount);
	if(0 == irtouchdev_context->ScreenProps.maxcount)
		irtouchdev_context->ScreenProps.maxcount = TOUCH_POINT_COUNT;
	
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,1,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get device configuration failed.",__func__);
	}
	memcpy(&irtouchdev_context->devConfig,&getData[5],sizeof(struct device_config));

	//get calib x parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_CALIB_X,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get calib X param failed.",__func__);
	}
	memcpy(&irtouchdev_context->calibX,&getData[5],sizeof(struct calib_param));

	//get calib y parameter
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_CALIB_Y,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get calib Y param failed.",__func__);
	}
	memcpy(&irtouchdev_context->calibY,&getData[5],sizeof(struct calib_param));

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_SN,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get device ProductSN failed.",__func__);
	}
	memcpy(&irtouchdev_context->ProductSN,&getData[5],sizeof(struct product_descriptor));

	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_BARCODE,0,inData,0,outData);
	status = irtouch_send_command(udev,GET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Get device BARCODE failed.",__func__);
	}
	memcpy(irtouchdev_context->BarCodeInfo,&getData[5],40);

	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Set to work mode failed.",__func__);
	}
}

static void irtouch_get_opt_param(struct usb_device * udev)
{
	unsigned char	in_data[58];
	unsigned char	out_data[64];
	unsigned char	get_data[64];
	unsigned char	status = false;
	int		length = 0;

	//Set primary camera to graphics mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	in_data[0] = 0x01;

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,PRIMARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		err("IRTOUCH:    %s Set primary camera to graphics mode fail.",__func__);
	}	
	
	//Set primary camera to graphics mode
	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,SECONDARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		err("IRTOUCH:    %s Set secondary camera to graphics mode fail.",__func__);
	}

	msleep(100);	
	//Get primary camera parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,COMMAND_GET_CAMERAPARA,PRIMARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{
		memcpy(&irtouchdev_context->PrimaryCamProps,&get_data[5],58);
	}
	else
	{
		err("IRTOUCH:    %s Get primary camera parameter fail.",__func__);
	}
	
	//Get secondary camera parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,COMMAND_GET_CAMERAPARA,SECONDARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{	
		memcpy(&irtouchdev_context->SecondaryCamProps,&get_data[5],58);	
	}
	else
	{
		err("IRTOUCH:    %s Get secondary camera parameter fail.",__func__);
	}

	//Get Product parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,PRIMARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&irtouchdev_context->optScreenProps,&get_data[5],58);	
	}
	else
	{
		err("IRTOUCH:    %s Get Product parameter fail.",__func__);
	}

	//Get Device Config
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_CONFIG,SECONDARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	if(status)
	{
		memcpy(&irtouchdev_context->devConfig,&get_data[5],58);
	}
	else
	{
		err("IRTOUCH:    %s Get Device Config fail.",__func__);
	}

	//Get X Calibration Parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,COMMAND_GET_CALIBPARA,PRIMARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&irtouchdev_context->calibX,&get_data[5],58);	
	}
	else
	{
		err("IRTOUCH:    %s Get X Calibration Parameter fail.",__func__);
	}

	//Get Y Calibration Parameter
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));
	memset(get_data,0,sizeof(get_data));

	length = irtouch_build_packet(GET_COMMAND,COMMAND_GET_CALIBPARA,SECONDARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);	
	if(status)
	{
		memcpy(&irtouchdev_context->calibY,&get_data[5],58);
	}
	else
	{
		err("IRTOUCH:    %s Get Y Calibration Parameter fail.",__func__);
	}

	//memset(in_data,0,sizeof(in_data));
	//memset(out_data,0,sizeof(out_data));
	//memset(get_data,0,sizeof(get_data));

	//length = irtouch_build_packet(GET_COMMAND,GET_DEVICE_SN,0,in_data,0,out_data);
	//status = irtouch_send_command(udev,GET_COMMAND,out_data,get_data,length);
	//if(!status)
	//{
	//	err("IRTOUCH:    %s  Get device ProductSN failed.",__func__);
	//}
	//memcpy(&irtouchdev_context->ProductSN,&get_data[5],sizeof(struct product_descriptor));
	
	//Set the primary camera of failure to coordinate mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,PRIMARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		err("IRTOUCH:    %s Set the primary camera of failure to coordinate mode fail.",__func__);
	}

	//Set the secondary camera of failure to coordinate mode
	memset(in_data,0,sizeof(in_data));
	memset(out_data,0,sizeof(out_data));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,SECONDARYID,in_data,1,out_data);
	status = irtouch_send_command(udev,SET_COMMAND,out_data,get_data,length);
	if(!status)
	{
		err("IRTOUCH:    %s Set the secondary camera of failure to coordinate mode fail.",__func__);
	}
	irtouchdev_context->ScreenProps.maxcount = 2;
}

static void irtouch_set_calib(struct usb_device * udev, bool resetFlg)
{
	unsigned char	inData[58];
	unsigned char	outData[64];
	unsigned char	getData[64];
	int status = -1;
	int length = 0;

	//set touchscreen to command mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));
	memset(getData,0,sizeof(getData));

	inData[0] = 0x01;

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Set to command mode failed.",__func__);
	}

	if(irtouchdev_context->productid == 0x0c20)
	{
		length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,1,inData,1,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			err("IRTOUCH:    %s  Set to command mode failed.",__func__);
		}
	}
	
	//Set device configuration
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	if(resetFlg)
	{
		irtouchdev_context->devConfig.calibrateStatus = 0;
		memcpy(&inData,&irtouchdev_context->devConfig,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			err("IRTOUCH:    %s  Set device configuration failed.",__func__);
		}
	}
	else
	{
		irtouchdev_context->devConfig.calibrateStatus = 1;
		memcpy(&inData,&irtouchdev_context->devConfig,58);
	
		length = irtouch_build_packet(SET_COMMAND,SET_DEVICE_CONFIG,1,inData,58,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			err("IRTOUCH:    %s  Set device configuration failed.",__func__);
		}

		//Set calib X param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&irtouchdev_context->calibX,58);

		if(irtouchdev_context->productid == 0x0c20)
		{
			length = irtouch_build_packet(SET_COMMAND,COMMAND_SET_CALIBPARA,PRIMARYID,inData,58,outData);
			status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				err("IRTOUCH:    %s Set X camera para fail.",__func__);
			}
		}
		else
		{
			length = irtouch_build_packet(SET_COMMAND,SET_CALIB_X,0,inData,58,outData);
			status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				err("IRTOUCH:    %s  Set calib X param failed.",__func__);
			}
		}
		//Set calib Y param
		memset(inData,0,sizeof(inData));
		memset(outData,0,sizeof(outData));

		memcpy(&inData,&irtouchdev_context->calibY,58);

		if(irtouchdev_context->productid == 0x0c20)
		{
			length = irtouch_build_packet(SET_COMMAND,COMMAND_SET_CALIBPARA,SECONDARYID,inData,58,outData);
			status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				err("IRTOUCH:    %s Set X camera para fail.",__func__);
			}
		}
		else
		{
			length = irtouch_build_packet(SET_COMMAND,SET_CALIB_Y,0,inData,58,outData);
			status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
			if(!status)
			{
				err("IRTOUCH:    %s  Set calib Y param failed.",__func__);
			}
		}
	}

	//set touchscreen to work mode
	memset(inData,0,sizeof(inData));
	memset(outData,0,sizeof(outData));

	length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,0,inData,1,outData);	
	status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
	if(!status)
	{
		err("IRTOUCH:    %s  Set to work mode failed.",__func__);
	}

	if(irtouchdev_context->productid == 0x0c20)
	{
		length = irtouch_build_packet(SET_COMMAND,SET_COMMAND_MODE,1,inData,1,outData);
		status = irtouch_send_command(udev,SET_COMMAND,outData,getData,length);
		if(!status)
		{
			err("IRTOUCH:    %s  Set to command mode failed.",__func__);
		}
	}
}

static int irtouch_open(struct inode * inode, struct file * filp)
{
	struct irtouchusb * irtouch;
	struct usb_interface * intf;

	intf = usb_find_interface(&irtouch_driver, MINOR_DEVICE_NUMBER);
	if(!intf)
	{
		err("IRTOUCH:    %s usb_find_interface ERROR!",__func__);
		return -1;
	}

	irtouch = usb_get_intfdata(intf);
	if(!irtouch)
	{
		err("IRTOUCH:    %s touch is NULL!",__func__);
		return -1;
	}

	filp->private_data = irtouch;
	//printk("IRTOUCH:    %s success!",__func__);
	return 0;
}

static int irtouch_release(struct inode * inode, struct file * filp)
{
	return 0;
}

static int irtouch_read(struct file * filp, char * buffer, size_t count, loff_t * ppos)
{
	int ret = -1;
	int bytes_read;
	struct irtouchusb * irtouch;
	
	irtouch = filp->private_data;
	ret = usb_interrupt_msg(irtouch->udev,
				irtouch->pipe,
				irtouch->inbuf,
				USB_TRANSFER_LENGTH,
				&bytes_read, 10000);
	
	if(!ret)
	{
		if(copy_to_user(buffer, &irtouch->inbuf, bytes_read))
		{
			err("IRTOUCH:  %s copy_to_user failed!",__func__);
			ret = -EFAULT;
		}
		else
		{
			ret = bytes_read;
		}
	}
	else if(ret == -ETIMEDOUT)
	{
		return ret;
	}
	else 
	{
		err("IRTOUCH:  %s usb_interrupt_msg failed %d!",__func__, ret);
	}
	
	return ret;	
}

static void irtouch_process_data(struct irtouchusb *irtouch)
{	
	int i;
	unsigned int x = 0, y = 0;
	int count = 0;
	struct input_dev *dev = irtouch->dev;
	
	for(i=0;i<irtouchdev_context->ScreenProps.maxcount;i++)
	{
		if(irtouchdev_context->algCtx.inPoint[i].status == 0x01)
		{
			x = irtouchdev_context->algCtx.inPoint[i].x;
			y = irtouchdev_context->algCtx.inPoint[i].y;

			if(irtouchdev_context->startCalib)
			{
				irtouchdev_context->points.x = irtouchdev_context->algCtx.inPoint[0].x;
				irtouchdev_context->points.y = irtouchdev_context->algCtx.inPoint[0].y;
			}
				
			if(irtouchdev_context->devConfig.calibrateStatus && !irtouchdev_context->startCalib)
			{
				irtouch_translatePoint(irtouchdev_context->algCtx.inPoint[i],&x,&y);
			}
			
			printk("status%d=%d, x=%d, y=%d!\n", i, irtouchdev_context->algCtx.inPoint[i].status, x, y);
			//input_report_abs(dev,ABS_MT_TRACKING_ID,data[i].PointId);
			input_report_abs(dev,ABS_MT_POSITION_X, x);
			input_report_abs(dev,ABS_MT_POSITION_Y, y);
			input_report_abs(dev,ABS_MT_PRESSURE,255);
			input_report_key(dev,BTN_TOUCH,1);
			input_mt_sync(dev);
			count++;
		}
	}

	/* SYN_MT_REPORT only if no contact */
	if (!count)
		input_mt_sync(dev);
	
	input_sync(dev);
}

static int irtouch_write(struct file * filp, const char * user_buffer, size_t count, loff_t * ppos)
{
	int ret = -1;
	struct irtouchusb *irtouch;
	
	irtouch = filp->private_data;
	if(copy_from_user(irtouchdev_context->algCtx.inPoint, user_buffer, sizeof(struct irtouch_point)*irtouchdev_context->ScreenProps.maxcount))
	{
		err("IRTOUCH: copy_from_user failed!\n");
		ret = -EFAULT;
	}
	else
	{
		ret = count;
		irtouch_process_data(irtouch);
	}
	//printk("IRTOUCH: irtouch_write,ret=%d!\n", ret);
	return ret;	
}

static long irtouch_ioctl(struct file * filp, unsigned int ctl_code, unsigned long ctl_param)
{
	unsigned char value = 0;
	int ret = -1;
	struct irtouchusb *irtouch;

	irtouch = filp->private_data;

	switch(ctl_code)
	{
		case CTLCODE_START_CALIB:				
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				if(value == 0x01)
				{
					irtouchdev_context->startCalib = true;
				}
				else
				{
					irtouchdev_context->startCalib = false;
				}
			}	
			
			break;

		case CTLCODE_GET_COORDINATE:
			ret = copy_to_user((struct points *)ctl_param, &irtouchdev_context->points, sizeof(struct points));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_COORDINATE>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_STATUS:
			{
				int status = irtouchdev_context->devConfig.calibrateStatus;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					err("IRTOUCH:    %s <CTLCODE_GET_CALIB_STATUS>copy_to_user failed!",__func__);
				}
				break;
			}
	
		case CTLCODE_SET_CALIB_PARA_X:
			ret = copy_from_user(&irtouchdev_context->calibX, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_SET_CALIB_PARA_X>copy_to_user failed!",__func__);
			}
			else
			{
				irtouchdev_context->isCalibX = true;
			}
			break;

		case CTLCODE_SET_CALIB_PARA_Y:
			ret = copy_from_user(&irtouchdev_context->calibY, (struct calib_param *)ctl_param, sizeof(struct calib_param));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_SET_CALIB_PARA_Y>copy_to_user failed!",__func__);
			}
			else
			{
				irtouchdev_context->isCalibY = true;
			}
			break;

		case CTLCODE_GET_CALIB_PARA_X:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->calibX, sizeof(struct calib_param));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_CALIB_PARA_Y:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->calibY, sizeof(struct calib_param));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_CALIB_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_SCR_PARA:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->ScreenProps, sizeof(struct scr_props));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_SCR_PARA>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_PRODUCT_SN:
			ret = copy_to_user((struct calib_param *)ctl_param, &irtouchdev_context->ProductSN, sizeof(struct product_descriptor));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_PRODUCT_SN>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_GET_PRODUCT_BAR:
			ret = copy_to_user((unsigned char *)ctl_param, irtouchdev_context->BarCodeInfo, 40);
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_GET_PRODUCT_BAR>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_SET_SCAN_MODE:
			{
				struct irscan_mode scanmode = {0};
				ret = copy_from_user(&scanmode, (unsigned char *)ctl_param, sizeof(struct irscan_mode));
				if(ret != 0)
				{
					err("IRTOUCH:    %s <CTLCODE_GET_CALIB_STATUS>copy_to_user failed!",__func__);
				}
				else
				{
					irtouch_set_scanmode(irtouch->udev, &scanmode);
				}
				break;
			}
			
		case CTLCODE_GET_PRODUCT_ID:
			{
				int status = irtouchdev_context->productid;
				ret = copy_to_user((int *)ctl_param, &status, sizeof(int));
				if(ret != 0)
				{
					err("IRTOUCH:    %s <CTLCODE_GET_PRODUCT_ID>copy_to_user failed!",__func__);
				}
				break;
			}
		
		case CTLCODE_DEVICE_RESET:
			ret = copy_from_user(&value, (unsigned char *)ctl_param, sizeof(unsigned char));
			if(ret == 0)
			{
				if(value == 0x01)
				{
					irtouch_set_calib(irtouch->udev,true);
				}
			}
			break;

		case CTLCODE_SCREEN_PROP:
			ret = copy_to_user((OptScrProps *)ctl_param, &irtouchdev_context->optScreenProps, sizeof(OptScrProps));	
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_SCREEN_PROP>copy_to_user failed!",__func__);
			}
			break;

		case CTLCODE_PRIMARY_CAMERA:

			ret = copy_to_user((OptCamProps *)ctl_param, &irtouchdev_context->PrimaryCamProps, sizeof(OptCamProps));
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_PRIMARY_CAMERA>copy_to_user failed!",__func__);
			}
			break;
			
		case CTLCODE_SECONDLY_CAMERA:
			ret = copy_to_user((OptCamProps *)ctl_param, &irtouchdev_context->SecondaryCamProps, sizeof(OptCamProps));	
			if(ret != 0)
			{
				err("IRTOUCH:    %s <CTLCODE_SECONDLY_CAMERA>copy_to_user failed!",__func__);
			}
			break;
			
		default:
			break;
	}

	if(irtouchdev_context->isCalibX && irtouchdev_context->isCalibY)
	{
		irtouchdev_context->isCalibX = false;
		irtouchdev_context->isCalibY = false;
		irtouch_set_calib(irtouch->udev,false);
	}

	return 0;
}


static struct file_operations irtouch_fops = {
	.owner = THIS_MODULE,
	.open = irtouch_open,
	.read = irtouch_read,
	.write = irtouch_write,
	.unlocked_ioctl = irtouch_ioctl,
	.release = irtouch_release,
};

static int irtouch_open_device(struct input_dev * dev)
{
	return 0;
}

static void irtouch_close_device(struct input_dev * dev)
{
	dbg("IRTOUCH:  irtouch_close_device+++\n");
}

static bool irtouch_mkdev(void)
{
	int retval;

	//create device node
	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	retval = register_chrdev_region(devno,1,TOUCH_DEVICE_NAME);
	if(retval < 0)
	{
		err("IRTOUCH:    %s register chrdev error.",__func__);
		return false;
	}

	cdev_init(&irtouch_cdev,&irtouch_fops);
	irtouch_cdev.owner = THIS_MODULE;
	irtouch_cdev.ops = &irtouch_fops;
	retval = cdev_add(&irtouch_cdev,devno,1);

	if(retval)
	{
		err("IRTOUCH:    %s  Adding char_reg_setup_cdev error=%d",__func__,retval);
		return false;
	}

	irser_class = class_create(THIS_MODULE, TOUCH_DEVICE_NAME);
	if(IS_ERR(irser_class))
	{
		err("IRTOUCH:    %s class create failed.",__func__);
		return false;
	}
	
	device_create(irser_class,NULL,MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER),NULL,TOUCH_DEVICE_NAME);
	
	printk("IRTOUCH:    %s success!\n",__func__);
	
	return true;
}

static int irtouch_probe(struct usb_interface * intf, const struct usb_device_id *id)
{
	int i;
	int ret = -ENOMEM;
	struct usb_host_interface * interface = intf->cur_altsetting;
	struct usb_endpoint_descriptor * endpoint = NULL;
	struct usb_device * udev = interface_to_usbdev(intf);
	struct input_dev * dev = NULL;
	struct irtouchusb * irtouch;

	if (!(irtouchdev_context = kmalloc(sizeof(struct device_context), GFP_KERNEL)))  
		return -ENOMEM;
	memset(irtouchdev_context,0,sizeof(struct device_context));
	
	irtouch = kzalloc(sizeof(struct irtouchusb), GFP_KERNEL);
	if(!irtouch)
	{
		err("IRTOUCH:    %s Out of memory.",__func__);
		goto EXIT;
	}
	memset(irtouch, 0, sizeof(struct irtouchusb));

	for(i = 0; i < interface->desc.bNumEndpoints; i++)
	{
		endpoint = &interface->endpoint[i].desc;
		if(endpoint->bEndpointAddress & USB_DIR_IN)
		{
			if(endpoint->bmAttributes == 3)
				irtouch->pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
			else
				irtouch->pipe = usb_rcvbulkpipe(udev, endpoint->bEndpointAddress);
		}
	}

	dev = input_allocate_device();
	if(!dev)
		goto EXIT;

	if(udev->manufacturer)
		strlcpy(irtouch->name, udev->manufacturer, sizeof(irtouch->name));

	if(udev->product) 
	{
		if (udev->manufacturer)
			strlcat(irtouch->name, " ", sizeof(irtouch->name));
		strlcat(irtouch->name, udev->product, sizeof(irtouch->name));
	}

	if (!strlen(irtouch->name))
		snprintf(irtouch->name, sizeof(irtouch->name),
				"IRTOUCH Multi-touch Touchscreen %04x:%04x",
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct));

	usb_make_path(udev, irtouch->phys, sizeof(irtouch->phys));
	strlcat(irtouch->phys, "/input0", sizeof(irtouch->phys));

	dev->name = irtouch->name;
	dev->phys = irtouch->phys;

	usb_to_input_id(udev, &dev->id);
	dev->dev.parent = &intf->dev;

	input_set_drvdata(dev, irtouch);

	dev->open = irtouch_open_device;
	dev->close = irtouch_close_device;

	dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	set_bit(BTN_TOUCH, dev->keybit);
	dev->absbit[0] = BIT(ABS_MT_POSITION_X) | BIT(ABS_MT_POSITION_Y);
	set_bit(ABS_MT_PRESSURE, dev->absbit);

	input_set_abs_params(dev, ABS_MT_PRESSURE,0,255,0,0);
	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, 32767, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, 32767, 0, 0);
	
	irtouch->udev = udev;
	irtouch->dev = dev;
	
	ret = input_register_device(irtouch->dev);
	if(ret)
	{
		err("IRTOUCH:    %s input_register_device failed,ret:%d",__func__,ret);
		goto EXIT;
	}

	usb_set_intfdata(intf, irtouch);
	ret = usb_register_dev(intf, &irtouch_class_driver);
	if(ret)
	{
		err("IRTOUCH:     %s Not able to get a minor for this device.",__func__);
		usb_set_intfdata(intf,NULL);
		goto EXIT;
	}

	msleep(5000);

	irtouchdev_context->productid = udev->descriptor.idProduct;
	if(irtouchdev_context->productid == 0x0c20)
		irtouch_get_opt_param(udev);
	else
		irtouch_get_device_param(udev);
	irtouch_mkdev();
	printk("IRTOUCH id=0x%x: %s success!\n", irtouchdev_context->productid, __func__);
	return 0;

EXIT:
	if(irtouchdev_context)
		kfree(irtouchdev_context);
	
	if(dev)
		input_free_device(dev);

	if(irtouch)
		kfree(irtouch);

	return -ENOMEM;
}

static void irtouch_disconnect(struct usb_interface * intf)
{
	struct irtouchusb * irtouch = usb_get_intfdata(intf);

	dev_t devno = MKDEV(MAJOR_DEVICE_NUMBER,MINOR_DEVICE_NUMBER);

	cdev_del(&irtouch_cdev);
	unregister_chrdev_region(devno,1);

	device_destroy(irser_class,devno);
	class_destroy(irser_class);
	
	usb_deregister_dev(intf, &irtouch_class_driver);
	usb_set_intfdata(intf, NULL);

	if(irtouchdev_context)
	{
		kfree(irtouchdev_context);
	}
	
	if(irtouch)
	{
		input_unregister_device(irtouch->dev);
		kfree(irtouch);
		irtouch = NULL;
	}
}

static struct usb_device_id irtouch_table[] = {
	{USB_DEVICE(0x6615, 0x0084)},
	{USB_DEVICE(0x6615, 0x0085)},
	{USB_DEVICE(0x6615, 0x0086)},
	{USB_DEVICE(0x6615, 0x0087)},
	{USB_DEVICE(0x6615, 0x0c20)},
	{ }    /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, irtouch_table);

static struct usb_driver irtouch_driver = {
	.name = TOUCH_DRIVER_NAME,
	.probe = irtouch_probe,
	.disconnect = irtouch_disconnect,
	.id_table = irtouch_table,
};

static int __init irtouch_init(void)
{
	int retval = usb_register(&irtouch_driver);
	if (retval == 0)
		printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":" DRIVER_DESC);
	return retval;
}

static void __exit irtouch_exit(void)
{
	usb_deregister(&irtouch_driver);
}


module_init(irtouch_init);
module_exit(irtouch_exit);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);


