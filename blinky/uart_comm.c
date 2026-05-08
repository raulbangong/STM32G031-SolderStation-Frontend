#include "uart_comm.h"
#include "stm32g0xx.h"

// ==========================================
// 1. 极致省内存的环形缓冲区 (Ring Buffer)
// ==========================================
#define RX_BUF_SIZE 64  // 你的内存很小，64字节足够缓冲几个数据包了
#define RX_BUF_MASK (RX_BUF_SIZE - 1) // 必须是 2 的 n 次方，用位与运算代替取余，速度极快！

static uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head = 0; // 中断负责推进
static uint8_t rx_tail = 0;          // 主循环负责推进

// 存储解析出的最新合法数据
static BackendData_t latest_data = {0};

// ==========================================
// 2. 状态机 (FSM) 解析器变量
// ==========================================
typedef enum {
    STATE_WAIT_HEADER,
    STATE_RECV_IRON_H,
    STATE_RECV_IRON_L,
    STATE_RECV_AIR_H,
    STATE_RECV_AIR_L,
    STATE_RECV_STATUS,
    STATE_RECV_CHECKSUM,
    STATE_WAIT_TAIL
} ParserState;

static ParserState fsm_state = STATE_WAIT_HEADER;
static uint8_t calc_checksum = 0;
static uint16_t temp_iron = 0;
static uint16_t temp_air = 0;
static uint8_t temp_status = 0;

// ==========================================
// 3. 硬件初始化 (PA2: TX, PA3: RX, USART2)
// ==========================================
void UART_Comm_Init(uint32_t baudrate) {
    // 1. 开启 GPIOA 和 USART2 的时钟
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;

    // 2. 配置 PA2 和 PA3 为复用功能 (AF1 对应 USART2)
    GPIOA->MODER &= ~(GPIO_MODER_MODE2 | GPIO_MODER_MODE3);
    GPIOA->MODER |= (GPIO_MODER_MODE2_1 | GPIO_MODER_MODE3_1); // 10: Alternate function
    
    // 配置 AF 寄存器 (AFR[0] 对应 Pin0~Pin7) -> 设置为 AF1
    GPIOA->AFR[0] &= ~((0xF << GPIO_AFRL_AFSEL2_Pos) | (0xF << GPIO_AFRL_AFSEL3_Pos));
    GPIOA->AFR[0] |=  ((1U << GPIO_AFRL_AFSEL2_Pos) | (1U << GPIO_AFRL_AFSEL3_Pos));

    // 3. 配置 USART2 寄存器
    USART2->CR1 = 0; // 先清零
    
    // 波特率计算：假设目前系统时钟是 16MHz (内部 HSI 默认值)
    // 如果你后来配了 64MHz，这里要换成 SystemCoreClock
    USART2->BRR = 16000000 / baudrate; 

    // 开启接收中断(RXNEIE), 发送使能(TE), 接收使能(RE)
    USART2->CR1 |= USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TE | USART_CR1_RE;

    // 4. 在 NVIC (嵌套向量中断控制器) 中开启中断，并设置优先级
    NVIC_SetPriority(USART2_IRQn, 1); // 优先级设高一点，别丢包
    NVIC_EnableIRQ(USART2_IRQn);

    // 5. 使能串口模块
    USART2->CR1 |= USART_CR1_UE;
}

// ==========================================
// 4. 最底层的神：串口中断服务函数
// (只做一件事：把数据塞进环形队列，绝不拖泥带水)
// ==========================================
void USART2_IRQHandler(void) {
    // 检查是否是“接收寄存器非空”中断
    if (USART2->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t data = (uint8_t)(USART2->RDR & 0xFF); // 读取数据，自动清中断标志
        
        // 算一下下一个头指针的位置
        uint8_t next_head = (rx_head + 1) & RX_BUF_MASK;
        
        // 如果追上了尾巴（缓冲区满了），就忍痛丢弃这个字节
        if (next_head != rx_tail) {
            rx_buf[rx_head] = data;
            rx_head = next_head;
        }
    }
}

// ==========================================
// 5. 协议解析器 (状态机模式，在 main 的 while 中调用)
// ==========================================
void UART_Comm_Process(void) {
    // 只要环形队列里有肉（头不等于尾），就一直吃
    while (rx_tail != rx_head) {
        uint8_t byte = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & RX_BUF_MASK; // 尾指针往前挪

        // 状态机解析流水线
        switch (fsm_state) {
            case STATE_WAIT_HEADER:
                if (byte == 0x5A) {
                    calc_checksum = byte; // 重置校验和
                    fsm_state = STATE_RECV_IRON_H;
                }
                break;

            case STATE_RECV_IRON_H:
                temp_iron = (byte << 8);
                calc_checksum += byte;
                fsm_state = STATE_RECV_IRON_L;
                break;

            case STATE_RECV_IRON_L:
                temp_iron |= byte;
                calc_checksum += byte;
                fsm_state = STATE_RECV_AIR_H;
                break;

            case STATE_RECV_AIR_H:
                temp_air = (byte << 8);
                calc_checksum += byte;
                fsm_state = STATE_RECV_AIR_L;
                break;

            case STATE_RECV_AIR_L:
                temp_air |= byte;
                calc_checksum += byte;
                fsm_state = STATE_RECV_STATUS;
                break;

            case STATE_RECV_STATUS:
                temp_status = byte;
                calc_checksum += byte;
                fsm_state = STATE_RECV_CHECKSUM;
                break;

            case STATE_RECV_CHECKSUM:
                if (byte == calc_checksum) {
                    fsm_state = STATE_WAIT_TAIL;
                } else {
                    fsm_state = STATE_WAIT_HEADER; // 校验失败，从头再来
                }
                break;

            case STATE_WAIT_TAIL:
                if (byte == 0xA5) {
                    // 完美接收到一帧！更新给 UI 层用的数据结构
                    latest_data.iron_temp = temp_iron;
                    latest_data.air_temp = temp_air;
                    latest_data.status = temp_status;
                    latest_data.is_updated = true;
                }
                // 不管尾部对不对，这一轮都结束了，重新等头
                fsm_state = STATE_WAIT_HEADER; 
                break;
        }
    }
}

// ==========================================
// 6. UI 层获取数据的安全接口
// ==========================================
void UART_Comm_GetData(BackendData_t *out_data) {
    // 关闭全局中断，防止在复制数据的瞬间被新中断打断（引发数据撕裂）
    __disable_irq(); 
    *out_data = latest_data;
    latest_data.is_updated = false; // 取走后清除标记
    __enable_irq();
}

// ==========================================
// 7. 发送控制指令给后端底板
// ==========================================
void UART_Comm_SendCmd(uint8_t cmd, uint16_t val) {
    uint8_t tx_frame[6];
    tx_frame[0] = 0x5A; // 头
    tx_frame[1] = cmd;
    tx_frame[2] = (val >> 8) & 0xFF;
    tx_frame[3] = val & 0xFF;
    
    uint8_t sum = 0;
    for(int i=0; i<4; i++) sum += tx_frame[i];
    tx_frame[4] = sum;
    tx_frame[5] = 0xA5; // 尾

    // 纯裸机轮询发送
    for(int i=0; i<6; i++) {
        while ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0);
        USART2->TDR = tx_frame[i];
    }
    while ((USART2->ISR & USART_ISR_TC) == 0); // 等待发送彻底完成
}
