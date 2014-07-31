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
#include <mach/gpio.h>
#include <linux/gpio.h>

#define LRADC

#undef SWITCH_DBG
#if (0)
    #define SWITCH_DBG(format,args...)  printk("[SWITCH] "format,##args)    
#else
    #define SWITCH_DBG(...)    
#endif

#ifdef LRADC
#define LRADC_CTRL         (0x0)
#define LRADC_INTC         (0x4)
#define LRADC_INTS         (0x8)
#define LRADC_DATA0        (0xc)
#define LRADC_DATA1        (0x10)
extern int smdt_tvin_flag;
extern int codec_play_flag;
void headset_lradc(void);
#else
#define TP_CTRL0 			(0x0)
#define TP_CTRL1 			(0x4)
#define TP_INT_FIFO_CTR		(0x10)
#define TP_INT_FIFO_STATUS	(0x14)
#define TP_DATA				(0x24)
#define TP_CTRL_CLK_PARA  	(0x00a6002f)
#endif

#define FUNCTION_NAME "h2w"
#define TIMER_CIRCLE 50

/* modify by cjcheng. start {----------------------------------- */
/* ctrl mute speaker 2013-07-17 */
//static int gpio_earphone_switch = 0;
static int gpio_pa_shutdown = 0;
/* modify by cjcheng. end   -----------------------------------} */
static void __iomem *tpadc_base;
static void __iomem *lradc_base;
static int count_state;
static int switch_used = 0;

struct gpio_switch_data {
	struct switch_dev sdev;
	int pio_hdle;	
	int state;
	int pre_state;
	
	struct work_struct work;
	struct timer_list timer;
};

static void earphone_hook_handle(unsigned long data)
{
	int fifo_val[4];
	int ave_count;
	int temp;	
	struct gpio_switch_data	*switch_data = (struct gpio_switch_data *)data;	
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);

#ifdef LRADC
    if (lradc_base == SW_VA_LRADC_IO_BASE){
        if (codec_play_flag == 1) {
            int lradc_data = readl(lradc_base + LRADC_DATA0);
//            printk("+++++++++++++++++++++++++== data = %d\n", lradc_data);
            if (lradc_data == 0){
                switch_data->state = 2;
                __gpio_set_value(gpio_pa_shutdown, 1);
            }else if (lradc_data == 1){
                switch_data->state = 1;
                __gpio_set_value(gpio_pa_shutdown, 1);
            }

            if (lradc_data > 15 && lradc_data < 25){
                switch_data->state = 0;
                __gpio_set_value(gpio_pa_shutdown, 1);
            }

            if (lradc_data > 60 && lradc_data < 65){
                switch_data->state = 0;
                __gpio_set_value(gpio_pa_shutdown, 1);
            }

            if ((switch_data->pre_state != switch_data->state)){
//                    && ount_state++ >= 3) {
                printk("enter:%s,line:%d, pre_state: %d, state: %d\n",
                        __func__, __LINE__, switch_data->pre_state, switch_data->state);
                switch_data->pre_state = switch_data->state;
			schedule_work(&switch_data->work);
			//switch_set_state(&switch_data->sdev, switch_data->state);
                count_state = 0;
            }

        }else{
            if (smdt_tvin_flag == 1)
                __gpio_set_value(gpio_pa_shutdown, 1);
            else
                __gpio_set_value(gpio_pa_shutdown, 1);
        }
    } else
        printk("headset_error\n");
#else
	/*判断是否需要采集数据*/
	temp = readl(tpadc_base + TP_INT_FIFO_STATUS);
	temp &= (1<<16);

	if (temp) {	
		fifo_val[0] = readl(tpadc_base + TP_DATA);
		fifo_val[1] = readl(tpadc_base + TP_DATA);
		fifo_val[2] = readl(tpadc_base + TP_DATA);
		fifo_val[3] = readl(tpadc_base + TP_DATA);
		
		/*清理pending位*/
		temp = readl(tpadc_base + TP_INT_FIFO_STATUS);
		temp |= (1<<16);
		writel(temp, tpadc_base + TP_INT_FIFO_STATUS);
		
		/*取4次fifo中的数据作为平均数*/
		ave_count = (fifo_val[0] + fifo_val[1] + fifo_val[2] + fifo_val[3])/4;
		

		//SWITCH_DBG("%s,line:%d,fifo_val[0]:%d\n", __func__, __LINE__,fifo_val[0]);
		//SWITCH_DBG("%s,line:%d,fifo_val[1]:%d\n", __func__, __LINE__,fifo_val[1]);
		//SWITCH_DBG("%s,line:%d,fifo_val[2]:%d\n", __func__, __LINE__,fifo_val[2]);
		//SWITCH_DBG("%s,line:%d,fifo_val[3]:%d\n", __func__, __LINE__,fifo_val[3]);

		/*如果x2线端采样值大于2900，代表耳机拔出*/
		//SWITCH_DBG("%s,line:%d,ave_count:%d\n", __func__, __LINE__,ave_count);
		if (ave_count > 2200) {
			switch_data->state = 0;
			 //printk("state:%d\n",switch_data->state);
			 //pa_dde=(pa_dde||(1<<2));
			// printk("0xf1c22c00 is:%x\n", *(volatile int *)0xf1c22c28);
			SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
		/*如果x2线端采样值在0~500之间，代表3段耳机插入*/
		} else if (ave_count < 500) {
			switch_data->state = 2;
			
			//gpio_write_one_pin_value(gpio_earphone_switch, 0, "audio_earphone_ctrl");
			//pa_dde=(pa_dde&&!(1<<2));
			 //printk("0xf1c22c00 is:%x\n", *(volatile int *)0xf1c22c28);
			//printk("state:%d\n",switch_data->state);
			SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
		/*如果x2线端采样值大于600,小于2600,代表4段耳机插入*/
		} else if (ave_count >= 600 && ave_count < 2000) {
		   	switch_data->state = 1;
		   //	printk("state:%d\n",switch_data->state);
		   //	gpio_write_one_pin_value(gpio_earphone_switch, 1, "audio_earphone_ctrl");
		   	
		   	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	       	// pa_dde=(pa_dde&&!(1<<2));
			 //printk("0xf1c22c00 is:%x\n", *(volatile int *)0xf1c22c28);
		   	
		   	/*如果是4段耳机,那么50ms后再次检查是否打开了hook(可以通过耳机mic通话)*/
			mdelay(30);
			
			temp = readl(tpadc_base + TP_INT_FIFO_STATUS);
			temp &= (1<<16);
			/*判断hook键是否按下*/
			if (temp) {
				fifo_val[0] = readl(tpadc_base + TP_DATA);
				fifo_val[1] = readl(tpadc_base + TP_DATA);
				fifo_val[2] = readl(tpadc_base + TP_DATA);
				fifo_val[3] = readl(tpadc_base + TP_DATA);
				
				temp = readl(tpadc_base + TP_INT_FIFO_STATUS);
				temp |= (1<<16);
				writel(temp, tpadc_base + TP_INT_FIFO_STATUS);
			
				/*取4次fifo中的数据作为平均数*/
				ave_count = (fifo_val[0] + fifo_val[1] + fifo_val[2] + fifo_val[3])/4; 
				if (ave_count <= 410) {
					SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);		
					switch_data->state = 3;
				}
/*				SWITCH_DBG("%s,line:%d,fifo_val[0]:%d\n", __func__, __LINE__,fifo_val[0]);
				SWITCH_DBG("%s,line:%d,fifo_val[1]:%d\n", __func__, __LINE__,fifo_val[1]);
				SWITCH_DBG("%s,line:%d,fifo_val[2]:%d\n", __func__, __LINE__,fifo_val[2]);
				SWITCH_DBG("%s,line:%d,fifo_val[3]:%d\n", __func__, __LINE__,fifo_val[3]);
*/
			}
		}
	
		if ((switch_data->pre_state != switch_data->state)
			&& count_state++ >= 3) {
			printk("enter:%s,line:%d, pre_state: %d, state: %d\n", 
					__func__, __LINE__, switch_data->pre_state, switch_data->state);
			switch_data->pre_state = switch_data->state;
			schedule_work(&switch_data->work);
			//switch_set_state(&switch_data->sdev, switch_data->state);
			count_state = 0;
		}
	}
#endif
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

#ifdef LRADC
void headset_lradc(void)
{
    static int pre_data=0;
    int data = readl(lradc_base + LRADC_DATA0);
    if (pre_data != data){
        if (data == 0){
            __gpio_set_value(gpio_pa_shutdown, 1);
        }else if (data == 1){
            __gpio_set_value(gpio_pa_shutdown, 1);
        }

        if (data > 15 && data < 25){
            __gpio_set_value(gpio_pa_shutdown, 1);
        }

        if (data > 60 && data < 65){
            __gpio_set_value(gpio_pa_shutdown, 1);
        }
    }
    pre_data = data;
}
EXPORT_SYMBOL(headset_lradc);
static ssize_t show_headset_data(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "data0 = %d, data1 = %d\n", readl(lradc_base + LRADC_DATA0), readl(lradc_base+LRADC_DATA1));
}

static ssize_t show_headset_inter(struct device *dev, struct device_attribute *attr, char *buf)
{
    unsigned int intc = 0x0;
    unsigned int ints = 0x0;
    intc = readl(lradc_base + LRADC_INTC);
    ints = readl(lradc_base + LRADC_INTS);
	return sprintf(buf, "intc = 0x%x, ints = 0x%x\n", intc, ints);
}

static ssize_t set_headset_inter(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int data = 0x0;
    unsigned int flag = 0x0;
	if (count <= 0)
		return 0;

	sscanf(buf, "0x%x,0x%x", &flag, &data);
    printk("flag = 0x%x , data = 0x%x\n", flag, data);
    if (flag == 0x4)
	    writel(data, lradc_base + LRADC_INTC);
    else if (flag == 0x8)
	    writel(data, lradc_base + LRADC_INTS);

	return count;
}

static ssize_t show_headset_ctrl(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "headset_ctrl = 0x%x\n", readl(lradc_base + LRADC_CTRL));
}

static ssize_t set_headset_ctrl(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int lradc_ctrl = 0x0;

	if (count <= 0)
		return 0;
	sscanf(buf, "%x", &lradc_ctrl);
    printk("lradc_ctrl = 0x%x \n", lradc_ctrl);
	lradc_base = (void __iomem *)SW_VA_LRADC_IO_BASE;
	writel(lradc_ctrl, lradc_base + LRADC_CTRL);

	return count;
}
static DEVICE_ATTR(headset_ctrl, S_IRUGO | S_IWUSR, show_headset_ctrl, set_headset_ctrl);
static DEVICE_ATTR(headset_inter, S_IRUGO | S_IWUSR, show_headset_inter, set_headset_inter);
static DEVICE_ATTR(headset_data, S_IRUGO | S_IWUSR, show_headset_data, NULL);
static struct attribute *dev_attrs[] = {
            &dev_attr_headset_ctrl.attr,
            &dev_attr_headset_inter.attr,
            &dev_attr_headset_data.attr,
            NULL,
};

static struct attribute_group dev_attr_grp = {
            .attrs = dev_attrs,
};
#endif

static int gpio_switch_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_switch_data *switch_data;
	int ret = 0;
	int temp;
	script_item_value_type_e  type;
    script_item_u item;
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	
	if (!pdata) {
		return -EBUSY;
	}

#ifdef LRADC
	ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_grp);
	if (ret < 0) {
		printk("%s : sysfs_create_group err\n", __func__);
		return ret;
	}

    lradc_base = (void __iomem *)SW_VA_LRADC_IO_BASE;
	writel(0x1, lradc_base + LRADC_CTRL);
#else
	tpadc_base = (void __iomem *)SW_VA_TP_IO_BASE;	
	writel(TP_CTRL_CLK_PARA, tpadc_base + TP_CTRL0);	
	temp = readl(tpadc_base + TP_CTRL1);
	temp |= ((1<<3) | (0<<4)); //tp mode function disable and select ADC module.
	temp |=0x1;//X2 channel	
	writel(temp, tpadc_base + TP_CTRL1);

	temp = readl(tpadc_base + TP_INT_FIFO_CTR);
	temp |= (1<<16); //TP FIFO Data available IRQ Enable
	temp |= (0x3<<8); //4次采样数据的平均值 (3+1)
	writel(temp, tpadc_base + TP_INT_FIFO_CTR);
	
	temp = readl(tpadc_base + TP_CTRL1);
	temp |= (1<<4);	
	writel(temp, tpadc_base + TP_CTRL1);	
	
	SWITCH_DBG("TP adc 0xf1c25000 is:%x\n", *(volatile int *)0xf1c25000);
	SWITCH_DBG("TP adc 0xf1c25004 is:%x\n", *(volatile int *)0xf1c25004);
	SWITCH_DBG("TP adc 0xf1c25008 is:%x\n", *(volatile int *)0xf1c25008);
	SWITCH_DBG("TP adc 0xf1c2500c is:%x\n", *(volatile int *)0xf1c2500c);
	SWITCH_DBG("TP adc 0xf1c25010 is:%x\n", *(volatile int *)0xf1c25010);
	SWITCH_DBG("TP adc 0xf1c25014 is:%x\n", *(volatile int *)0xf1c25014);
	SWITCH_DBG("TP adc 0xf1c25018 is:%x\n", *(volatile int *)0xf1c25018);
	SWITCH_DBG("TP adc 0xf1c2501c is:%x\n", *(volatile int *)0xf1c2501c);
	SWITCH_DBG("TP adc 0xf1c25020 is:%x\n", *(volatile int *)0xf1c25020);

#endif
	switch_data = kzalloc(sizeof(struct gpio_switch_data), GFP_KERNEL);
	if (!switch_data) {
		return -ENOMEM;
	}
	//gpio_earphone_switch = gpio_request_ex("audio_para", "audio_earphone_ctrl");
//	if(!gpio_earphone_switch) {
//		printk("earphone request gpio fail!\n");
//		ret = gpio_earphone_switch;
//		goto err_gpio_request;
//	}
//	
    type = script_get_item("gpio_para", "gpio_pin_3", &item);
    if (SCIRPT_ITEM_VALUE_TYPE_PIO != type) {
        printk("script_get_item return type err\n");
        return -EFAULT;
    }else{
        gpio_pa_shutdown = item.gpio.gpio;
    }

	switch_data->sdev.state = 0;
	switch_data->pre_state = -1;
	switch_data->sdev.name = pdata->name;	
    /* modify by cjcheng. start {----------------------------------- */
    /* ctrl mute speaker 2013-07-17 */
//	switch_data->pio_hdle = gpio_earphone_switch;
    /* modify by cjcheng. end   -----------------------------------} */
	switch_data->sdev.print_name = print_headset_name;
	switch_data->sdev.print_state = switch_gpio_print_state;
	INIT_WORK(&switch_data->work, earphone_switch_work);

    ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0) {
		goto err_switch_dev_register;
	}

#if 0		
	setup_timer(&switch_data->timer, earphone_hook_handle, (unsigned long)switch_data);
	mod_timer(&switch_data->timer, jiffies + HZ/2);
#endif

#if 1
	init_timer(&switch_data->timer);
	switch_data->timer.expires = jiffies + 1 * HZ;
	switch_data->timer.function = &earphone_hook_handle;
	switch_data->timer.data = (unsigned long)switch_data;
	add_timer(&switch_data->timer);
#endif
	return 0;

err_switch_dev_register:
		//gpio_release(switch_data->pio_hdle, 1);
/* modify by cjcheng. start {----------------------------------- */
/* ctrl mute speaker 2013-07-17 */
		gpio_free(gpio_pa_shutdown); // add by cjcheng
/* modify by cjcheng. end   -----------------------------------} */
err_gpio_request:
		kfree(switch_data);

	return ret;
}

static int __devexit gpio_switch_remove(struct platform_device *pdev)
{
	struct gpio_switch_data *switch_data = platform_get_drvdata(pdev);
	
	SWITCH_DBG("enter:%s,line:%d\n", __func__, __LINE__);
	
#ifdef LRADC
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_grp);
#endif
	//gpio_release(switch_data->pio_hdle, 1);
/* modify by cjcheng. start {----------------------------------- */
/* ctrl mute speaker 2013-07-17 */
	//gpio_release(gpio_earphone_switch, 1);
	gpio_free(gpio_pa_shutdown); // add by cjcheng
/* modify by cjcheng. end   -----------------------------------} */
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
	script_item_value_type_e  type;	/* 获取audio_used值 */	
	type = script_get_item("switch_para", "switch_used", &val);	
	if(SCIRPT_ITEM_VALUE_TYPE_INT != type){		
		printk("type err!");	}	
		pr_info("value is %d\n", val.val);    
		switch_used=val.val;
    if (switch_used) {
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
