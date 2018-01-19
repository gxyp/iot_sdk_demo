/* Copyright Statement:
 *
 * (C) 2005-2016  MediaTek Inc. All rights reserved.
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. ("MediaTek") and/or its licensors.
 * Without the prior written permission of MediaTek and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 * You may only use, reproduce, modify, or distribute (as applicable) MediaTek Software
 * if you have agreed to and been bound by the applicable license agreement with
 * MediaTek ("License Agreement") and been granted explicit permission to do so within
 * the License Agreement ("Permitted User").  If you are not a Permitted User,
 * please cease any access or use of MediaTek Software immediately.
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT MEDIATEK SOFTWARE RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES
 * ARE PROVIDED TO RECEIVER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 */

#include "stdint.h"
#include "main_screen.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp_ctp.h"
#include "task_def.h"

#include "bsp_lcd.h"
#include "mt25x3_hdk_backlight.h"
#include "hal_display_pwm.h"
#include "hal_display_pwm_internal.h"
#include "timers.h" 

#include "bend_config.h"  // add by gaochao

//add by chenchen // change by gaochao
#include "sct_key_event.h"
#include "hal_keypad.h"

#define UI_TASK_QUEUE_SIZE 20
#define UI_TASK_NAME "UI_DEMO"
#define UI_TASK_STACK_SIZE 4800
#define UI_TASK_PRIORITY 3

typedef struct ui_task_message_struct {
    int message_id;
    int param1;
    void* param2;
} ui_task_message_struct_t;

extern TimeOut_Time_struct_t g_TimeOut_Time;

static struct {
    QueueHandle_t event_queue;
    touch_event_proc_func touch_event_callback_f;
	//add by chenchen
	keypad_event_proc_func keypad_event_callback_f;
//	powerkey_event_proc_func powerkey_event_callback_f;
    bool Is_Need_Back_wf_Screen;
    void* user_data;
} ui_task_cntx;

TimerHandle_t vbacklightTimer = NULL;
TimerHandle_t vbackwfScreenTimer = NULL;


static int32_t ui_send_event_from_isr(message_id_enum event_id, int32_t param1, void* param2);

log_create_module(GRAPHIC_TAG, PRINT_LEVEL_INFO);

//add by chenchen start 
#ifdef MTK_KEYPAD_ENABLE

void demo_ui_register_keypad_event_callback(keypad_event_proc_func proc_func, bool needBackWF, void* user_data)
{
    GRAPHICLOG("demo_ui_register_keypad_event_callback, proc_func:%x", proc_func);
    ui_task_cntx.Is_Need_Back_wf_Screen = needBackWF;
    ui_task_cntx.keypad_event_callback_f = proc_func;
    ui_task_cntx.user_data = user_data;

    if (needBackWF) {
        backwfScreen_timer_init(15);
    } else {
        backwfScreen_timer_stop();
    }
}

/*
void user_keypad_callback (void *user_data)
{
     hal_keypad_event_t keypad_event;
     hal_keypad_get_key(&keypad_event);
}
*/
// callback function registered inside sct_key module
void demo_ui_keypad_callback_func(sct_key_event_t event, uint8_t key_data, void *user_data)
{
    GRAPHICLOG("[ui_demo]sct_key_event %d, key_data %d",event,key_data);

    // Enable backlight and backlight_timer then send message to demo ui task
    if ( SCT_KEY_RELEASE == event ) {
        hal_display_pwm_deinit();
        hal_display_pwm_init(HAL_DISPLAY_PWM_CLOCK_26MHZ);
        hal_display_pwm_set_duty(g_TimeOut_Time.idle_BackWF_Time);
        backlight_timer_init(g_TimeOut_Time.backlight_Time);      // backlight timeout setting to 8 second
        ui_send_event(MESSAGE_ID_KEYPAD_EVENT, (int32_t)key_data, NULL);
    }

    // long press power_key to power off
    if ( SCT_KEY_LONG_PRESS_2 == event && DEVICE_KEY_POWER == key_data ) 
    {
        hal_rtc_set_time_notification_period(HAL_RTC_TIME_NOTIFICATION_NONE);
        hal_sleep_manager_enter_power_off_mode();

    }
}

// ui key event functioin and call ui.keypad_callback for each screen
// just follow previous callback design, will redesign later. 
void keypad_event_handle(uint8_t key_data)
{
    hal_keypad_event_t keypad_event;
   
    keypad_event.state = HAL_KEYPAD_KEY_RELEASE;
    keypad_event.key_data = key_data;

    if (ui_task_cntx.Is_Need_Back_wf_Screen) {
        backwfScreen_timer_init(20);
    }
    
    if (ui_task_cntx.keypad_event_callback_f) {
        GRAPHICLOG("gaochao keypad event handle, data:%x", keypad_event.key_data);
        ui_task_cntx.keypad_event_callback_f(&keypad_event, ui_task_cntx.user_data);
   	}
}
#endif

void vbacklightTimerCallback( TimerHandle_t xTimer )
{
//    bsp_backlight_deinit();
    hal_display_pwm_deinit();
    hal_display_pwm_init(HAL_DISPLAY_PWM_CLOCK_26MHZ);
    hal_display_pwm_set_duty(0);

    backlight_timer_stop();
}

void backlight_timer_stop(void)
{
    if (vbacklightTimer && (xTimerIsTimerActive(vbacklightTimer) != pdFALSE)){
        xTimerStop(vbacklightTimer, 0);
    }
}

void backlight_timer_init(uint32_t time)
{
    if (vbacklightTimer && (xTimerIsTimerActive(vbacklightTimer) != pdFALSE)) {
        xTimerStop(vbacklightTimer, 0);
    } else {
        vbacklightTimer = xTimerCreate( "vbacklightTimer",           // Just a text name, not used by the kernel.
                                      ( time*1000 / portTICK_PERIOD_MS), // The timer period in ticks.
                                      pdFALSE,                    // The timer is a one-shot timer.
                                      0,                          // The id is not used by the callback so can take any value.
                                      vbacklightTimerCallback     // The callback function that switches the LCD back-light off.
                                   );
    }
    xTimerStart(vbacklightTimer, 0);
}

void vbackwfScreenTimerCallback( TimerHandle_t xTimer )
{
// Should send message to ui task for back wf screen
    ui_send_event(MESSAGE_ID_BACKWF_EVENT, 0, NULL);
    backwfScreen_timer_stop();
}

void backwfScreen_timer_stop(void)
{
    if (vbackwfScreenTimer && (xTimerIsTimerActive(vbackwfScreenTimer) != pdFALSE)){
        xTimerStop(vbackwfScreenTimer, 0);
    }
}

void backwfScreen_timer_init(uint32_t time)
{
    if (vbackwfScreenTimer && (xTimerIsTimerActive(vbackwfScreenTimer) != pdFALSE)) {
        xTimerStop(vbackwfScreenTimer, 0);
    } else {
        vbackwfScreenTimer = xTimerCreate( "vbackwfScreenTimer",           // Just a text name, not used by the kernel.
                                      ( time*1000 / portTICK_PERIOD_MS), // The timer period in ticks.
                                      pdFALSE,                    // The timer is a one-shot timer.
                                      0,                          // The id is not used by the callback so can take any value.
                                      vbackwfScreenTimerCallback     // The callback function that switches the LCD back-light off.
                                   );
    }
    xTimerStart(vbackwfScreenTimer, 0);
}


#if 0
/*
void demo_ui_register_powerkey_event_callback(powerkey_event_proc_func proc_func, void* user_data)
{
    GRAPHICLOG("demo_ui_register_powerkey_event_callback, proc_func:%x", proc_func);
    ui_task_cntx.powerkey_event_callback_f = proc_func;
    ui_task_cntx.user_data = user_data;
}

static void demo_ui_powerkey_callback_func(void* param)
{
    ui_send_event_from_isr(MESSAGE_ID_POWERKEY_EVENT, 0, NULL);

}


static void powerkey_event_handle()
{
    hal_keypad_status_t ret;
    hal_keypad_powerkey_event_t powerkey_event;

	GRAPHICLOG("powerkey_event_handle");

    ret = hal_keypad_powerkey_get_key(&powerkey_event);
    GRAPHICLOG("powerkey_get_event_data ret:%d", ret);
    while (ret == HAL_KEYPAD_STATUS_OK) {
        ret = hal_keypad_powerkey_get_key(&powerkey_event);
        if (ui_task_cntx.powerkey_event_callback_f) {
        GRAPHICLOG("chenchen powerkey event handle, data:%d", powerkey_event.key_data);
        ui_task_cntx.powerkey_event_callback_f(&powerkey_event, ui_task_cntx.user_data);
    	}
    }

}
*/
#endif
//chenchen end

/*****************************************************************************
 * FUNCTION
 *  demo_ui_register_touch_event_callback
 * DESCRIPTION
 *  register touch event callback
 * PARAMETERS
 *  proc_func       [in]
 *  user_data       [in]
 * RETURNS
 *  void
 *****************************************************************************/
void demo_ui_register_touch_event_callback(touch_event_proc_func proc_func, void* user_data)
{
    GRAPHICLOG("demo_ui_register_touch_event_callback, proc_func:%x", proc_func);
    ui_task_cntx.touch_event_callback_f = proc_func;
    ui_task_cntx.user_data = user_data;
}

// this option is used for touch panel, This project support touch panel.
// bellowing code used for process touch event which recieved from touch panel hardware.
#ifdef MTK_CTP_ENABLE
/*****************************************************************************
 * FUNCTION
 *  demo_ui_ctp_callback_func
 * DESCRIPTION
 *  CTP callback function
 * PARAMETERS
 *  param       [in]
 * RETURNS
 *  void
 *****************************************************************************/
static void demo_ui_ctp_callback_func(void* param)
{
    ui_send_event_from_isr(MESSAGE_ID_PEN_EVENT, 0, NULL);
}

/*****************************************************************************
 * FUNCTION
 *  demo_ui_process_sigle_event
 * DESCRIPTION
 *  Process ctp event, support single event only
 * PARAMETERS
 *  event       [in]
 * RETURNS
 *  void
 *****************************************************************************/
static void demo_ui_process_sigle_event(bsp_ctp_multiple_event_t* event)
{
    // support single touch currently.
    bsp_ctp_event_status_t hit_type;
    int32_t i = 0;
    static touch_event_struct_t pre_event = {{0,0},TOUCH_EVENT_MAX};

    GRAPHICLOG("process single event, model:%d", event->model);
    if (event->model <= 0) {
        return;
    }

    if (pre_event.type == TOUCH_EVENT_MAX || pre_event.type == TOUCH_EVENT_UP || pre_event.type == TOUCH_EVENT_ABORT) {
        // skip all up & abort point
        hit_type = CTP_PEN_DOWN;
    } else {
        // skip all down & abort event
        hit_type = CTP_PEN_UP;
    }

    while (i < event->model) {
         GRAPHICLOG("[point] event = %d, piont[0].x = %d, piont[0].y = %d\r\n",
                    event->points[i].event, 	\
                    event->points[i].x,		\
                    event->points[i].y);
         if (event->points[i].event == hit_type) {
             break;
         }
         i++;
    }
    if (i >= event->model) {
        GRAPHICLOG("no valid point, pre event:%d", pre_event.type);
        return;
    }

    pre_event.position.x = event->points[i].x;
    pre_event.position.y = event->points[i].y;
    pre_event.type = (touch_event_enum) event->points[i].event;

    if (ui_task_cntx.touch_event_callback_f) {
        GRAPHICLOG("callback app, type:%d,[%d:%d]", pre_event.type, pre_event.position.x, pre_event.position.y);
        ui_task_cntx.touch_event_callback_f(&pre_event, ui_task_cntx.user_data);
    }
}

/*****************************************************************************
 * FUNCTION
 *  pen_event_handle
 * DESCRIPTION
 *  Process ctp event, support single event only
 * PARAMETERS
 *  void
 * RETURNS
 *  void
 *****************************************************************************/
static void pen_event_handle()
{
    bsp_ctp_status_t ret;
    bsp_ctp_multiple_event_t ctp_event;

    // get pen event
    GRAPHICLOG("pen_event_handle");

    ret = bsp_ctp_get_event_data(&ctp_event);
    GRAPHICLOG("ctp_get_event_data ret:%d", ret);
    while (ret == BSP_CTP_OK) {
        ret = bsp_ctp_get_event_data(&ctp_event);
        demo_ui_process_sigle_event(&ctp_event);
    }
    if (ret == BSP_CTP_EVENT_EMPTY) {
        //do nothing
    }

}
#endif

/*****************************************************************************
 * FUNCTION
 *  ui_task_msg_handler
 * DESCRIPTION
 *  Process message in queue
 * PARAMETERS
 *  message         [in]
 * RETURNS
 *  void
 *****************************************************************************/
static void ui_task_msg_handler(ui_task_message_struct_t *message)
{
    if (!message) {
        return;
    }
    switch (message->message_id) {
        
        // this option is used for touch panel, This project support touch panel.
        // bellowing code used for process touch event which recieved from touch panel hardware.
#ifdef MTK_CTP_ENABLE
        case MESSAGE_ID_PEN_EVENT:
            pen_event_handle();
            break;
#endif
#ifdef MTK_KEYPAD_ENABLE
		case MESSAGE_ID_KEYPAD_EVENT:
			keypad_event_handle( (uint8_t)message->param1);
			break;
#endif
        case MESSAGE_ID_BACKWF_EVENT:
            backwfScreen_timer_stop();
            wf_app_task_enable_show();
            break;

        default:
            common_event_handler((message_id_enum) message->message_id, (int32_t) message->param1, (void*) message->param2);
            break;
                
    }
}

/*****************************************************************************
 * FUNCTION
 *  ui_send_event
 * DESCRIPTION
 *  Send message to UI task
 * PARAMETERS
 *  event_id        [in]
 *  param1          [in]
 *  param2          [in]
 * RETURNS
 *  int32_t
 *****************************************************************************/
int32_t ui_send_event(message_id_enum event_id, int32_t param1, void* param2)
{
    ui_task_message_struct_t message;
    message.message_id = event_id;
    message.param1 = param1;
    message.param2 = param2;
    
    return xQueueSend(ui_task_cntx.event_queue, &message, 10);
}

/*****************************************************************************
 * FUNCTION
 *  ui_send_event_from_isr
 * DESCRIPTION
 *  Send message to UI task
 * PARAMETERS
 *  event_id        [in]
 *  param1          [in]
 *  param2          [in]
 * RETURNS
 *  int32_t
 *****************************************************************************/
int32_t ui_send_event_from_isr(message_id_enum event_id, int32_t param1, void* param2)
{
    BaseType_t xHigherPriorityTaskWoken;
    ui_task_message_struct_t message;
    message.message_id = event_id;
    message.param1 = param1;
    message.param2 = param2;
    
    return xQueueSendFromISR(ui_task_cntx.event_queue, &message, &xHigherPriorityTaskWoken);
}

/*****************************************************************************
 * FUNCTION
 *  ui_task_main
 * DESCRIPTION
 *  Task mail loop
 * PARAMETERS
 *  arg        [in]
 * RETURNS
 *  void
 *****************************************************************************/
void ui_task_main(void*arg)
{
    ui_task_message_struct_t queue_item;
    // this option is used for touch panel, This project support touch panel.
    // bellowing code used for init touch panel hardware.
#ifdef MTK_CTP_ENABLE
    bsp_ctp_status_t ret;
 
    ret = bsp_ctp_init();
    GRAPHICLOG("ctp init, ret:%d", ret);
    ret = bsp_ctp_register_callback(demo_ui_ctp_callback_func, NULL);
    GRAPHICLOG("ctp register callback, ret:%d", ret);
#endif
//add by chenchen  // change by gaochao
#ifdef MTK_KEYPAD_ENABLE
    bool status;
    status = sct_key_event_init();
    if (status != true) {
        GRAPHICLOG("[ui_demo][sct_Keypad] sct key init fail, status:%d", status);
    }

    status = sct_key_register_callback(demo_ui_keypad_callback_func, NULL);
    if (status != true) {
        GRAPHICLOG("[ui_demo][sct_Keypad] register callback fail, status:%d", status);
    }
#endif

#if 0 //def MTK_POWERKEY_ENABLE
	hal_keypad_status_t pk;
	pk = keypad_custom_powerkey_init();
	GRAPHICLOG("powerkey init, ret:%d", st);
	pk = hal_keypad_powerkey_register_callback(demo_ui_powerkey_callback_func,NULL);
	GRAPHICLOG("powerkey register callback, ret:%d", st);
#endif

    arg = arg;
    ui_task_cntx.event_queue = xQueueCreate(UI_TASK_QUEUE_SIZE , sizeof( ui_task_message_struct_t ) );
    GRAPHICLOG("ui_task_main");
    backlight_timer_init(g_TimeOut_Time.poweron_BackWF_Time);
    show_main_screen();
    while (1) {
        if (xQueueReceive(ui_task_cntx.event_queue, &queue_item, portMAX_DELAY)) {
            ui_task_msg_handler(&queue_item);
        }
    }
}

