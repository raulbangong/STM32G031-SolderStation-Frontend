#ifndef __SYSTICK_H__
#define __SYSTICK_H__
#include <stdint.h>

void SysTick_Init(void);
uint32_t Get_Tick(void); // 获取当前开机了多少毫秒
void Delay_ms_Block(uint32_t ms); // 仅在初始化时使用的阻塞延时

#endif
