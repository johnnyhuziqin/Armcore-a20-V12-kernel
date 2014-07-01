/*
 * Driver for keys on GPIO lines.
 *
 * Copyright 2009 jiangjianjun <jerryjianjun@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/io.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <linux/mfd/axp-mfd.h>
#include <asm/div64.h>

#include <mach/sys_config.h>

#include <linux/input.h>
#include <linux/reboot.h>

#include <linux/i2c.h>
#include <linux/clk.h>
#include <mach/system.h>

#include <linux/mfd/axp-mfd.h>

#define MAX_BUTTON_CNT		(3)

#define TIMER_MIN	60
static struct timer_list timer;

struct class *a20_boot_class;
static int count = 2;
static int flag;

static void a10button_timer_handler(unsigned long data)
{
//	printk ("Lynx ========>AAAAA flag = %d count = %d\n",flag,count);

	if (flag == 0 && count <= 0)
	{
		printk("timer reboot!\n");
		arch_reset(0,NULL);
	}

	if (count > 0)
	{		
		mod_timer(&timer, jiffies + (HZ*TIMER_MIN));
		count --;
	}

	return;
}

static ssize_t a20_boot_show(struct class *class, struct class_attribute *attr, char *buf)
{
//	printk ("%s : %d flag = %d !!!\n",__func__,__LINE__,flag);
	flag = 1;

	return 0;
}


static ssize_t a20_boot_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
    if(!strncmp(buf,"1",1)){
        printk("%s :  On.....\n",__func__);

        flag = 1;
        return count;
    }

    if(!strncmp(buf,"2",1)){
        printk("%s : Off.....\n",__func__);
        return count;
    }
    flag = 1;

    return count;
}

static CLASS_ATTR(boot, S_IRUGO|S_IWUSR|S_IWGRP,a20_boot_show, a20_boot_store);

static int a20_boot_probe(struct platform_device *pdev)
{

	/* Scan timer init */
	init_timer(&timer);
	timer.function = a10button_timer_handler;

	timer.expires = jiffies + (HZ*TIMER_MIN);
	add_timer(&timer);

	printk("a10 button Initialized!!\n");

    printk("%s: Entering......\n",__func__);
    a20_boot_class = class_create(THIS_MODULE, "a20_boot");
    if (IS_ERR(a20_boot_class)) {
        printk("Unable to create flash_led class; errno = %ld\n", PTR_ERR(a20_boot_class));
    }		
    if (class_create_file(a20_boot_class, &class_attr_boot) < 0){
        printk("Failed to create range info file");	
    }

	return 0;
}

static int a20_boot_remove(struct platform_device *pdev)
{
	class_destroy(a20_boot_class);
	del_timer(&timer);
	
	return  0;
}

#ifdef CONFIG_PM
static int a20_boot_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int a20_boot_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define a20_boot_suspend	NULL
#define a20_boot_resume	NULL
#endif

static struct platform_driver a20_boot_driver = {
	.probe		= a20_boot_probe,
	.remove		= a20_boot_remove,
	.suspend	= a20_boot_suspend,
	.resume		= a20_boot_resume,
	.driver		= {
		.name	= "a20_boot",
		.owner	= THIS_MODULE,
	}
};


static struct platform_device a20_boot_device = {
	.name	= "a20_boot",
	.id		= -1,
};

static int __init a20_boot_init(void)
{   
	platform_device_register(&a20_boot_device);       

	return platform_driver_register(&a20_boot_driver);
}

static void __exit a20_boot_exit(void)
{

	platform_driver_unregister(&a20_boot_driver);
	platform_device_unregister(&a20_boot_device);   
}

module_init(a20_boot_init);
module_exit(a20_boot_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jerry jianjun <jerryjianjun@gmail.com>");
MODULE_DESCRIPTION("Keyboard driver for a10 button.");
MODULE_ALIAS("platform:a20_boot");
