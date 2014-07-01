/* drivers/input/touchscreen/sis_i2c.c - I2C Touch panel driver for SiS 9200 family
 *
 * Copyright (C) 2011 SiS, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Date: 2012/10/11
 * Version:	Android_v2.03.01-A639-1011
 */

#include <linux/module.h>
#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include "sis_i2c.h"
#include <linux/linkage.h>
#include <linux/slab.h>
//#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/irq.h>
//#include "ctp_platform_ops.h"
#include <mach/sys_config.h>
#include <linux/ctp.h>

#ifdef _STD_RW_IO
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#define CTP_NAME "sis925x_ts"
#define CDEV_DEVICE_NAME "sis_aegis_touch_device"
static int sis_char_devs_count = 1;        /* device count */
static int sis_char_major = 0;
static struct cdev sis_char_cdev;
static struct class *sis_char_class = NULL;
#endif

static u32 debug_mask = 0;
#define dprintk(level_mask,fmt,arg...)    if(unlikely(debug_mask & level_mask)) \
        printk("***CTP***"fmt, ## arg)

#define VERSION "hxj@20121225.1552"

/* Addresses to scan */
static const unsigned short normal_i2c[] = { SIS_SLAVE_ADDR, I2C_CLIENT_END };
static const int chip_id_value[] = {0x55};
static struct workqueue_struct *sis_wq;
struct sis_ts_data *ts_bak = 0;
struct sisTP_driver_data *TPInfo = NULL;
static void sis_tpinfo_clear(struct sisTP_driver_data *TPInfo, int max);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void sis_ts_early_suspend(struct early_suspend *h);
static void sis_ts_late_resume(struct early_suspend *h);
#endif

#ifdef CONFIG_X86
//static const struct i2c_client_address_data addr_data;
/* Insmod parameters */
static int sis_ts_detect(struct i2c_client *client, struct i2c_board_info *info);
#endif

static union{
	unsigned short dirty_addr_buf[2];
	const unsigned short normal_i2c[2];
}u_i2c_addr = {{0x00},};

static void* __iomem gpio_addr = NULL;
static int gpio_int_hdle = 0;
static int gpio_wakeup_hdle = 0;
static int gpio_reset_hdle = 0;
static int gpio_wakeup_enable = 1;
static int gpio_reset_enable = 1;
//extern int m_inet_ctpState;

extern struct ctp_config_info config_info;
#define CTP_IRQ_NUMBER    (config_info.irq_gpio_number)
#define CTP_IRQ_MODE			(TRIG_EDGE_NEGATIVE)
/*
static int	int_cfg_addr[]={PIO_INT_CFG0_OFFSET,PIO_INT_CFG1_OFFSET,
			PIO_INT_CFG2_OFFSET, PIO_INT_CFG3_OFFSET};
*/
static int screen_max_x = 0;
static int screen_max_y = 0;
static int revert_x_flag = 0;
static int revert_y_flag = 0;
static int exchange_x_y_flag = 0;
static int support_ten_flag = 0;
static int ctp_vendor = 0;
static __u32 twi_id = 0;
static u32 int_handle = 0;

#define CTP_IRQ_NO			(IRQ_EINT21)
#define SW_INT_IRQNO_PIO                28
/*
static void ctp_clear_penirq(void)
{
	int reg_val;
	//clear the IRQ_EINT29 interrupt pending
	//pr_info("clear pend irq pending\n");
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
	//writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
	//writel(reg_val&(1<<(IRQ_EINT21)),gpio_addr + PIO_INT_STAT_OFFSET);
	if((reg_val = (reg_val&(1<<(CTP_IRQ_NO))))){
		printk("==CTP_IRQ_NO=\n");              
		writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
	}
	return;
}*/

void PrintBuffer(int start, int length, char* buf)
{
	int i;
	for ( i = start; i < length; i++ )
	{
		printk("%02x ", buf[i]);
		if (i != 0 && i % 30 == 0)
			printk("\n");
	}
	printk("\n");	
}

int sis_command_for_write(struct i2c_client *client, int wlength, unsigned char *wdata)
{
    int ret = -1;
    struct i2c_msg msg[1];

    msg[0].addr = client->addr;
    msg[0].flags = 0; //Write
    msg[0].len = wlength;
    msg[0].buf = (unsigned char *)wdata;

    ret = i2c_transfer(client->adapter, msg, 1);
	//	msleep(1);

    return ret;
}

int sis_command_for_read(struct i2c_client *client, int rlength, unsigned char *rdata)
{
    int ret = -1;
    struct i2c_msg msg[1];

    msg[0].addr = client->addr;
    msg[0].flags = I2C_M_RD; //Read
    msg[0].len = rlength;
    msg[0].buf = rdata;

    ret = i2c_transfer(client->adapter, msg, 1);

    return ret;
}

int sis_cul_unit(uint8_t report_id)
{
	int basic = 6;
	int area = 2;
	int pressure = 1;
	int ret = basic;
	
	if (BIT_AREA(report_id) && BIT_TOUCH(report_id))
	{
		ret += area;
	}
	if (BIT_PRESSURE(report_id))
	{
		ret += pressure;
	}
	
	return ret;	
}

int sis_ReadPacket(struct i2c_client *client, uint8_t cmd, uint8_t* buf)
{
	uint8_t tmpbuf[MAX_BYTE] = {0};

    int ret = -1;
    int touchnum = 0;
    int L_COUNT_OTHER = 0;
    int bytecount = 0;

	ret = sis_command_for_read(client, MAX_BYTE, tmpbuf);
	//printk("1111111111 \n");		
	if(ret < 0 )
	{
		printk(KERN_ERR "sis_ReadPacket: i2c transfer error----1\n");
		return ret;
	}
	memcpy(&buf[0], &tmpbuf[2], 62);	//skip bytecount

	if (buf[L_REPORT_ID] == 0x10)		//One packet
	{	
		//L_COUNT_OTHER = sis_cul_unit(buf[L_REPORT_ID]) * 1 + 1;  // ***** Modify 
		//touchnum = buf[L_COUNT_OTHER] & 0xff;		
		
		bytecount = tmpbuf[0];
		bytecount = bytecount - 2 - 1;  // - byte_bytecout -byte_count
		
		touchnum = bytecount / sis_cul_unit(buf[L_REPORT_ID]);
		L_COUNT_OTHER = sis_cul_unit(buf[L_REPORT_ID]) * touchnum + 1;
		
		touchnum = buf[L_COUNT_OTHER] & 0xff;
		//touchnum = buf[L_COUNT_TOUCH] & 0xff;

	}
	else 								//TOUCH_SERIAL
	{
		bytecount = tmpbuf[0];
		bytecount = bytecount - 2 - 1 - 1 - 2;  // - byte_bytecout -ReportID -byte_count -CRC
		
		touchnum = bytecount / sis_cul_unit(buf[L_REPORT_ID]);
		L_COUNT_OTHER = sis_cul_unit(buf[L_REPORT_ID]) * touchnum + 1; // + ReportID
		
		touchnum = buf[L_COUNT_OTHER] & 0xff;		
		
		//L_COUNT_OTHER = sis_cul_unit(buf[L_REPORT_ID]) * 5 + 1;
		//touchnum = buf[L_COUNT_OTHER] & 0xff;	

		if(touchnum > 5)
		{
			ret = sis_command_for_read(client, MAX_BYTE, tmpbuf);
			if(ret < 0)
			{
				printk(KERN_ERR "sis_ReadPacket: i2c transfer error\n");
				return ret;
			}
			
			if ((tmpbuf[L_COUNT_OTHER + 2] & 0xff) != 0)
			{
				printk(KERN_ERR "sis_ReadPacket: get error package\n");
				return -1;
			}
			memcpy(&buf[64], &tmpbuf[2], 62);	//skip bytecount			
		}		
	}
	//printk("====================== %s End!\n",__func__);
	return touchnum;
}	

int check_gpio_interrupt(void)
{
    int ret = 0;
    //TODO
    //CHECK GPIO INTERRUPT STATUS BY YOUR PLATFORM SETTING.
    //ret = gpio_get_value(GPIO_IRQ);
    return ret;
}

void ts_report_key(struct i2c_client *client, uint8_t keybit_state)
{
	int i = 0;
	uint8_t diff_keybit_state= 0x0; //check keybit_state is difference with pre_keybit_state
	uint8_t key_value = 0x0; //button location for binary
	uint8_t  key_pressed = 0x0; //button is up or down
	struct sis_ts_data *ts = i2c_get_clientdata(client);

	if (!ts)
	{
		printk(KERN_ERR "%s error: Missing Platform Data!\n", __func__);
		return;
	}

	diff_keybit_state = TPInfo->pre_keybit_state ^ keybit_state;

	if (diff_keybit_state)
	{
		for (i = 0; i < BUTTON_KEY_COUNT; i++)
		{
		    if ((diff_keybit_state >> i) & 0x01)
			{
				key_value = diff_keybit_state & (0x01 << i);
				key_pressed = (keybit_state >> i) & 0x01;
				switch (key_value)
				{
					case MSK_COMP:
						input_report_key(ts->input_dev, KEY_COMPOSE, key_pressed);
						printk(KERN_ERR "%s : MSK_COMP %d \n", __func__ , key_pressed);
						break;
					case MSK_BACK:
						input_report_key(ts->input_dev, KEY_BACK, key_pressed);
						printk(KERN_ERR "%s : MSK_BACK %d \n", __func__ , key_pressed);
						break;
					case MSK_MENU:
						input_report_key(ts->input_dev, KEY_MENU, key_pressed);
						printk(KERN_ERR "%s : MSK_MENU %d \n", __func__ , key_pressed);
						break;
					case MSK_HOME:
						input_report_key(ts->input_dev, KEY_HOME, key_pressed);
						printk(KERN_ERR "%s : MSK_HOME %d \n", __func__ , key_pressed);
						break;
					case MSK_NOBTN:
						//Release the button if it touched.
					default:
						break;
				}
			}
		}
		TPInfo->pre_keybit_state = keybit_state;
	}
}

static void sis_ts_work_func(struct work_struct *work)
{
	struct sis_ts_data *ts = container_of(work, struct sis_ts_data, work);
    	int ret = -1;
    	int point_unit;
  
	uint8_t buf[PACKET_BUFFER_SIZE] = {0};
	uint8_t i = 0, fingers = 0;
	uint8_t px = 0, py = 0, pstatus = 0;
#ifdef _ANDROID_4
	bool all_touch_up = true;
#endif

	  //printk("=======sis_ts_work_func=====\n");
    /* I2C or SMBUS block data read */
    ret = sis_ReadPacket(ts->client, SIS_CMD_NORMAL, buf);

	if (ret < 0) //Error fingers' number or Unknow bytecount
	{
	  printk("================ chaoban test: ret = -1\n");
		goto err_free_allocate;
	}
	else if (ret == 0)
	{
		//printk("================ sis_ReadPacket = ret\n",ret);
		for( i = 0; i < 10; i++) //when no touch, clean information temp buffer
		{
			ts->area_tmp[i][0]=0;
			ts->pressure_tmp[i][0]=0;
		}
		i=0;
	}
	sis_tpinfo_clear(TPInfo, MAX_FINGERS);

	/* Parser and Get the sis9200 data */
	point_unit = sis_cul_unit(buf[L_REPORT_ID]);
	if (buf[L_REPORT_ID] == 0x10)
	{		
		fingers = ret;		
	}
	else 
	{
		fingers = ret;
	}	

	TPInfo->fingers = fingers = (fingers > MAX_FINGERS ? 0 : fingers);
	
	for (i = 0; i < fingers; i++)
	{
        if ((buf[L_REPORT_ID] != 0x10) && (i >= 5))
        {
			pstatus = 1 + ((i - 5) * point_unit);    // Calc point status
			pstatus += 64;		
		}
		else 
		{
			pstatus = 1 + (i * point_unit);          // Calc point status
		}

	    px = pstatus + 2;                   // Calc point x_coord
	    py = px + 2;                        // Calc point y_coord

		if ((buf[pstatus]) == TOUCHUP)
		{
			TPInfo->pt[i].bWidth = 0;
			TPInfo->pt[i].bPressure = 0;
		}
		else if ((buf[pstatus]) == TOUCHDOWN)
		{	
			if (buf[L_REPORT_ID] == 0x10)
			{
					TPInfo->pt[i].bWidth = 1;
					TPInfo->pt[i].bPressure = 1;
			}
			else
			{
				if (BIT_PRESSURE(buf[L_REPORT_ID]))
				{
					if (BIT_AREA(buf[L_REPORT_ID]))
					{
						TPInfo->pt[i].bWidth = ((buf[pstatus + 6] & 0xff) | ((buf[pstatus + 7] & 0xff)<< 8));
						TPInfo->pt[i].bPressure = (buf[pstatus + 8]);
					}
					else
					{
						TPInfo->pt[i].bWidth = 1;
						TPInfo->pt[i].bPressure = (buf[pstatus + 8]);
					}
				}
				else
				{
					if (BIT_AREA(buf[L_REPORT_ID]))
					{				
						TPInfo->pt[i].bWidth = ((buf[pstatus + 6] & 0xff) | ((buf[pstatus + 7] & 0xff)<< 8));
						TPInfo->pt[i].bPressure = 1;
					}
					else
					{
						TPInfo->pt[i].bWidth = 1;
						TPInfo->pt[i].bPressure = 1;
					}
				}
			}			
		}
		else
		{
			goto err_free_allocate;
		}
		
		TPInfo->pt[i].id = (buf[pstatus + 1]);
		TPInfo->pt[i].x = ((buf[px] & 0xff) | ((buf[px + 1] & 0xff)<< 8));
    TPInfo->pt[i].y = ((buf[py] & 0xff) | ((buf[py + 1] & 0xff)<< 8));      
	}


//label_send_report:
    /* Report co-ordinates to the multi-touch stack */
#ifdef _ANDROID_4	
		for(i = 0; ((i < TPInfo->fingers) && (i < MAX_FINGERS)); i++)
		{
			if(TPInfo->pt[i].bPressure)
			{
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, TPInfo->pt[i].bWidth);
				input_report_abs(ts->input_dev, ABS_MT_PRESSURE, TPInfo->pt[i].bPressure);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, TPInfo->pt[i].x);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, TPInfo->pt[i].y);
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, TPInfo->pt[i].id);     //Android 2.3
				input_mt_sync(ts->input_dev);
				all_touch_up = false;
			}
			//printk("x = %d,  y = %d \n", TPInfo->pt[i].x, TPInfo->pt[i].y);
			//printk("==========android4.0=========\n");
			if (i == (TPInfo->fingers -1) && all_touch_up == true)
			{    
				//printk("======all_touch_up =======\n");  
        input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
        input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
				input_mt_sync(ts->input_dev);
			}
		}

		if(TPInfo->fingers == 0)
		{     
			//printk("====finger num is 0=====\n");
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0); 	
			input_mt_sync(ts->input_dev);
			
		}
#else
	i = 0;
		do
		{
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, TPInfo->pt[i].bPressure);
			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, TPInfo->pt[i].bWidth);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, TPInfo->pt[i].x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, TPInfo->pt[i].y);
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, TPInfo->pt[i].id);		//Android 2.3
			printk("=======android 2.3 ======\n");
			//printk(KERN_INFO "poinert ID = %d\n", TPInfo->pt[i].id);
			input_mt_sync(ts->input_dev);
			i++;
		}
		while ((i < TPInfo->fingers) && (i < MAX_FINGERS));
#endif
	input_sync(ts->input_dev);

err_free_allocate:
	//enable_irq(SW_INT_IRQNO_PIO);
	sw_gpio_eint_set_enable(CTP_IRQ_NUMBER,1);
    return;
}

static void sis_tpinfo_clear(struct sisTP_driver_data *TPInfo, int max)
{
	int i = 0;
	for(i = 0; i < max; i++)
	{
		TPInfo->pt[i].id = -1;
		TPInfo->pt[i].touch = -1;
		TPInfo->pt[i].x = 0;
		TPInfo->pt[i].y = 0;
		TPInfo->pt[i].bPressure = 0;
		TPInfo->pt[i].bWidth = 0;
	}
	TPInfo->CRC = 0x0;
	TPInfo->id = 0x0;
	TPInfo->fingers = 0;
}

static enum hrtimer_restart sis_ts_timer_func(struct hrtimer *timer)
{
	struct sis_ts_data *ts = container_of(timer, struct sis_ts_data, timer);
	queue_work(sis_wq, &ts->work);
	if (!ts->use_irq)
	{	// For Polling mode
	    hrtimer_start(&ts->timer, ktime_set(0, TIMER_NS), HRTIMER_MODE_REL);
	}
	return HRTIMER_NORESTART;
}

#if 0
static irqreturn_t sis_ts_irq_handler(int irq, void *dev_id)
{
	struct sis_ts_data *ts = dev_id;
	printk("-----------------------------sis_ts_irq_handler---------------------\n");
#if ( LINUX_VERSION_CODE < KERNEL_VERSION (2, 6, 39) )
	if ((ts->desc->status & IRQ_DISABLED) == IRQ_STATUS_ENABLED)
#else
	if ((ts->desc->irq_data.state_use_accessors & IRQD_IRQ_DISABLED) == IRQ_STATUS_ENABLED)
#endif
	{
		disable_irq_nosync(ts->client->irq);
	}
	queue_work(sis_wq, &ts->work);
	printk("-----------------------------end---------------------\n");	
	return IRQ_HANDLED;
}
#else
static irqreturn_t sis_ts_irq_handler(struct sis_ts_data *ts)
{	
	printk("==========sis Interrupt============\n");				 
  //printk("Enter work\n");
	queue_work(sis_wq, &ts->work);	
	return 0;
}
#endif

static int initial_irq(void)
{
	int ret = 0;
#ifdef _I2C_INT_ENABLE
	/* initialize gpio and interrupt pins */
	/* TODO */
	//ret = gpio_request(GPIO_IRQ, "GPIO_133");	// ex. GPIO_133 for interrupt mode
	if (ret < 0)
	{
		// Set Active Low. Please reference the file include/linux/interrupt.h
		printk(KERN_ERR "sis_ts_: Failed to gpio_request\n");
		printk(KERN_ERR "sis_ts_: Fail : gpio_request was called before this driver call\n");
	}	
	/* setting gpio direction here OR boardinfo file*/
	/* TODO */
#else
	ret = -1;
#endif
	return ret;
}

uint16_t cal_crc (char* cmd, int start, int end)
{
	int i = 0;
	uint16_t crc = 0;
	for (i = start; i <= end ; i++)
	{
		crc = (crc<<8) ^ crc16tab[((crc>>8) ^ cmd[i] )&0x00FF];
	}
	return crc;
}

uint16_t cal_crc_with_cmd (char* data, int start, int end, uint8_t cmd)
{
	int i = 0;
	uint16_t crc = 0;
	
	crc = (crc<<8) ^ crc16tab[((crc>>8) ^ cmd)&0x00FF];
	for (i = start; i <= end ; i++)
	{
		crc = (crc<<8) ^ crc16tab[((crc>>8) ^ data[i] )&0x00FF];
	}
	return crc;
}

void write_crc (unsigned char *buf, int start, int end)
{
	uint16_t crc = 0;
	crc = cal_crc (buf, start , end);
	buf[end+1] = (crc >> 8)& 0xff;
	buf[end+2] = crc & 0xff;
}

//#ifdef CONFIG_FW_SUPPORT_POWERMODE
/*
 * When you will send commad to chip, you should use this function on 
 * the first time.
 * 
 * Return:If switch success return ture, else return false.
 */
bool sis_switch_to_cmd_mode(struct i2c_client *client)
{
	int ret = -1;
	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_active[10] 	= {0x04, 0x00, 0x08, 0x00, 0x09, 
		0x00, 0x85, 0x0d, 0x51, 0x09};
	uint8_t sis817_cmd_enable_diagnosis[10] 	= {0x04, 0x00, 0x08, 
		0x00, 0x09, 0x00, 0x85, 0x5c, 0x21, 0x01};
	
	
	
	//Send 85 CMD - PWR_CMD_ACTIVE
	ret = sis_command_for_write(client, sizeof(sis817_cmd_active), sis817_cmd_active);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Switch CMD Faile - 85(PWR_CMD_ACTIVE)\n");
		return false;
	}
	
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if(ret < 0){
		printk(KERN_ERR "SiS READ Switch CMD Faile - 85(PWR_CMD_ACTIVE)\n");
		return false;
	}
	
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return NACK - 85(PWR_CMD_ACTIVE)\n");
		return false;
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return Unknow- 85(PWR_CMD_ACTIVE)\n");
		return false;
	}
	
	msleep(100);
	memset(tmpbuf, 0, sizeof(tmpbuf));
	
	//Send 85 CMD - ENABLE_DIAGNOSIS_MODE
	ret = sis_command_for_write(client, sizeof(sis817_cmd_enable_diagnosis), sis817_cmd_enable_diagnosis);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Switch CMD Faile - 85(ENABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if(ret < 0){
		printk(KERN_ERR "SiS READ Switch CMD Faile - 85(ENABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return NACK - 85(ENABLE_DIAGNOSIS_MODE)\n");
		return false;
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return Unknow- 85(ENABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	msleep(50);
	return true;	
}

/*
 * When chip in the command mode will switch to work, you should use 
 * this function.
 * 
 * Return:If switch success return ture, else return false.
 */
bool sis_switch_to_work_mode(struct i2c_client *client)
{
	int ret = -1;
	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_fwctrl[10] 	= {0x04, 0x00, 0x08, 0x00, 0x09, 
		0x00, 0x85, 0x3c, 0x50, 0x09};
	uint8_t sis817_cmd_disable_diagnosis[10] 	= {0x04, 0x00, 0x08, 
		0x00, 0x09, 0x00, 0x85, 0x6d, 0x20, 0x01};
	
	
	//Send 85 CMD - PWR_CMD_FW_CTRL
	ret = sis_command_for_write(client, sizeof(sis817_cmd_fwctrl), sis817_cmd_fwctrl);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Switch CMD Faile - 85(PWR_CMD_FW_CTRL)\n");
		return false;
	}
	
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if(ret < 0){
		printk(KERN_ERR "SiS READ Switch CMD Faile - 85(PWR_CMD_FW_CTRL)\n");
		return false;
	}
	
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return NACK - 85(PWR_CMD_FW_CTRL)\n");
		return false;
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return Unknow- 85(PWR_CMD_FW_CTRL)\n");
		return false;
	}

	memset(tmpbuf, 0, sizeof(tmpbuf));
	
	//Send 85 CMD - DISABLE_DIAGNOSIS_MODE
	ret = sis_command_for_write(client, sizeof(sis817_cmd_disable_diagnosis), sis817_cmd_disable_diagnosis);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Switch CMD Faile - 85(DISABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if(ret < 0){
		printk(KERN_ERR "SiS READ Switch CMD Faile - 85(DISABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return NACK - 85(DISABLE_DIAGNOSIS_MODE)\n");
		return false;
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Switch CMD Return Unknow- 85(DISABLE_DIAGNOSIS_MODE)\n");
		return false;
	}
	
	msleep(50);
	return true;
}

/*
 * Use this function check chip status.
 * 
 * Return:Ture is chip on the work function, else is chip not ready.
 */
bool sis_check_fw_ready(struct i2c_client *client)
{
  	int ret = 0;
  	int check_num = 10;
  	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_check_ready[14] = {0x04, 0x00, 0x0c, 0x00, 0x09, 
		0x00, 0x86, 0x00, 0x00, 0x00, 0x00, 0x50, 0x34, 0x00};
	
	
	sis817_cmd_check_ready[BUF_CRC_PLACE] = (0xFF & cal_crc_with_cmd(sis817_cmd_check_ready, 8, 13, 0x86));
	

	if(!sis_switch_to_cmd_mode(client)){
		printk(KERN_ERR "SiS Switch to CMD mode error.\n");
		return false;
	}
	
	while(check_num--){
		printk(KERN_ERR "SiS Check FW Ready.\n");
		ret = sis_command_for_write(client, sizeof(sis817_cmd_check_ready), sis817_cmd_check_ready);
		if(ret < 0){
			printk(KERN_ERR "SiS SEND Check FW Ready CMD Faile - 86\n");
		}
		ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
		if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
			printk(KERN_ERR "SiS SEND Check FW Ready CMD Return NACK\n");
		}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
			printk(KERN_ERR "SiS SEND Check FW Ready CMD Return Unknow\n");
		}else{
			if(tmpbuf[9] == 1){
				printk(KERN_ERR "SiS FW READY.\n");
				break;
			}
		}
		printk(KERN_ERR "SiS CHECK FW READY - Retry:%d.\n", (10-check_num));	
		msleep(50);
	}
	
	if(!sis_switch_to_work_mode(client)){
		printk(KERN_ERR "SiS Switch to Work mode error.\n");
		return false;
	}
	
	if(check_num == 0) return false;
	return true;
		
}

/*
 * Use this function to change chip power mode. 
 * 
 * mode:POWER_MODE_FWCTRL, power control by FW.
 * 		POWER_MODE_ACTIVE, chip always work on time.
 * 		POWER_MODE_SLEEP,  chip on the sleep mode.
 * 
 * Return:Ture is change power mode success.
 */
bool sis_change_fw_mode(struct i2c_client *client, enum SIS_817_POWER_MODE mode)
{
	int ret = -1;
	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_fwctrl[10] 	= {0x04, 0x00, 0x08, 0x00, 0x09, 0x00, 0x85, 0x3c, 0x50, 0x09};
	uint8_t sis817_cmd_active[10] 	= {0x04, 0x00, 0x08, 0x00, 0x09, 0x00, 0x85, 0x0d, 0x51, 0x09};
	uint8_t sis817_cmd_sleep[10] 	= {0x04, 0x00, 0x08, 0x00, 0x09, 0x00, 0x85, 0x5e, 0x52, 0x09};
	
	
	switch(mode)
	{
	case POWER_MODE_FWCTRL:
		ret = sis_command_for_write(client, sizeof(sis817_cmd_fwctrl), sis817_cmd_fwctrl);
		break;
	case POWER_MODE_ACTIVE:
		ret = sis_command_for_write(client, sizeof(sis817_cmd_active), sis817_cmd_active);
		break;
	case POWER_MODE_SLEEP:
		ret = sis_command_for_write(client, sizeof(sis817_cmd_sleep), sis817_cmd_sleep);
		break;
	default:
		return false;
		break;
	}

	if(ret < 0){
		printk(KERN_ERR "SiS SEND Power CMD Faile - 85\n");
		return false;
	}
	
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if(ret < 0){
		printk(KERN_ERR "SiS READ Power CMD Faile - 85\n");
		return false;
	}
	
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Power CMD Return NACK - 85\n");
		return false;
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Power CMD Return Unknow- 85\n");
		return false;
	}
	
	msleep(100);
	
	return true;
}

/*
 * Use this function to get chip work status. 
 * 
 * Return:-1 is get firmware work status error.
 * 		  POWER_MODE_FWCTRL, power control by FW.
 * 		  POWER_MODE_ACTIVE, chip always work on time.
 * 		  POWER_MODE_SLEEP,  chip on the sleep mode.
 */
enum SIS_817_POWER_MODE sis_get_fw_mode(struct i2c_client *client)
{
	int ret;
	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_check_power_mode[14] = {0x04, 0x00, 0x0c, 0x00, 
		0x09, 0x00, 0x86, 0x00, 0x00, 0x00, 0x00, 0x50, 0x34, 0x00};

	printk(KERN_INFO "SiS Get FW Mode.\n");	
	sis817_cmd_check_power_mode[BUF_CRC_PLACE] = (0xFF & cal_crc_with_cmd(sis817_cmd_check_power_mode, 8, 13, 0x86));
	
	ret = sis_command_for_write(client, sizeof(sis817_cmd_check_power_mode), sis817_cmd_check_power_mode);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Get FW Mode CMD Faile - 86\n");
	}else{
		ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
		if(ret < 0){
			printk(KERN_ERR "SiS READ Get FW Mode CMD Faile - 86\n");
		}else{
			if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
				printk(KERN_ERR "SiS SEND Get FW Mode CMD Return NACK\n");
			}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
				printk(KERN_ERR "SiS SEND Get FW Mode CMD Return Unknow\n");
				PrintBuffer(0, sizeof(tmpbuf), tmpbuf);
			}
		}
	}
	
	switch(tmpbuf[10])
	{
	case POWER_MODE_FWCTRL:
		return POWER_MODE_FWCTRL;
	case POWER_MODE_ACTIVE:
		return POWER_MODE_ACTIVE;
	case POWER_MODE_SLEEP:
		return POWER_MODE_SLEEP;
	default:
		break;
	}

	return -1;
}

/*
 * Use this function to reset chip. 
 */
void sis_fw_softreset(struct i2c_client *client)
{

	int ret = 0;
	uint8_t tmpbuf[MAX_BYTE] = {0};
	uint8_t sis817_cmd_reset[8] = {0x04, 0x00, 0x06, 0x00, 0x09, 0x00, 0x82, 0x00};
	

	sis817_cmd_reset[BUF_CRC_PLACE] = (0xFF & cal_crc(sis817_cmd_reset, 6, 6));

	printk(KERN_ERR "SiS Software Reset.\n");
	if(!sis_switch_to_cmd_mode(client)){
		printk(KERN_ERR "SiS Switch to CMD mode error.\n");
		return;
	}
	
	ret = sis_command_for_write(client, sizeof(sis817_cmd_reset), sis817_cmd_reset);
	if(ret < 0){
		printk(KERN_ERR "SiS SEND Reset CMD Faile - 82\n");
	}
	ret = sis_command_for_read(client, sizeof(tmpbuf), tmpbuf);
	if((tmpbuf[BUF_ACK_PLACE_L] == BUF_NACK_L) && (tmpbuf[BUF_ACK_PLACE_H] == BUF_NACK_H)){
		printk(KERN_ERR "SiS SEND Reset CMD Return NACK - 85(DISABLE_DIAGNOSIS_MODE)\n");
	}else if((tmpbuf[BUF_ACK_PLACE_L] != BUF_ACK_L) || (tmpbuf[BUF_ACK_PLACE_H] != BUF_ACK_H)){
		printk(KERN_ERR "SiS SEND Reset CMD Return Unknow- 85(DISABLE_DIAGNOSIS_MODE)\n");
	}	
	msleep(2000);
}
//#endif //CONFIG_FW_SUPPORT_POWERMODE

#ifdef _STD_RW_IO
#define BUFFER_SIZE MAX_BYTE
ssize_t sis_cdev_write( struct file *file, const char __user *buf, size_t count, loff_t *f_pos )
{
	 int ret = 0;
	 char *kdata;
	 char cmd;
	 printk(KERN_INFO "sis_cdev_write.\n");
	 
	 if (ts_bak == 0)
    	return -13;
    	
    ret = access_ok(VERIFY_WRITE, buf, BUFFER_SIZE);
    if (!ret) {
        printk(KERN_ERR "cannot access user space memory\n");
        return -11;
    }

	 kdata = kmalloc(BUFFER_SIZE, GFP_KERNEL);
     if (kdata == 0)
    	return -12;
    	
     ret = copy_from_user(kdata, buf, BUFFER_SIZE);
     if (ret) {
        printk(KERN_ERR "copy_from_user fail\n");
        kfree(kdata);
        return -14;
     } 
#if 0
	PrintBuffer(0, count, kdata);
#endif

	cmd = kdata[6];

    printk(KERN_INFO "io cmd=%02x\n", cmd);

//Write & Read
    ret = sis_command_for_write(ts_bak->client, count, kdata);
    if (ret < 0) {
        printk(KERN_ERR "i2c_transfer write error %d\n", ret);
		kfree(kdata);
		return -21;
	}

    if ( copy_to_user((char*) buf, kdata, BUFFER_SIZE ) )
    {
        printk(KERN_ERR "copy_to_user fail\n" );
        ret = -19;
    }

    kfree( kdata );

	return ret;
}

//for get system time
ssize_t sis_cdev_read( struct file *file, char __user *buf, size_t count, loff_t *f_pos )
{
	 int ret = 0;
	 char *kdata;
	 char cmd;
	 int i;
	 printk(KERN_INFO "sis_cdev_read.\n");
	 
	 if (ts_bak == 0)
    	return -13;
    	
    ret = access_ok(VERIFY_WRITE, buf, BUFFER_SIZE);
    if (!ret) {
        printk(KERN_ERR "cannot access user space memory\n");
        return -11;
    }

	 kdata = kmalloc(BUFFER_SIZE, GFP_KERNEL);
     if (kdata == 0)
    	return -12;
    	
     ret = copy_from_user(kdata, buf, BUFFER_SIZE);
     if (ret) {
        printk(KERN_ERR "copy_from_user fail\n");
        kfree(kdata);
        return -14;
     }    
#if 0
    PrintBuffer(0, count, kdata);
#endif
	 cmd = kdata[6];
	 //for making sure AP communicates with SiS driver
    if(cmd == 0xa2)
    {
		kdata[0] = 5;
		kdata[1] = 0;
		kdata[3] = 'S';
		kdata[4] = 'i';
		kdata[5] = 'S';
		if ( copy_to_user((char*) buf, kdata, BUFFER_SIZE ) )
		{
			printk(KERN_ERR "copy_to_user fail\n" );
			kfree( kdata );
			return -19;
		}

		kfree( kdata );
		return 3;	
	}
//Write & Read
    ret = sis_command_for_read(ts_bak->client, MAX_BYTE, kdata);
    if (ret < 0) {
        printk(KERN_ERR "i2c_transfer read error %d\n", ret);
		kfree(kdata);
		return -21;
	}

	ret = kdata[0] | (kdata[1] << 8);

/*
    for ( i = 0; i < BUFFER_SIZE - 1; i++ ) {
	    kdata[i] = kdata[i+1];
    }
*/

    printk(KERN_INFO "%d\n", ret);

    for ( i = 0; i < ret && i < BUFFER_SIZE; i++ )
    {
        printk("%02x ", kdata[i]);
    }

    printk( "\n" );

    if ( copy_to_user((char*) buf, kdata, BUFFER_SIZE ) )
    {
        printk(KERN_ERR "copy_to_user fail\n" );
        ret = -19;
    }

    kfree( kdata );

	return ret;
}

#undef BUFFER_SIZE

int sis_cdev_open(struct inode *inode, struct file *filp)
{
	int reg_val; //add by Zerget 20121011
	
	printk(KERN_INFO "sis_cdev_open.\n");
	if ( ts_bak == 0 )
    	return -13;

	msleep(200);
	
	if (ts_bak->use_irq)
	{
	/*
		//Modify by Zerget 20121011
		reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
		if(reg_val&(1<<(IRQ_EINT21)))
		{
			disable_irq_nosync(SW_INT_IRQNO_PIO);
			writel(reg_val&(1<<(IRQ_EINT21)), gpio_addr + PIO_INT_STAT_OFFSET);
		}*/
		sw_gpio_eint_set_enable(CTP_IRQ_NUMBER,0);
	}
	
	hrtimer_cancel(&ts_bak->timer);
	flush_workqueue(sis_wq); 	   // only flush sis_wq
    
	msleep(200);
	return 0; /* success */
}

int sis_cdev_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "sis_cdev_release.\n");
	 msleep(200);
    if (ts_bak == 0)
    	return -13;

	if (ts_bak->use_irq)
	{
		sw_gpio_eint_set_enable(CTP_IRQ_NUMBER,1);
		//enable_irq(SW_INT_IRQNO_PIO); //Modify by Zerget 20121011 
	}
	else{
		hrtimer_start(&ts_bak->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

    return 0;
}

static const struct file_operations sis_cdev_fops = {
	.owner	= THIS_MODULE,
	.read	= sis_cdev_read,
	.write	= sis_cdev_write,
	.open	= sis_cdev_open,
	.release= sis_cdev_release,
};
/*
static int ctp_set_irq_mode(char *major_key , char *subkey, int ext_int_num, ext_int_mode int_mode)
{
	int ret = 0;
	__u32 reg_num = 0;
	__u32 reg_addr = 0;
	__u32 reg_val = 0;
	//config gpio to int mode
	pr_info("%s: config gpio to int mode. \n", __func__);
#ifndef SYSCONFIG_GPIO_ENABLE
#else
	if(gpio_int_hdle){
		gpio_release(gpio_int_hdle, 2);
	}
	gpio_int_hdle = gpio_request_ex(major_key, subkey);
	if(!gpio_int_hdle){
		pr_info("request tp_int_port failed. \n");
		ret = -1;
		goto request_tp_int_port_failed;
	}
#endif

#ifdef AW_GPIO_INT_API_ENABLE
#else
	pr_info(" INTERRUPT CONFIG\n");
	reg_num = ext_int_num%8;
	reg_addr = ext_int_num/8;
	reg_val = readl(gpio_addr + int_cfg_addr[reg_addr]);
	reg_val &= (~(7 << (reg_num * 4)));
	reg_val |= (int_mode << (reg_num * 4));
	writel(reg_val,gpio_addr+int_cfg_addr[reg_addr]);
                                                               
	ctp_clear_penirq();
                                                               
	reg_val = readl(gpio_addr+PIO_INT_CTRL_OFFSET); 
	reg_val |= (1 << ext_int_num);
	writel(reg_val,gpio_addr+PIO_INT_CTRL_OFFSET);

	udelay(1);
#endif

request_tp_int_port_failed:
	return ret;  
}
*/

int ctp_detect(struct i2c_client *client, struct i2c_board_info *info)
{
        struct i2c_adapter *adapter = client->adapter;
        int ret = 0, i = 0;
        int retry = 5;

        if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
        {
        	 printk("------------ sis925x_ts detect error!\n");
        	 return -ENODEV;
        }

        if(twi_id == adapter->nr)
        {
        		do{
							if(sis_check_fw_ready(client))
							{
								strlcpy(info->type, CTP_NAME, I2C_NAME_SIZE);
								return 0;
							} 
							printk(KERN_ERR "ctp_detect retry check i2c:%d\n", retry);
							msleep(20);
						}while(retry--);
        	/*
        	      msleep(200);
				        ret = i2c_smbus_read_byte_data(client,0xA3);
		                dprintk(DEBUG_INIT,"addr:0x%x,chip_id_value:0x%x\n",client->addr,ret);
		                while(chip_id_value[i++]){
	                      if(ret == chip_id_value[i - 1]){
	          	                strlcpy(info->type, CTP_NAME, I2C_NAME_SIZE);
	  		                return 0;
	                      }                   
		                }*/
			        	printk("%s:I2C connection might be something wrong ! \n",__func__);
			        	return -ENODEV;
        }else{
                return -ENODEV;
        }
}

static int sis_setup_chardev(struct sis_ts_data *ts)
{
	
	dev_t dev = MKDEV(sis_char_major, 0);
	int alloc_ret = 0;
	int cdev_err = 0;
	int input_err = 0;
	struct device *class_dev = NULL;
	void *ptr_err;
	
	printk("sis_setup_chardev.\n");
	
	if (ts == NULL) 
	{
	  input_err = -ENOMEM;
	  goto error;
	} 
	 // dynamic allocate driver handle
	alloc_ret = alloc_chrdev_region(&dev, 0, sis_char_devs_count, CDEV_DEVICE_NAME);
	if (alloc_ret)
		goto error;
		
	sis_char_major = MAJOR(dev);
	cdev_init(&sis_char_cdev, &sis_cdev_fops);
	sis_char_cdev.owner = THIS_MODULE;
	cdev_err = cdev_add(&sis_char_cdev, MKDEV(sis_char_major, 0), sis_char_devs_count);
	
	if (cdev_err) 
		goto error;
	
	printk(KERN_INFO "%s driver(major %d) installed.\n", CDEV_DEVICE_NAME, sis_char_major);
	
	// register class
	sis_char_class = class_create(THIS_MODULE, CDEV_DEVICE_NAME);
	if(IS_ERR(ptr_err = sis_char_class)) 
	{
		goto err2;
	}
	
	class_dev = device_create(sis_char_class, NULL, MKDEV(sis_char_major, 0), NULL, CDEV_DEVICE_NAME);
	
	if(IS_ERR(ptr_err = class_dev)) 
	{
		goto err;
	}
	
	return 0;
error:
	if (cdev_err == 0)
		cdev_del(&sis_char_cdev);
	if (alloc_ret == 0)
		unregister_chrdev_region(MKDEV(sis_char_major, 0), sis_char_devs_count);
	if(input_err != 0)
	{
		printk("sis_ts_bak error!\n");
	}
err:
	device_destroy(sis_char_class, MKDEV(sis_char_major, 0));
err2:
	class_destroy(sis_char_class);
	return -1;
}
#endif

static int sis_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0, retry = 10;
	struct sis_ts_data *ts;
	struct input_dev *input_dev;	
	struct sis_i2c_rmi_platform_data *pdata = NULL;	

	client->addr = 0x08;
	printk("========================================== sis_ts_probe============================\n");	
	pr_info("====%s begin=====.  \n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		printk("=========== sis_ts_probe check i2c error!\n");
		ret = -ENODEV;
		goto exit_check_functionality_failed;
	}	

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) 
	{
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	input_dev = input_allocate_device();
	if (input_dev == NULL) 
	{
		ret = -ENOMEM;
		printk(KERN_ERR "sis_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

 	ts->input_dev = input_dev;
    	ts->client = client;
        

	TPInfo = kzalloc(sizeof(struct sisTP_driver_data), GFP_KERNEL);
        if (TPInfo == NULL) 
        {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}


	ts_bak = ts;

	//1. Init Work queue and necessary buffers
	INIT_WORK(&ts->work, sis_ts_work_func);
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;
printk("^^^^^^^^^^^ %s\n",pdata);
	if (pdata)
	{
		printk("[%d] -------- pdata->power = %d\n",__LINE__,pdata->power);
		ts->power = pdata->power;
	}
	
	if (ts->power) 
	{
		printk("[%d] -------- ts->power = %d\n",__LINE__,ts->power);
		ret = ts->power(1);
		if (ret < 0) 
		{
			printk(KERN_ERR "sis_ts_probe power on failed\n");
			goto err_power_failed;
		}
	}

	//2. Allocate input device

	ts->input_dev->name = CTP_NAME;//"SiS9200-i2c-touchscreen";

	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
    set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
    set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
    set_bit(ABS_MT_TRACKING_ID, ts->input_dev->absbit);

#ifdef _ANDROID_4
    set_bit(ABS_MT_PRESSURE, ts->input_dev->absbit);
    set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
    input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, PRESSURE_MAX, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, AREA_LENGTH_LONGER, 0, 0);
#else
    set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
    set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
    input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, PRESSURE_MAX, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, AREA_LENGTH_LONGER, 0, 0);
#endif

    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, SIS_MAX_X, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, SIS_MAX_Y, 0, 0);
    input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 15, 0, 0);
	
	do{
		if(sis_check_fw_ready(client)) break;
		printk(KERN_ERR "sis_check_fw_ready retry:%d\n", retry);
		msleep(20);
	}while(retry--);
	
	if(retry == 0){
		//m_inet_ctpState=0;//add by  ximeng 20121018
		printk(KERN_ERR "sis_check_fw_ready---------error---\n");
		goto err_input_register_device_failed;
	}
	printk(KERN_ERR "sis_check_fw_ready------------\n");
//	m_inet_ctpState=1;//add by  ximeng 20121018
	
    /* add for touch keys */
	set_bit(KEY_COMPOSE, ts->input_dev->keybit);
	set_bit(KEY_BACK, ts->input_dev->keybit);
	set_bit(KEY_MENU, ts->input_dev->keybit);
	set_bit(KEY_HOME, ts->input_dev->keybit);

	//3. Register input device to core
	ret = input_register_device(ts->input_dev);
/*
	ret = ctp_set_irq_mode("ctp_para", "ctp_int_port", CTP_IRQ_NO, CTP_IRQ_MODE);
	if(0 != ret){
		pr_info("%s:ctp_ops.set_irq_mode err. \n", __func__);
		goto exit_set_irq_mode;
	}
	ret = request_irq(SW_INT_IRQNO_PIO, sis_ts_irq_handler, IRQF_TRIGGER_FALLING | IRQF_SHARED,client->addr, ts);
	if (ret == 0) 
	{
		ts->use_irq = 1;
	} 
	else 
	{
		dev_err(&client->dev, "request_irq failed\n");
	}
//	ts->desc = irq_to_desc(ts_bak->client->irq);
	*/
	int_handle = sw_gpio_irq_request(CTP_IRQ_NUMBER,CTP_IRQ_MODE,(peint_handle)sis_ts_irq_handler,ts);
	//int_handle = request_irq(CTP_IRQ_NUMBER, sis_ts_irq_handler, IRQF_TRIGGER_FALLING | IRQF_SHARED,client->addr, ts);
	if (!int_handle) {
		printk( "sis_ts_probe: request irq failed\n");
		goto error_req_irq_fail;
	}else
	{
		ts->use_irq = 1;
	}
	
	hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ts->timer.function = sis_ts_timer_func;

	if (!ts->use_irq) 
	{
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = sis_ts_early_suspend;
	ts->early_suspend.resume = sis_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	printk(KERN_INFO "sis_ts_probe: Start touchscreen %s in %s mode\n", ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	
	if (ts->use_irq)
	{
#ifdef _INT_MODE_1
			printk(KERN_INFO "sis_ts_probe: interrupt case 1 mode\n");
#else
			printk(KERN_INFO "sis_ts_probe: interrupt case 2 mode\n");
#endif
	}
	
#ifdef _STD_RW_IO
	ret = sis_setup_chardev(ts);
	if(ret)
	{
		printk( KERN_INFO"sis_setup_chardev fail\n");
	}
#endif
	//ctp_reset();
	
	return 0;
	
error_req_irq_fail:
        sw_gpio_irq_free(int_handle);
err_input_register_device_failed:
	input_free_device(ts->input_dev);
exit_set_irq_mode:
err_input_dev_alloc_failed:
err_power_failed:
	kfree(ts);
err_alloc_data_failed:
exit_check_functionality_failed:
	return ret;
}

static int sis_ts_remove(struct i2c_client *client)
{
	struct sis_ts_data *ts = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);
	sw_gpio_irq_free(int_handle);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

//Modify By Zerget 20121011
static int sis_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret = 0, reg_val;
	struct sis_ts_data *ts = i2c_get_clientdata(client);

#ifdef CONFIG_FW_SUPPORT_POWERMODE
	int retry = 5;
#endif

	TPInfo->pre_keybit_state = 0x0;

	if (ts->use_irq)
	{
		sw_gpio_eint_set_enable(CTP_IRQ_NUMBER,0);
		printk("9999999999999999999\n");
		//disable_irq(client->irq);
		/*reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
		if(reg_val&(1<<(IRQ_EINT21)))
		{
			disable_irq_nosync(SW_INT_IRQNO_PIO);
			writel(reg_val&(1<<(IRQ_EINT21)), gpio_addr + PIO_INT_STAT_OFFSET);
		}*/
	}
	else{
		hrtimer_cancel(&ts->timer);
	}
	
	flush_workqueue(sis_wq); 	   		// only flush sis_wq

#ifdef CONFIG_FW_SUPPORT_POWERMODE
		while (retry > 0 && sis_get_fw_mode(client) != POWER_MODE_SLEEP)
		{
			
			printk("sis_change_fw_mode return:%d\n", sis_change_fw_mode(client, POWER_MODE_SLEEP));				

			if (retry == 1)
				printk(KERN_ERR "sis_ts_suspend: change mode failed\n");
			else
				printk(KERN_ERR "sis_ts_suspend: change mode retry - %d\n", retry);
			retry--;
			msleep(500);
		}
#endif


#if 0
	/* Turn off SiS Chip*/
	/* TODO */
	gpio_direction_output(TOUCH_RESET_PIN, 0);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Reset Low\n");
//	msleep(5);
	gpio_direction_output(TOUCH_POWER_PIN, 0);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Power off\n");
#endif

	if (ts->power) {
		printk("8888888888\n");
		ret = ts->power(0);
		if (ret < 0)
			printk(KERN_ERR "sis_ts_suspend power off failed\n");
	}
	
	return 0;
}

static int sis_ts_resume(struct i2c_client *client)
{
	int ret = 0;
	struct sis_ts_data *ts = i2c_get_clientdata(client);

#ifdef CONFIG_FW_SUPPORT_POWERMODE
	int retry = 5;
#endif

	if (ts->power)
	{
		printk("[%d] -------- ts->power = %d\n",__LINE__,ts->power);
		ret = ts->power(1);
		if (ret < 0)
			printk(KERN_ERR "sis_ts_resume power on failed\n");
	}

#if 0
	/* Turn on SiS Chip*/
	/* TODO */
	gpio_direction_output(TOUCH_POWER_PIN, 1);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Power on\n");
	msleep(5);
	gpio_direction_output(TOUCH_RESET_PIN, 1);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Reset HI\n");
	msleep(5);
	gpio_direction_output(TOUCH_RESET_PIN, 0);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Reset Low\n");
	msleep(5);
	gpio_direction_output(TOUCH_RESET_PIN, 1);
	printk(KERN_INFO "[MSI TOUCH] SiS Touch Reset HI\n");
#endif

#ifdef CONFIG_FW_SUPPORT_POWERMODE

		while (retry > 0 && sis_get_fw_mode(client) != POWER_MODE_FWCTRL)
		{
			
			printk("sis_change_fw_mode return:%d\n", sis_change_fw_mode(client, POWER_MODE_FWCTRL));				

			if (retry == 1)
				printk(KERN_ERR "sis_ts_resume: change mode failed\n");
			else
				printk(KERN_ERR "sis_ts_resume: change mode retry - %d\n", retry);
			retry--;
			msleep(500);
		}
#endif
		//sis_fw_softreset(client);

	if (ts->use_irq)
	{
		sw_gpio_eint_set_enable(CTP_IRQ_NUMBER,1);
		//Modify by Zerget 20121011 
		//enable_irq(client->irq);
		//enable_irq(SW_INT_IRQNO_PIO);
	}
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void sis_ts_early_suspend(struct early_suspend *h)
{
	struct sis_ts_data *ts;
	printk("----- sis_ts_early_suspend -----\n");
	TPInfo->pre_keybit_state = 0x0;
	ts = container_of(h, struct sis_ts_data, early_suspend);
	sis_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void sis_ts_late_resume(struct early_suspend *h)
{
	struct sis_ts_data *ts;
	printk("----- sis_ts_late_resume ------\n");
	ts = container_of(h, struct sis_ts_data, early_suspend);
	sis_ts_resume(ts->client);
}
#endif

static const struct i2c_device_id sis_ts_id[] = {
	{ CTP_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, sis_ts_id);

static struct i2c_driver sis_ts_driver = {
	.class      = I2C_CLASS_HWMON,
	.probe		= sis_ts_probe,
	.remove		= sis_ts_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= sis_ts_suspend,
	.resume		= sis_ts_resume,
#endif
#ifdef CONFIG_X86
    .class      = I2C_CLASS_HWMON,
    .detect		= sis_ts_detect,
	.address_list	= normal_i2c,
#endif
	.id_table	= sis_ts_id,
	.driver = {
		.name	= CTP_NAME,
		.owner  = THIS_MODULE,
	},
	.address_list	=  normal_i2c,
};

static int ctp_get_system_config(void)
{   
	  ctp_print_info(config_info,DEBUG_INIT);
	  twi_id = config_info.twi_id;
	  screen_max_x = config_info.screen_max_x;
	  screen_max_y = config_info.screen_max_y;
	  revert_x_flag = config_info.revert_x_flag;
	  revert_y_flag = config_info.revert_y_flag;
	  exchange_x_y_flag = config_info.exchange_x_y_flag;
	  if((twi_id == 0) || (screen_max_x == 0) || (screen_max_y == 0)){
	          printk("%s:read config error!\n",__func__);
	          return 0;
	  }
	  return 1;
}

static int __devinit sis_ts_init(void)
{
	int ret = -1;
	char name[20];
	
	printk("****************************************************************\n");
	printk("***%s begin!***\n",__func__);
	pr_info("sis_ts VERSION =%s\n",VERSION);
	
	if(config_info.ctp_used == 0){
    printk("*** ctp_used set to 0 !\n");
    printk("*** if use ctp,please put the sys_config.fex ctp_used set to 1. \n");
    return 0;
	}
	
  if(!ctp_get_system_config()){
    printk("%s:read config fail!\n",__func__);
    return ret;
  }
        
	ctp_wakeup(0,5); 

	sis_wq = create_singlethread_workqueue("sis_wq");
	if (!sis_wq){
		return -ENOMEM;
		printk("==not creat thread==\n");
	}
	sis_ts_driver.detect = ctp_detect;
	ret = i2c_add_driver(&sis_ts_driver);
	printk("after i2c_add_driver");
	return 0;
}

#ifdef CONFIG_X86
/* Return 0 if detection is successful, -ENODEV otherwise */
static int sis_ts_detect(struct i2c_client *client,
		       struct i2c_board_info *info)
{
	const char *type_name;
    printk(KERN_INFO "sis_ts_detect\n");
	type_name = "sis_i2c_ts";
	strlcpy(info->type, type_name, I2C_NAME_SIZE);
	return 0;
}
#endif

static void __exit sis_ts_exit(void)
{
#ifdef _STD_RW_IO
	dev_t dev;
#endif

	printk(KERN_INFO "sis_ts_exit\n");
	i2c_del_driver(&sis_ts_driver);
	if (sis_wq)
		destroy_workqueue(sis_wq);

#ifdef _STD_RW_IO
	dev = MKDEV(sis_char_major, 0);
	cdev_del(&sis_char_cdev);
	unregister_chrdev_region(dev, sis_char_devs_count);
	device_destroy(sis_char_class, MKDEV(sis_char_major, 0));
	class_destroy(sis_char_class);
#endif
}

module_init(sis_ts_init);
module_exit(sis_ts_exit);
MODULE_DESCRIPTION("SiS 9200 Family Touchscreen Driver");
MODULE_LICENSE("GPL");
