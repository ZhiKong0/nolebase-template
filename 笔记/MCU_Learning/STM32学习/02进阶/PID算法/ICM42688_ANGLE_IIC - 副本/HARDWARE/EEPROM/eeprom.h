#ifndef __EEPROM_H
#define __EEPROM_H

#include "stm32f10x.h"

/*使用STM32内部Flash做EEPROM
API
*/
#define  PAGE_Config    (0x08000000 + 61 * 1024) //将配置信息存放在第62页Flash
#define  PAGE_BackConfig    (0x08000000 + 62 * 1024) //将配置信息存放在第62页Flash
struct data_map{
	
	int16_t is_good;   //数据是否有效
	
	
	int16_t dGx_offset;
	int16_t dGy_offset;
	int16_t dGz_offset;
	
	int16_t dAx_offset;
	int16_t dAy_offset;
	
		
	int16_t Acx_offset;
	int16_t Acy_offset;
	int16_t Acz_offset;
	
	int16_t end_is_good;   //数据是否有效
};

extern struct data_map Config;//外部可能调用  在eeprom.c中声明
void Write_config(void);  //写入配置
void Write_backconfig(void);
void load_backconfig(void);
void load_config(void);	  //读取配置

#endif
//------------------End of File----------------------------
