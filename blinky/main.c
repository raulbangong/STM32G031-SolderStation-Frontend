#include "stm32g0xx.h"
#include "st7735.h"
#include "fonts.h"
#include "uart_comm.h"
#include "systick.h"
#include "button.h"
#include <stdio.h> // 去掉 sprintf 后，这里不再需要，但为了格式化温度，我们要自己写一个

// --- 全局控制状态 ---
uint16_t target_iron_temp = 300;
uint16_t real_iron_temp = 25;
uint16_t real_air_temp = 25;
uint16_t last_real_iron = 999; // 用于记录上一次显示的真实温度

// --- 纯手工数字转字符 (不占堆栈) ---
void format_temp(uint16_t temp, char *buf) {
    buf[0] = (temp >= 100) ? (temp / 100) + '0' : ' ';
    buf[1] = (temp >= 10)  ? ((temp / 10) % 10) + '0' : ' ';
    buf[2] = (temp % 10) + '0';
    buf[3] = '\0';
}

// --- 底层硬件初始化 ---
void Hardware_Init(void) {
    SysTick_Init();
    // 屏幕 SPI 引脚初始化 (PA9背光, PB3, PB4, PB5, PB6, PB7)
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
    
    GPIOB->MODER &= ~(GPIO_MODER_MODE4 | GPIO_MODER_MODE6 | GPIO_MODER_MODE7);
    GPIOB->MODER |= (GPIO_MODER_MODE4_0 | GPIO_MODER_MODE6_0 | GPIO_MODER_MODE7_0);
    GPIOB->BSRR = (1U << 4); // CS High

    GPIOB->MODER &= ~(GPIO_MODER_MODE3 | GPIO_MODER_MODE5);
    GPIOB->MODER |= (GPIO_MODER_MODE3_1 | GPIO_MODER_MODE5_1); // AF0

    GPIOA->MODER &= ~(GPIO_MODER_MODE9);
    GPIOA->MODER |= (GPIO_MODER_MODE9_0);
    GPIOA->BSRR = (1U << 9); // 背光亮

    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM; 
    SPI1->CR2 = (7U << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    SPI1->CR1 |= SPI_CR1_SPE; 
}

// --- 局部刷新：只刷设定温度 (极速响应用户) ---
void UI_Update_Target(void) {
    char buf[10];
    format_temp(target_iron_temp, buf);
    ST7735_WriteString(35, 65, buf, Font_7x10, ST7735_GRAY, ST7735_BLACK);
}

// --- 局部刷新：只刷真实温度 (依赖后端发来的数据) ---
void UI_Update_Realtime(void) {
    char buf[10];
    if (real_iron_temp != last_real_iron) {
        format_temp(real_iron_temp, buf);
        ST7735_WriteString(15, 30, buf, Font_11x18, ST7735_GREEN, ST7735_BLACK);
        
        // 功率条
        int bar_w = (real_iron_temp * 60) / 450; 
        ST7735_FillRectangle(5, 55, bar_w, 4, ST7735_RED);
        ST7735_FillRectangle(5 + bar_w, 55, 60 - bar_w, 4, ST7735_BLACK);
        
        last_real_iron = real_iron_temp;
    }
}

int main(void) {
    Hardware_Init();
    Button_Init();
    UART_Comm_Init(115200); // 你上一条发的串口代码

    // 因为 st7735 库里我们把 HAL_Delay 换成了 delay_ms，这里统一一下
    // 你可以全局用 Delay_ms_Block 替换 st7735.c 里的 delay_ms
    ST7735_Init();
    ST7735_FillScreen(ST7735_BLACK);
    
    // 画静态 UI 框架
    ST7735_FillRectangle(79, 0, 2, 80, ST7735_GRAY);
    ST7735_WriteString(5, 5, "IRON", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(5, 65, "SET:", Font_7x10, ST7735_GRAY, ST7735_BLACK);
    
    // 开机初始化显示
    UI_Update_Target();

    uint32_t last_btn_tick = 0;
    BackendData_t backend_data = {0};

    // =====================================
    // 超级异步主循环 (全速狂奔)
    // =====================================
    while(1) {
        // 任务 1：10ms 扫描一次按键 (绝对不阻塞)
        if (Get_Tick() - last_btn_tick >= 10) {
            last_btn_tick = Get_Tick();
            Button_Scan_Task();
            
            // 响应用户输入：立刻刷新 UI 并发送串口指令
            KeyEvent_t event = Button_Get_Event();
            if (event != KEY_NONE) {
                if (event == KEY_PLUS_SHORT && target_iron_temp < 450) {
                    target_iron_temp += 5;
                    UI_Update_Target(); 
                    UART_Comm_SendCmd(0x01, target_iron_temp); // 发给后端
                }
                else if (event == KEY_MINUS_SHORT && target_iron_temp > 50) {
                    target_iron_temp -= 5;
                    UI_Update_Target();
                    UART_Comm_SendCmd(0x01, target_iron_temp); // 发给后端
                }
                else if (event == KEY_MENU_LONG) {
                    // 进入高级设置菜单的逻辑 (待开发)
                }
            }
        }

        // 任务 2：无情吞噬后端 F030 发来的串口数据
        UART_Comm_Process();
        
        // 任务 3：如果有新的一帧真实温度到了，就局部刷新屏幕
        UART_Comm_GetData(&backend_data);
        if (backend_data.is_updated) {
            real_iron_temp = backend_data.iron_temp;
            real_air_temp = backend_data.air_temp;
            UI_Update_Realtime();
        }
    }
}
