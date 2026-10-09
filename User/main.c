#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
//#include "BlueTooth.h"   // 蓝牙已停用, USART3 改接 WiFi(ESP32)
#include "Servo.h"
#include "PetAction.h"
#include "Face_Config.h"
#include "PWM.h"
#include "PowerDetection.h"
#include "Variable.h"
#include "Led_Breathing.h"
#include "Timer.h"
#include "Voice.h"
#include "WiFi.h"

/***************************************************************************************
  * 本程序由博主我Sngels_wyh创建并免费开源共享
  * 你可以任意查看、使用和修改，并应用到自己的项目之中
  *
  * 程序名称：				基于STM32F103C8T6单片机的桌面宠物小项目
  * 此程序更新时间：			2026.3.23
  *
  * 主要在立创开源平台、CSDN网站、哔哩哔哩、抖音分享自己的作品。
	*	相关的资料可以去立创开源硬件平台网站上搜索,作品附件放置更新网盘，如有更新可方便快速查看
	* 地址:https://oshwhub.com/sngelswyh/stm32-smart-desktop-pet
  ***************************************************************************************/

int main(void)
{
	GetBattery_Init();//电量检测初始化
	Servo_Init();//舵机初始化
	OLED_Init();//OLED初始化
	Timer_Init();//定时器初始化
	//BlueTooth_Init();//蓝牙初始化 (已停用, USART3 改接 WiFi)
	Voice_Init();//语音初始化
	WiFi_Init();//WiFi串口初始化 (USART3, H11接口接ESP32)

	OLED_ShowImage(0,0,128,64,Face_eyes);
	OLED_Update();
	while(1)
	{
		/*WiFi数据处理*/
		WiFi_Process();

		/*AI模式超时退出 (10秒无活动)*/
		if(AI_Mode && AI_Timeout == 0)
		{
			AI_Mode = 0;
			AI_ResponseReady = 0;
			Face_Mode = 5;
			Action_Mode = 2;
		}

		/*显示表情(电量)*/
		Face_Config();
		/*执行动作*/
		PetAction_Perform();
	}
}

/*每20ms进一次中断*/
void TIM3_IRQHandler(void)
{
	if(TIM_GetITStatus(TIM3,TIM_IT_Update)==SET)
	{
		/*电量检测*/
		GetCur_Power();
		/*呼吸灯*/
		LED_Breathing();

		/*AI超时倒计时 (20ms/tick, 500=10秒)*/
		if(AI_Timeout > 0) AI_Timeout--;

		TIM_ClearITPendingBit(TIM3,TIM_IT_Update);
	}
}
