#ifndef ECU_MSG_H
#define ECU_MSG_H

#include <stdint.h>

#define MSG_STM_IDLE     0
#define MSG_STM_LENGTH   1
#define MSG_STM_DATA     2

#define MSG_STATUS_NONE  0
#define MSG_STATUS_OK    1
#define MSG_STATUS_ERR   2

#define MAIN_STM_NONE       0
#define MAIN_STM_BREAK      1
#define MAIN_STM_WAKEUP     2
#define MAIN_STM_INIT       3
#define MAIN_STM_RUN        4
#define MAIN_STM_REQ_11     5

#define MAIN_REASON_NONE    0
#define MAIN_REASON_RX      1
#define MAIN_REASON_INIT    2
#define MAIN_REASON_BREAK   3

#define MAIN_WATCHDOG_MAX   5

// Functions between main.c and ecu_msg.c

extern void write_upstream(const char *msg, int n);
extern void write_downstream(const unsigned char *msg, int n);
extern void break_downstream(void);
extern void break_hard_downstream(int state);
extern void flush_downstream(void);
extern void delay_downstream_ms(uint32_t ms);

extern int do_main_stm(int reason, unsigned char rx);

#endif

