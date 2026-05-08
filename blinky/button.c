#include "button.h"
#include "stm32g0xx.h"

// 读取引脚状态 (低电平有效，假设外部有上拉或内部开启上拉)
#define READ_KEY_PLUS  ((GPIOC->IDR & (1U << 14)) == 0)
#define READ_KEY_MINUS ((GPIOB->IDR & (1U << 8))  == 0)
#define READ_KEY_MENU  ((GPIOA->IDR & (1U << 5))  == 0)

static KeyEvent_t current_event = KEY_NONE;

void Button_Init(void) {
    // 开启时钟 PA, PB, PC
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN;

    // 配置 PA5(Menu), PB8(Minus), PC14(Plus) 为输入模式 (00)
    GPIOA->MODER &= ~(3U << (5 * 2));
    GPIOB->MODER &= ~(3U << (8 * 2));
    GPIOC->MODER &= ~(3U << (14 * 2));

    // 开启内部上拉电阻 (01)
    GPIOA->PUPDR |= (1U << (5 * 2));
    GPIOB->PUPDR |= (1U << (8 * 2));
    GPIOC->PUPDR |= (1U << (14 * 2));
}

// 非阻塞按键扫描状态机 (建议每 10ms 调用一次)
void Button_Scan_Task(void) {
    static uint16_t menu_press_time = 0;
    static uint8_t plus_pressed = 0;
    static uint8_t minus_pressed = 0;

    // --- 扫描 Menu 键 (支持短按和长按) ---
    if (READ_KEY_MENU) {
        menu_press_time++;
        if (menu_press_time == 100) { // 按下持续 1000ms (100 * 10ms)
            current_event = KEY_MENU_LONG;
        }
    } else {
        if (menu_press_time > 3 && menu_press_time < 100) { // 大于 30ms 防抖，小于长按
            current_event = KEY_MENU_SHORT;
        }
        menu_press_time = 0;
    }

    // --- 扫描 Plus 键 (仅短按) ---
    if (READ_KEY_PLUS) {
        plus_pressed++;
    } else {
        if (plus_pressed > 3) current_event = KEY_PLUS_SHORT;
        plus_pressed = 0;
    }

    // --- 扫描 Minus 键 (仅短按) ---
    if (READ_KEY_MINUS) {
        minus_pressed++;
    } else {
        if (minus_pressed > 3) current_event = KEY_MINUS_SHORT;
        minus_pressed = 0;
    }
}

KeyEvent_t Button_Get_Event(void) {
    KeyEvent_t temp = current_event;
    current_event = KEY_NONE; // 读取后清空
    return temp;
}
