#include "stm32f10x.h"
#include "Delay.h"
#include "SoftIIC.h"
#include "LED.h"
#include "OLED.h"

/* ==================== 主函数 ==================== */
int main(void)
{
    // 初始化各个模块
    SoftIIC_Init();     // 软件 I2C 初始化（PB10/PB11）
    LED_Init();         // LED 初始化（PC13）
    OLED_Init();        // OLED 初始化（使用软件 I2C）
    
    // 演示1：使用封装好的 OLED 库函数显示内容
    OLED_Clear();
    OLED_ShowString(1, 1, "SoftIIC Test");
    OLED_ShowString(2, 1, "Addr: 0x78");
    OLED_ShowString(3, 1, "Status:");
    
    // 演示2：直接使用软件 I2C 底层函数发送命令
    // 发送命令点亮 OLED 屏幕
    uint8_t commands[] = {0x00, 0x8D, 0x14, 0xAF, 0xA5};
    int result = SoftIIC_SendBytes(0x78, commands, 5);
    
    // 显示发送结果
    if (result == 0)
        OLED_ShowString(4, 1, "Send: OK");
    else
        OLED_ShowString(4, 1, "Send: FAIL");
    
    // 演示3：读取 OLED 状态寄存器
    uint8_t status;
    result = SoftIIC_ReceiveBytes(0x78, &status, 1);
    
    // 根据状态控制 LED：第6位(D6)=0表示屏幕点亮
    if (result == 0 && (status & (0x01 << 6)) == 0)
    {
        LED_On(1);   // 点亮 LED（假设 PC13 对应 LED1）
    }
    else
    {
        LED_Off(1);  // 熄灭 LED
    }
    
    // 演示4：使用 OLED 专用封装函数
    // SoftIIC_SendOLEDCommand(0x78, 0xA7);  // 反转显示
    // Delay_ms(1000);
    // SoftIIC_SendOLEDCommand(0x78, 0xA6);  // 正常显示
    
    while(1)
    {
        // 主循环可以添加其他任务
    }
}
