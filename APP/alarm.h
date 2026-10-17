#ifndef __ALARM_H__
#define __ALARM_H__

/* alarm.h */
void Alarm_Init(void);   /* 初始显示：红灯灭、绿灯亮（上电默认正常相） */
void Alarm_Tick(void);   /* 100ms 节拍，CtrlTask 循环调用 */

#endif