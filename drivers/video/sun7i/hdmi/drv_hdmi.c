#include "drv_hdmi_i.h"
#include "hdmi_hal.h"
#include "dev_hdmi.h"


static struct semaphore *run_sem = NULL;
static struct task_struct * HDMI_task;
static __bool hdmi_used;
static __bool b_hdmi_suspend;
__s32 Hdmi_suspend(void);
__s32 Hdmi_resume(void);

void hdmi_delay_ms(__u32 t)
{
        __u32 timeout = t*HZ/1000;

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(timeout);
}


__s32 Hdmi_open(void)
{
        __inf("[Hdmi_open]\n");

        Hdmi_hal_video_enable(1);
	//if(ghdmi.bopen == 0)
	//{
	//	up(run_sem);
	//}
	ghdmi.bopen = 1;

	return 0;
}

__s32 Hdmi_close(void)
{
        __inf("[Hdmi_close]\n");
    
	Hdmi_hal_video_enable(0); 
	ghdmi.bopen = 0;

	return 0;
}

__s32 Hdmi_set_display_mode(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode;

	__inf("[Hdmi_set_display_mode],mode:%d\n",mode);
	
	switch(mode)
	{
	case DISP_TV_MOD_480I:
		hdmi_mode = HDMI1440_480I;
		break;
		
	case DISP_TV_MOD_576I:
		hdmi_mode = HDMI1440_576I;
		break;
		
	case DISP_TV_MOD_480P:
		hdmi_mode = HDMI480P;
		break;
		
	case DISP_TV_MOD_576P:
		hdmi_mode = HDMI576P;
		break;  
		
	case DISP_TV_MOD_720P_50HZ:
		hdmi_mode = HDMI720P_50;
		break;
		
	case DISP_TV_MOD_720P_60HZ:
		hdmi_mode = HDMI720P_60;
		break;
		
	case DISP_TV_MOD_1080I_50HZ:
		hdmi_mode = HDMI1080I_50;
		break;
		
	case DISP_TV_MOD_1080I_60HZ:
		hdmi_mode = HDMI1080I_60;
		break;         
		
	case DISP_TV_MOD_1080P_24HZ:
		hdmi_mode = HDMI1080P_24;
		break;    
		
	case DISP_TV_MOD_1080P_50HZ:
		hdmi_mode = HDMI1080P_50;
		break;
		
	case DISP_TV_MOD_1080P_60HZ:
		hdmi_mode = HDMI1080P_60;
		break;  
		
	case DISP_TV_MOD_1080P_25HZ:
		hdmi_mode = HDMI1080P_25;
		break;  
		
	case DISP_TV_MOD_1080P_30HZ:
		hdmi_mode = HDMI1080P_30;
		break;  

	case DISP_TV_MOD_1080P_24HZ_3D_FP:
		hdmi_mode = HDMI1080P_24_3D_FP;
		break;  
		
        case DISP_TV_MOD_720P_50HZ_3D_FP:
                hdmi_mode = HDMI720P_50_3D_FP;
                break;

        case DISP_TV_MOD_720P_60HZ_3D_FP:
                hdmi_mode = HDMI720P_60_3D_FP;
                break;

	default:
	    __wrn("unsupported video mode %d when set display mode\n", mode);
		return -1;
	}

	ghdmi.mode = mode;
	return Hdmi_hal_set_display_mode(hdmi_mode);
}

__s32 Hdmi_Audio_Enable(__u8 mode, __u8 channel)
{
        __inf("[Hdmi_Audio_Enable],ch:%d\n",channel);
    
	return Hdmi_hal_audio_enable(mode, channel);
}

__s32 Hdmi_Set_Audio_Para(hdmi_audio_t * audio_para)
{
        __inf("[Hdmi_Set_Audio_Para]\n");
    
	return Hdmi_hal_set_audio_para(audio_para);
}

__s32 Hdmi_mode_support(__disp_tv_mode_t mode)
{
	__u32 hdmi_mode;
	
	switch(mode)
	{
	case DISP_TV_MOD_480I:
		hdmi_mode = HDMI1440_480I;
		break;
		
	case DISP_TV_MOD_576I:
		hdmi_mode = HDMI1440_576I;
		break;
		
	case DISP_TV_MOD_480P:
		hdmi_mode = HDMI480P;
		break;
		
	case DISP_TV_MOD_576P:
		hdmi_mode = HDMI576P;
		break;  
		
	case DISP_TV_MOD_720P_50HZ:
		hdmi_mode = HDMI720P_50;
		break;
		
	case DISP_TV_MOD_720P_60HZ:
		hdmi_mode = HDMI720P_60;
		break;
		
	case DISP_TV_MOD_1080I_50HZ:
		hdmi_mode = HDMI1080I_50;
		break;
		
	case DISP_TV_MOD_1080I_60HZ:
		hdmi_mode = HDMI1080I_60;
		break;         
		
	case DISP_TV_MOD_1080P_24HZ:
		hdmi_mode = HDMI1080P_24;
		break;    
		
	case DISP_TV_MOD_1080P_50HZ:
		hdmi_mode = HDMI1080P_50;
		break;
		
	case DISP_TV_MOD_1080P_60HZ:
		hdmi_mode = HDMI1080P_60;
		break;  
		
	case DISP_TV_MOD_1080P_25HZ:
		hdmi_mode = HDMI1080P_25;
		break;  

	case DISP_TV_MOD_1080P_30HZ:
		hdmi_mode = HDMI1080P_30;
		break;  

	case DISP_TV_MOD_1080P_24HZ_3D_FP:
	        hdmi_mode = HDMI1080P_24_3D_FP;
	        break;

        case DISP_TV_MOD_720P_50HZ_3D_FP:
                hdmi_mode = HDMI720P_50_3D_FP;
                break;

        case DISP_TV_MOD_720P_60HZ_3D_FP:
                hdmi_mode = HDMI720P_60_3D_FP;
                break;

	default:
		hdmi_mode = HDMI720P_50;
		break;
	}

	return Hdmi_hal_mode_support(hdmi_mode);
}

__s32 Hdmi_get_HPD_status(void)
{
	return Hdmi_hal_get_HPD();
}


__s32 Hdmi_set_pll(__u32 pll, __u32 clk)
{
        Hdmi_hal_set_pll(pll, clk);
        return 0;
}

int Hdmi_run_thread(void *parg)
{
	while (1)
	{
		//if(ghdmi.bopen == 0)
		//{
		//	down(run_sem);
		//}
		if(kthread_should_stop())
                {
                        break;
                }   

		if(!b_hdmi_suspend)
                {
                        Hdmi_hal_main_task();
                }

		if(ghdmi.bopen)
		{		    
			hdmi_delay_ms(200);
		}
		else
		{
			hdmi_delay_ms(200);   
		}
	}

	return 0;
}

static struct switch_dev hdmi_switch_dev = {     
        .name = "hdmi",  
};

void hdmi_report_hpd_work(struct work_struct *work)
{
	char buf[16];
	char *envp[2];

        if(Hdmi_get_HPD_status())
        {
                //snprintf(buf, sizeof(buf), "HDMI_PLUGIN");
                //switch_set_state(&hdmi_switch_dev, 1);
                __inf("switch_set_state 1\n");
        }
        else
        {
                //snprintf(buf, sizeof(buf), "HDMI_PLUGOUT");
                switch_set_state(&hdmi_switch_dev, 0);
                __inf("switch_set_state 0\n");
        }
    
	envp[0] = buf;
	envp[1] = NULL;
	//kobject_uevent_env(&ghdmi.dev->kobj, KOBJ_CHANGE, envp);
}

__s32 hdmi_hpd_state(__u32 state)
{
        if(state == 0)
        {
                switch_set_state(&hdmi_switch_dev, 0);
        }else
        {
                switch_set_state(&hdmi_switch_dev, 1);
        }

        return 0;
}
/**
 * hdmi_hpd_report - report hdmi hot plug state to user space
 * @hotplug:	0: hdmi plug out;   1:hdmi plug in
 *
 * always return success.
 */
__s32 Hdmi_hpd_event()
{
        schedule_work(&ghdmi.hpd_work);

        return 0;
}
extern void audio_set_hdmi_func(__audio_hdmi_func * hdmi_func);
extern __s32 disp_set_hdmi_func(__disp_hdmi_func * func);

__s32 Hdmi_init(void)
{	    
    __audio_hdmi_func audio_func;
    __disp_hdmi_func disp_func;
    script_item_u   val;
    script_item_value_type_e  type;

    hdmi_used = 0;
    b_hdmi_suspend = 0;
    
    type = script_get_item("hdmi_para", "hdmi_used", &val);
    if(SCIRPT_ITEM_VALUE_TYPE_INT == type)
    {
        hdmi_used = val.val;
        if(hdmi_used)
    	{
        	run_sem = kmalloc(sizeof(struct semaphore),GFP_KERNEL | __GFP_ZERO);
        	sema_init((struct semaphore*)run_sem,0);
        	
        	HDMI_task = kthread_create(Hdmi_run_thread, (void*)0, "hdmi proc");
        	if(IS_ERR(HDMI_task))
        	{
        	    __s32 err = 0;
        	    
        		__wrn("Unable to start kernel thread %s.\n","hdmi proc");
        		err = PTR_ERR(HDMI_task);
        		HDMI_task = NULL;
        		return err;
        	}
        	wake_up_process(HDMI_task);

                Hdmi_set_reg_base((__u32)ghdmi.base_hdmi);
        	Hdmi_hal_init();

                audio_func.hdmi_audio_enable = Hdmi_Audio_Enable;
                audio_func.hdmi_set_audio_para = Hdmi_Set_Audio_Para;
        	audio_set_hdmi_func(&audio_func);

        	disp_func.hdmi_open = Hdmi_open;
        	disp_func.hdmi_close = Hdmi_close;
        	disp_func.hdmi_set_mode = Hdmi_set_display_mode;
        	disp_func.hdmi_mode_support = Hdmi_mode_support;
        	disp_func.hdmi_get_HPD_status = Hdmi_get_HPD_status;
        	disp_func.hdmi_set_pll = Hdmi_set_pll;
            
                disp_func.hdmi_suspend = Hdmi_suspend;
                disp_func.hdmi_resume = Hdmi_resume;
        	disp_set_hdmi_func(&disp_func);

                INIT_WORK(&ghdmi.hpd_work, hdmi_report_hpd_work);
                switch_dev_register(&hdmi_switch_dev);  
        }
    }

    return 0;
}

__s32 Hdmi_exit(void)
{
        if(hdmi_used)
        {
                Hdmi_hal_exit();

        	if(run_sem)
                {
                        kfree(run_sem);
                }    		

                run_sem = 0;
        	if(HDMI_task)
        	{
        		kthread_stop(HDMI_task);
        		HDMI_task = 0;
        	}
        }
	return 0;
}

__s32 Hdmi_suspend(void)
{
        if(hdmi_used)
        {
                b_hdmi_suspend = 1;
                pr_info("[HDMI]hdmi suspend\n");
        }
        return 0;
}

__s32 Hdmi_resume(void)
{
        if(hdmi_used)
        {
                b_hdmi_suspend = 0;
                Hdmi_hal_init();
                pr_info("[HDMI]hdmi resume\n");
        }
        return  0; 
}
