/*
 *  drivers/switch/switch_gpio.c
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
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
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <linux/switch.h>
#include <mach/sys_config.h>
#include <mach/system.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>


#undef SWITCH_DBG
#if (0)
    #define SWITCH_DBG(format,args...)  printk("[SWITCH] "format,##args)    
#else
    #define SWITCH_DBG(...)    
#endif

#define  KEY_BASSADDRESS	(0xf1c22800)
#define  LRADC_CTRL		(0x00)
#define  LRADC_INTC		(0x04)
#define  LRADC_INT_STA 		(0x08)
#define  LRADC_DATA0		(0x0c)
#define  LRADC_DATA1		(0x10)

#define  FIRST_CONCERT_DLY	(2<<24)
#define  CHAN			(0x3)
#define  ADC_CHAN_SELECT	(CHAN<<22)
#define  LRADC_KEY_MODE		(0)
#define  KEY_MODE_SELECT	(LRADC_KEY_MODE<<12)
#define  LEVELB_VOL		(0<<4)

#define  LRADC_HOLD_EN		(1<<6)

#define  LRADC_SAMPLE_32HZ	(3<<2)
#define  LRADC_SAMPLE_62HZ	(2<<2)
#define  LRADC_SAMPLE_125HZ	(1<<2)
#define  LRADC_SAMPLE_250HZ	(0<<2)


#define  LRADC_EN		(1<<0)

#define  LRADC_ADC1_UP_EN	(1<<12)
#define  LRADC_ADC1_DOWN_EN	(1<<9)
#define  LRADC_ADC1_DATA_EN	(1<<8)

#define  LRADC_ADC0_UP_EN	(1<<4)
#define  LRADC_ADC0_DOWN_EN	(1<<1)
#define  LRADC_ADC0_DATA_EN	(1<<0)

#define  LRADC_ADC1_UPPEND	(1<<12)
#define  LRADC_ADC1_DOWNPEND	(1<<9)
#define  LRADC_ADC1_DATAPEND	(1<<8)


#define  LRADC_ADC0_UPPEND 	(1<<4)
#define  LRADC_ADC0_DOWNPEND	(1<<1)
#define  LRADC_ADC0_DATAPEND	(1<<0)


#define FUNCTION_NAME "h2w" 
static int gpio_earphone_switch = 0;
static int switch_used = 0;
static int mic_sw_gpio = 0;
static script_item_u	hp_det_gpio;
static script_item_u    spk_mute_ctl;
static	int start_val=-2;
static	int end_val=-1;

struct gpio_switch_data {
	struct switch_dev sdev;
	int pio_hdle;	
	int state;
	int pre_state;
	unsigned int gpio_pa_shutdown;
	struct work_struct work;
	struct timer_list timer;
};

static void earphone_hook_handle(unsigned long data)
{	 	
	struct gpio_switch_data	*switch_data = (struct gpio_switch_data *)data;	
	
	unsigned int  reg_val;
	volatile unsigned int key_val;
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	  
	 start_val = __gpio_get_value(hp_det_gpio.gpio.gpio);
	 SWITCH_DBG("%s,start_val=%d,end_val=%d\n", __func__, start_val, end_val);
	 
	 if(start_val!=end_val)
	 {
	  	 end_val = start_val;
		 if(start_val)	
		 {		
		     __gpio_set_value(mic_sw_gpio, 1);
			 __gpio_set_value(spk_mute_ctl.gpio.gpio, 1);
			 switch_data->state = 2;			   	
		 }
		 else
		 {		
		 	  __gpio_set_value(mic_sw_gpio, 0);  /*mic_sw 主MIC与耳机MIC切换，*/
			  __gpio_set_value(spk_mute_ctl.gpio.gpio, 0); /*mute_ctl控制喇叭功放，必须为低*/
			  switch_data->state = 0;
		 }		  
		  
		 schedule_work(&switch_data->work);
    }
	 
	mod_timer(&switch_data->timer, jiffies + msecs_to_jiffies(200));	
}

static void earphone_switch_work(struct work_struct *work)
{
	struct gpio_switch_data	*data =
		container_of(work, struct gpio_switch_data, work);
	SWITCH_DBG("%s,line:%d, data->state:%d\n", __func__, __LINE__, data->state);
	switch_set_state(&data->sdev, data->state);
}
static ssize_t switch_gpio_print_state(struct switch_dev *sdev, char *buf)
{	
	struct gpio_switch_data	*switch_data =
		container_of(sdev, struct gpio_switch_data, sdev);
	
	return sprintf(buf, "%d\n", switch_data->state);	
}

static ssize_t print_headset_name(struct switch_dev *sdev, char *buf)
{
	struct gpio_switch_data	*switch_data =
		container_of(sdev, struct gpio_switch_data, sdev);
	
	return sprintf(buf, "%s\n", switch_data->sdev.name);
}

static int gpio_switch_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_switch_data *switch_data;
	int ret = 0;
	int temp;
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	
	if (!pdata) {
		return -EBUSY;
	}	
	
	switch_data = kzalloc(sizeof(struct gpio_switch_data), GFP_KERNEL);
	if (!switch_data) {
		return -ENOMEM;
	}
	
	switch_data->sdev.state = 0;
	switch_data->pre_state = -1;
	switch_data->sdev.name = pdata->name;	
	switch_data->pio_hdle = gpio_earphone_switch;
	switch_data->sdev.print_name = print_headset_name;
	switch_data->sdev.print_state = switch_gpio_print_state;
	INIT_WORK(&switch_data->work, earphone_switch_work);

    ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0) {
		goto err_switch_dev_register;
	}

	init_timer(&switch_data->timer);
	switch_data->timer.expires = jiffies + 1 * HZ;
	switch_data->timer.function = &earphone_hook_handle;
	switch_data->timer.data = (unsigned long)switch_data;
	add_timer(&switch_data->timer);

	return 0;

err_switch_dev_register:
		//gpio_release(switch_data->pio_hdle, 1);
err_gpio_request:
		kfree(switch_data);

	return ret;
}

static int __devexit gpio_switch_remove(struct platform_device *pdev)
{
	struct gpio_switch_data *switch_data = platform_get_drvdata(pdev);
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	
	//gpio_release(switch_data->pio_hdle, 1);
	//gpio_release(gpio_earphone_switch, 1);
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	del_timer(&switch_data->timer);	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
    switch_dev_unregister(&switch_data->sdev);
    SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	kfree(switch_data);
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	return 0;
}

static struct platform_driver gpio_switch_driver = {
	.probe		= gpio_switch_probe,
	.remove		= __devexit_p(gpio_switch_remove),
	.driver		= {
		.name	= "switch-gpio",
		.owner	= THIS_MODULE,
	},
};

static struct gpio_switch_platform_data headset_switch_data = { 
    .name = "h2w",
};

static struct platform_device gpio_switch_device = { 
    .name = "switch-gpio",
    .dev = { 
            .platform_data = &headset_switch_data,
    }   
};

static int __init gpio_switch_init(void)
{
	int ret = 0;	
	static script_item_u   val;	
	static script_item_u   val1;	
	script_item_value_type_e  type;	/* 获取audio_used值 */		
	
	type = script_get_item("switch_para", "switch_used", &val);	
	if(SCIRPT_ITEM_VALUE_TYPE_INT != type){		
		printk("type err!");	}	
		pr_info("value is %d\n", val.val);    
		switch_used=val.val;
    if (switch_used) {
		type = script_get_item("switch_para", "mic_sw", &val1);
		if (SCIRPT_ITEM_VALUE_TYPE_PIO != type) {
			printk("failed to fetch mic_sw\n");
			return ;
		}
		mic_sw_gpio = val1.gpio.gpio;
	    if (0 != gpio_request_one(mic_sw_gpio, GPIOF_OUT_INIT_LOW, NULL)){
	        printk("[switch_para] gpio_request mic_sw:%d failed\n", mic_sw_gpio);
	        return ;
	    }
		
		type = script_get_item("switch_para", "hp_det", &hp_det_gpio);
		if(SCIRPT_ITEM_VALUE_TYPE_PIO != type) {
			printk("hp_det type err!");			
			return ;
		}

		if(0 != gpio_request(hp_det_gpio.gpio.gpio, NULL)) {
			printk("ERROR: hp_det Gpio_request is failed\n");			
			return ;
		}
		
		if (0 != sw_gpio_setall_range(&hp_det_gpio.gpio, 1)) {
			printk("hp_det sw_gpio_setall_range  err!");
			return ;
		}

		type = script_get_item("switch_para", "spk_mute_ctl", &spk_mute_ctl);
		if (SCIRPT_ITEM_VALUE_TYPE_PIO != type) {
			printk("failed to fetch mic_sw\n");
			return ;
		}
		
	    if (0 != gpio_request_one(spk_mute_ctl.gpio.gpio, GPIOF_OUT_INIT_HIGH, NULL)){
	        printk("[switch_para] gpio_request spk_mute_ctl:%d failed\n", spk_mute_ctl.gpio.gpio);
	        return ;
	    }
		
		ret = platform_device_register(&gpio_switch_device);
		if (ret == 0) {
			ret = platform_driver_register(&gpio_switch_driver);
		}
	} else {
		SWITCH_DBG("[switch]switch headset cannot find any using configuration for controllers, return directly!\n");
		return 0;
	}
	return ret;
}

static void __exit gpio_switch_exit(void)
{
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	if (switch_used) {
		switch_used = 0;
		platform_driver_unregister(&gpio_switch_driver);
		platform_device_unregister(&gpio_switch_device);
	}
}
module_init(gpio_switch_init);
module_exit(gpio_switch_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("GPIO Switch driver");
MODULE_LICENSE("GPL");
