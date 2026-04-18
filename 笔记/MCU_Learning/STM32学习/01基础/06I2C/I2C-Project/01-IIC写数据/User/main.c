#include "stm32f10x.h"
#include "Delay.h"

void DZ_I2C_Init(void);
void OLED_Test_LED_Init(void);  // OLED测试用的LED初始化函数声明
int I2C_SendBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pData, uint16_t Size);
int I2C_ReceiveBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pBuffer, uint16_t Size);

int main()
{
	DZ_I2C_Init();
	
	// 示例1：发送命令点亮 OLED 屏幕
	uint8_t commands[] = {0x00, 0x8D, 0x14, 0xAF, 0xA5};
	int result = I2C_SendBytes(I2C1, 0x78, commands, 5);
	
	// 示例2：在 OLED 上显示数据
	// 先发送 0x40 表示后续是显示数据，然后发送要显示的字节
	uint8_t display_data[] = {0x40, 0xFF, 0x81, 0x81, 0x81, 0xFF}; // 显示字符"0"的简单图案
	result = I2C_SendBytes(I2C1, 0x78, display_data, 6);
	
	// 可添加 LED 指示发送结果
	// result = 0 表示发送成功，-1 表示寻址失败，-2 表示数据被拒收
	
	while(1)
	{
		
	}
}

/**
 * @brief  I2C1 初始化函数（使用 PB8/PB9 重映射）
 * @note   配置流程：
 *         1. 使能 AFIO 和 GPIOB 时钟
 *         2. 配置 GPIO 为复用开漏模式
 *         3. 配置 AFIO 引脚重映射
 *         4. 使能并复位 I2C1 外设
 *         5. 配置 I2C 工作参数
 */
void DZ_I2C_Init(void)
{
	/* 
	 * 使能外设时钟
	 * AFIO 时钟：引脚重映射功能必需
	 * GPIOB 时钟：I2C1 重映射后使用 PB8、PB9
	 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);      // 开启 AFIO 时钟，用于引脚重映射
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);     // 开启 GPIOB 时钟，I2C 引脚所在端口
	
	/* 
	 * 配置 GPIO 引脚
	 * PB8 = SCL（时钟线）, PB9 = SDA（数据线）
	 * 模式：复用开漏输出（I2C 协议要求，实现线与功能）
	 */
	GPIO_InitTypeDef GPIO_InitStruct;
	
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;        // 选择 PB8 和 PB9 引脚
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF_OD;             // 复用开漏模式：必须使用此模式！
	                                                        // 原因：I2C 采用开漏输出+上拉电阻结构
	                                                        // 实现多设备共用总线的"线与"功能
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;           // 输出速度 50MHz，满足 I2C 时序要求
	GPIO_Init(GPIOB, &GPIO_InitStruct);                      // 将配置写入 GPIOB 寄存器
	
	/* 
	 * 配置 AFIO 引脚重映射
	 * I2C1 默认使用 PB6(SCL)/PB7(SDA)，通过重映射切换到 PB8/PB9
	 * 适用于 PB6/7 被其他功能占用的情况
	 */
	GPIO_PinRemapConfig(GPIO_Remap_I2C1, ENABLE);            // 使能 I2C1 重映射功能
	                                                         // 注：若使用默认引脚 PB6/PB7，无需此行代码
	
	/* 
	 * 使能并复位 I2C1 外设
	 * 先复位再解复位，清除可能的异常状态
	 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);     // 开启 I2C1 时钟（I2C1 在 APB1 总线上）
	
	RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);      // 复位 I2C1 外设：清除所有寄存器状态
	RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);     // 解除复位：外设进入可配置状态
	
	/* 
	 * 配置 I2C 工作参数
	 * 设置通信速率、工作模式等关键参数
	 */
	I2C_InitTypeDef I2C_InitStruct;
	
	I2C_InitStruct.I2C_ClockSpeed = 400000;                   // 设置时钟频率 400kHz（快速模式）
	                                                        // 标准模式：100kHz，快速模式：400kHz
	I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;                   // I2C 标准模式（非 SMBus 模式）
	I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;           // 快速模式下 SCL 占空比 2:1
	                                                        // 即高电平:低电平 = 2:1
	
	I2C_Init(I2C1, &I2C_InitStruct);                          // 将配置写入 I2C1 寄存器，完成初始化
}

/**
 * @brief  OLED测试用的LED初始化（PC13）
 * @note   PC13 连接板载LED，低电平点亮，高电平熄灭
 *         用于指示OLED屏幕状态：亮=屏幕点亮，灭=屏幕熄灭
 */
void OLED_Test_LED_Init(void)
{
	// 开启 GPIOC 时钟
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;           // PC13
	GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;     // 推挽输出
	GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;     // 低速 2MHz
	
	GPIO_Init(GPIOC, &GPIO_InitStruct);
	
	// 默认熄灭LED（高电平）
	GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

/**
 * @brief  I2C 主机发送数据（阻塞式）
 * @param  I2Cx: I2C 外设（I2C1 或 I2C2）
 * @param  Addr: 从机地址（8位格式，已包含读写位，bit0=0 表示写）
 * @param  pData: 要发送的数据缓冲区
 * @param  Size: 数据长度（字节数）
 * @retval 0: 发送成功，-1: 寻址失败，-2: 数据拒收
 * @note   通信流程：起始信号 -> 发送地址 -> 等待ACK -> 发送数据 -> 停止信号
 */
int I2C_SendBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pData, uint16_t Size)
{
	/* 阶段1：等待总线空闲 */
	// I2C_FLAG_BUSY 表示总线正在通信中，必须等待空闲才能发起新通信
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY) == SET);	
	
	/* 阶段2：发送起始信号（Start Condition） */
	// 起始信号：SCL=1 时，SDA 从高电平跳变到低电平
	// 通知所有从机，主机即将发起通信
	I2C_GenerateSTART(I2C1, ENABLE);	// 发送起始信号
	// I2C_FLAG_SB（Start Bit）起始位发送完成标志
	// 必须等待起始信号发送完毕，才能进行下一步
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET); 	
	
	/* 阶段3：寻址阶段 - 发送从机地址 */
	// I2C_FLAG_AF（Acknowledge Failure）应答失败标志
	// 清除此标志，避免之前的失败影响当前通信
	I2C_ClearFlag(I2Cx, I2C_FLAG_AF);
	
	// 发送地址：Addr & 0xFE 确保 bit0 为 0（写操作）
	// 地址格式：7位从机地址 + 1位读写标志（0=写，1=读）
	// 例如从机地址 0x3C，则发送 0x78（0x3C << 1）
	I2C_SendData(I2Cx, Addr & 0xFE);
	
	// 等待从机应答，有两种结果：
	// 1. I2C_FLAG_ADDR=1：地址匹配成功，从机已应答
	// 2. I2C_FLAG_AF=1：无应答，从机不存在或地址错误
	while(1)
	{
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == SET) break; 	// 寻址成功，退出等待
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)				// 寻址失败，从机无应答
		{
			I2C_GenerateSTOP(I2Cx, ENABLE);	// 发送停止信号，释放总线
			return -1;							// 返回错误码：寻址失败
		}
	}
	
	/* 阶段4：清除 ADDR 标志 */
	// I2C_FLAG_ADDR（Address sent）地址已发送标志
	// 必须通过读取 SR1 和 SR2 寄存器来清除此标志
	// 这是 STM32 I2C 硬件要求，不清除会导致总线锁定
	I2C_ReadRegister(I2Cx, I2C_Register_SR1);
	I2C_ReadRegister(I2Cx, I2C_Register_SR2);
	
	/* 阶段5：发送数据 */
	for(uint16_t i = 0; i < Size; i++)
	{
		// 等待发送缓冲区空闲
		// I2C_FLAG_TXE（Transmit Empty）发送寄存器空标志
		// 表示可以写入下一个字节
		while(1)
		{
			if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET) // 检测应答失败
			{
				I2C_GenerateSTOP(I2Cx, ENABLE);				// 发送停止信号
				return -2;								// 返回错误码：数据拒收
			}
			if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_TXE) == SET)	// 发送缓冲区已空
			{
				break;	// 可以发送下一个字节
			}
		}
		// 将数据写入数据寄存器，硬件自动发送
		I2C_SendData(I2Cx, pData[i]);
	}
	
	/* 阶段6：等待发送完成 */
	// I2C_FLAG_BTF（Byte Transfer Finished）字节传输完成标志
	// 表示最后一个字节已发送且收到应答
	while(1)
	{
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)
		{
			I2C_GenerateSTOP(I2Cx, ENABLE);					// 发送停止信号
			return -2;									// 返回错误码：数据拒收
		}
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == SET)	// 字节传输完成
		{
			break;	// 所有数据发送完毕
		}
		// 发送停止信号，结束本次通信
		// 停止信号：SCL=1 时，SDA 从低电平跳变到高电平
		I2C_GenerateSTOP(I2Cx, ENABLE);
	}
	
	return 0;	// 发送成功
}


/**
 * @brief  I2C 主机接收数据（阻塞式）
 * @param  I2Cx: I2C 外设（I2C1 或 I2C2）
 * @param  Addr: 从机地址（8位格式，已包含读写位，bit0=1 表示读）
 * @param  pBuffer: 接收数据缓冲区
 * @param  Size: 要接收的数据长度（字节数）
 * @retval 0: 接收成功，-1: 寻址失败
 * @note   通信流程：起始信号 -> 发送地址(R/W=1) -> 等待ACK -> 接收数据 -> 发送NACK -> 停止信号
 *         注意：接收时主机在最后一个字节前发送 NACK，告知从机停止发送
 */
int I2C_ReceiveBytes(I2C_TypeDef *I2Cx, uint8_t Addr, uint8_t *pBuffer, uint16_t Size)
{
	uint16_t i;
	
	/* 阶段1：发送起始信号 */
	// 起始信号：SCL=1 时，SDA 从高电平跳变到低电平
	I2C_GenerateSTART(I2Cx, ENABLE);
	// 等待起始信号发送完成（SB=Start Bit 标志）
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_SB) == RESET);
	
	/* 阶段2：寻址阶段 - 发送从机地址（读操作） */
	// 清除 AF（Acknowledge Failure）标志，避免之前通信影响
	I2C_ClearFlag(I2Cx, I2C_FLAG_AF);
	
	// 发送地址：Addr | 0x01 确保 bit0 为 1（读操作）
	// 地址格式：7位从机地址 + 1位读写标志（0=写，1=读）
	I2C_SendData(I2Cx, Addr | 0x01);
	
	// 等待从机应答
	while(1)
	{
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_AF) == SET)	// 寻址失败，从机无应答
		{
			I2C_GenerateSTOP(I2Cx, ENABLE);	// 发送停止信号，释放总线
			return -1;						// 返回错误码：寻址失败
		}
		
		if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDR) == SET)	// 寻址成功
		{
			break;
		}
	}
	
	/* 阶段3：接收数据 */
	// 根据接收字节数，使用不同的接收策略（STM32 I2C硬件要求）
	
	if(Size == 1)
	{
		/* 单字节接收：必须在清除ADDR前禁用ACK和发送STOP */
		I2C_AcknowledgeConfig(I2Cx, DISABLE);      // ① 先禁用ACK（准备回NACK，而不是ACK）
		I2C_GenerateSTOP(I2Cx, ENABLE);              // ② 立即发送STOP信号（通知从机停止发送）
		                                              //    注：单字节时必须在接收前设好STOP，否则从机可能多发数据
		I2C_ReadRegister(I2Cx, I2C_Register_SR1);      // ③ 清除ADDR标志（读取SR1）
		I2C_ReadRegister(I2Cx, I2C_Register_SR2);     //    继续读SR2完成清除，同时触发数据接收
		                                              //    注：此时从机收到地址+读命令，开始发送第1字节
		                                              //    由于已设NACK，从机发送完1字节后收到NACK自动停止
		
		while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);  // ④ 等待RXNE=1（数据已存入DR寄存器）
		pBuffer[0] = I2C_ReceiveData(I2Cx);          // ⑤ 读取唯一的1个字节
	}
	else if(Size == 2)
	{
		/* 双字节接收：使用POS=1使NACK只作用于第二个字节 */
		I2C_AcknowledgeConfig(I2Cx, ENABLE);         // ① 先使能ACK（第1字节要回ACK）
		I2C_NACKPositionConfig(I2Cx, I2C_NACKPosition_Next);  // ② 设POS=1，NACK只对"下一个"字节生效（即第2字节）
		                                              //    注：POS位控制NACK作用于当前字节还是下一个字节
		I2C_ReadRegister(I2Cx, I2C_Register_SR1);      // ③ 清除ADDR标志（开始接收第1字节）
		I2C_ReadRegister(I2Cx, I2C_Register_SR2);      //    读SR2完成清除，第1字节开始传输
		
		while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTF) == RESET);  // ④ 等待BTF=1（Byte Transfer Finished）
		                                              //    注：BTF=1表示DR和移位寄存器都有数据（2字节都到了）
		                                              //    此时第1字节在DR，第2字节在移位寄存器，从机暂停等待应答
		
		I2C_AcknowledgeConfig(I2Cx, DISABLE);         // ⑤ 禁用ACK（对第2字节回NACK）
		I2C_GenerateSTOP(I2Cx, ENABLE);                // ⑥ 发送STOP（结束通信）
		                                              //    注：此时从机正准备发第2字节的ACK位，收到NACK后停止
		
		pBuffer[0] = I2C_ReceiveData(I2Cx);          // ⑦ 读第1字节（DR变空，第2字节从移位寄存器移入DR）
		while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);  // ⑧ 等待第2字节移入DR（RXNE=1）
		pBuffer[1] = I2C_ReceiveData(I2Cx);          // ⑨ 读第2字节（最后一个字节）
	}
	else
	{
		/* 3字节及以上：标准接收流程 */
		I2C_ReadRegister(I2Cx, I2C_Register_SR1);      // ① 清除ADDR标志（开始接收第1字节）
		I2C_ReadRegister(I2Cx, I2C_Register_SR2);      //    读SR2完成清除，从机开始发送数据
		
		// 接收前 Size-1 个字节（发ACK）
		for(i = 0; i < Size - 1; i++)
		{
			while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);  // ② 等待当前字节到达（RXNE=1）
			pBuffer[i] = I2C_ReceiveData(I2Cx);          // ③ 读取数据（DR变空，硬件自动准备接收下一字节）
			I2C_AcknowledgeConfig(I2Cx, ENABLE);         // ④ 发送ACK（表示"收到了，请继续发下一个"）
			                                              //    注：ACK在当前字节传输的9th时钟位发出
		}
		
		// 最后一个字节：发NACK + STOP，然后读取
		I2C_AcknowledgeConfig(I2Cx, DISABLE);         // ⑤ 禁用ACK（准备回NACK）
		I2C_GenerateSTOP(I2Cx, ENABLE);                // ⑥ 发送STOP（通知从机停止发送）
		                                              //    注：STOP在总线空闲时生效，当前字节传输完成后才生效
		while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_RXNE) == RESET);  // ⑦ 等待最后一个字节到达
		pBuffer[Size - 1] = I2C_ReceiveData(I2Cx);     // ⑧ 读取最后一个字节（同时从机收到NACK）
	}
	
	return 0;
}
