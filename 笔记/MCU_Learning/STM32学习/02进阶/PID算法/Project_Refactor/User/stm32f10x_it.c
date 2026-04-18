/**
  ******************************************************************************
  * @file    Project/STM32F10x_StdPeriph_Template/stm32f10x_it.c 
  * @author  MCD Application Team
  * @version V3.5.0
  * @date    08-April-2011
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_it.h"
#include "Control_Straight.h"
#include "Control_Track.h"
#include "Timer.h"
#include "stm32f10x_bkp.h"
#include "stm32f10x_pwr.h"

/** @addtogroup STM32F10x_StdPeriph_Template
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define FAULT_TRACE_MAGIC  0xA500u
#define FAULT_TRACE_NONE   0x0000u
#define FAULT_TRACE_HARD   0x0001u
#define FAULT_TRACE_MEM    0x0002u
#define FAULT_TRACE_BUS    0x0003u
#define FAULT_TRACE_USAGE  0x0004u
/* Private variables ---------------------------------------------------------*/
extern ControlStraightSystem_t g_straightSys;
extern ControlTrackSystem_t g_trackSys;
extern volatile uint8_t g_mainSelectedMode;
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
static void fault_trace_store(uint16_t code)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    BKP_WriteBackupRegister(BKP_DR1, (uint16_t)(FAULT_TRACE_MAGIC | (code & 0x00FFu)));
}

static void fault_trace_reset(uint16_t code)
{
    fault_trace_store(code);
    NVIC_SystemReset();
    while (1)
    {
    }
}

uint16_t FaultTrace_GetAndClearCode(void)
{
    uint16_t raw;
    uint16_t out = FAULT_TRACE_NONE;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    raw = BKP_ReadBackupRegister(BKP_DR1);
    if ((raw & 0xFF00u) == FAULT_TRACE_MAGIC) {
        out = (uint16_t)(raw & 0x00FFu);
    }
    BKP_WriteBackupRegister(BKP_DR1, 0u);
    return out;
}

/******************************************************************************/
/*            Cortex-M3 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  fault_trace_reset(FAULT_TRACE_HARD);
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  fault_trace_reset(FAULT_TRACE_MEM);
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  fault_trace_reset(FAULT_TRACE_BUS);
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  fault_trace_reset(FAULT_TRACE_USAGE);
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
}

/******************************************************************************/
/*                 STM32F10x Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f10x_xx.s).                                            */
/******************************************************************************/

/**
  * @brief  This function handles TIM4 interrupt (1ms control tick).
  * @param  None
  * @retval None
  */
void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

        ControlTrack_TimerTickISR(&g_trackSys);
        if (g_mainSelectedMode == 0u) {
            ControlStraight_TimerTickISR(&g_straightSys);
        }
    }
}

/**
  * @}
  */ 


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
