#ifndef __BUTTON_H__
#define __BUTTON_H__
#include <stdint.h>

// 定义按键事件
typedef enum {
    KEY_NONE = 0,
    KEY_PLUS_SHORT,
    KEY_MINUS_SHORT,
    KEY_MENU_SHORT,
    KEY_MENU_LONG
} KeyEvent_t;

void Button_Init(void);
void Button_Scan_Task(void); // 放在 main 的 10ms 任务里
KeyEvent_t Button_Get_Event(void); // 获取按键动作

#endif
