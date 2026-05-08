#include "systick.h"
#include "stm32g0xx.h"

// 全局滴答时钟，每毫秒加 1
static volatile uint32_t uwTick = 0;

void SysTick_Init(void) {
    // 假设系统时钟为内部 16MHz
    // SystemCoreClock / 1000 产生 1ms 的中断
    SysTick_Config(16000000 / 1000); 
}

// SysTick 中断服务函数 (内核自动调用)
void SysTick_Handler(void) {
    uwTick++;
}

uint32_t Get_Tick(void) {
    return uwTick;
}

void Delay_ms_Block(uint32_t ms) {
    uint32_t start = uwTick;
    while ((uwTick - start) < ms);
}
