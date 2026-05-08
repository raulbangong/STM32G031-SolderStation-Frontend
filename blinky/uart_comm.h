#ifndef __UART_COMM_H__
#define __UART_COMM_H__

#include <stdint.h>
#include <stdbool.h>

// --- 对外暴露的数据结构 ---
typedef struct {
    uint16_t iron_temp;   // 烙铁当前温度
    uint16_t air_temp;    // 风枪当前温度
    uint8_t  status;      // 状态位（比如：休眠、加热中、报错）
    bool     is_updated;  // 标记是否有新数据到来
} BackendData_t;

// --- 核心 API 声明 ---

// 1. 初始化串口硬件 (引脚配置、波特率、开启中断)
void UART_Comm_Init(uint32_t baudrate);

// 2. 在主循环 while(1) 里不断调用，它会默默吃掉环形缓冲区的数据并解析
void UART_Comm_Process(void);

// 3. UI 层调用这个接口，安全地获取最新温度数据
void UART_Comm_GetData(BackendData_t *out_data);

// 4. (可选) 给底板发送指令，比如设定温度
void UART_Comm_SendCmd(uint8_t cmd, uint16_t val);

#endif // __UART_COMM_H__
