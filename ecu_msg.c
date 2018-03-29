#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ecu_msg.h"

static const unsigned char REQ_WAKEUP[] = {0xfe, 0x04, 0xff, 0xff};
static const unsigned char REQ_INIT[] = {0x72, 0x05, 0x00, 0xf0, 0x99};

static const unsigned char REQ_READ_TABLE_11[] = {0x72, 0x05, 0x71, 0x11, 0x07};
static const unsigned char REQ_READ_TABLE_D1[] = {0x72, 0x05, 0x71, 0xd1, 0x47};

static const char TO_HEX[] = "0123456789ABCDEF";

static int main_state = MAIN_STM_NONE;
static int main_watchdog = 0;

static int msg_state = MSG_STM_IDLE;
static int msg_index = 0;
static int msg_length = 0;
static unsigned char msg_buf[32];
static char str_buf[64];

#define DBG(...) {\
  snprintf(str_buf, sizeof(str_buf), __VA_ARGS__);\
  write_upstream(str_buf, strlen(str_buf));\
}\

#define WRITE_UPSTREAM(msg) {\
  char *ptr = str_buf;\
  *ptr++ = ':';\
  for (int i = 0; i < msg[1]; i++) {\
    ptr += to_hex(ptr, msg[i]);\
  }\
  write_upstream(str_buf, 1+msg[1]*2);\
}\

static int to_hex(char *ptr, unsigned char v)
{
    ptr[0] = TO_HEX[v>>4];
    ptr[1] = TO_HEX[v&0xf];
    return 2;
}

static int verify_msg_csum(unsigned char *msg)
{
    //DBG("verify_msg_csum");
    int csum = 0;
    int len = msg[1];

    for (int i = 0; i < len-1; i++)
    {
        csum += msg[i];
    }

    csum = (0x100 - csum) & 0xff;
    return csum == msg[len-1];
}

static void reset_msg_stm(void)
{
    //DBG("reset_msg_stm");
    msg_index = 0;
    msg_length = 0;
    msg_state = MSG_STM_IDLE;
}

static int do_msg_stm(unsigned char rx)
{
    int res = MSG_STATUS_NONE;

    switch (msg_state)
    {
    case MSG_STM_IDLE:
        msg_buf[msg_index++] = rx;
        msg_state = MSG_STM_LENGTH;
        break;

    case MSG_STM_LENGTH:
        msg_length = rx;

        if (msg_length >= 3 && msg_length <= sizeof(msg_buf))
        {
            msg_buf[msg_index++] = rx;
            msg_state = MSG_STM_DATA;
        }
        else
        {
            res = MSG_STATUS_ERR;
            reset_msg_stm();
        }
        break;

    case MSG_STM_DATA:
        msg_buf[msg_index++] = rx;
        if (msg_index >= msg_length)
        {
            res = verify_msg_csum(msg_buf) ? MSG_STATUS_OK : MSG_STATUS_ERR;
            reset_msg_stm();
        }
        break;
    }

    return res;
}

static void ecu_send_req(const unsigned char *msg)
{
    write_downstream(msg, msg[1]);
}

static int ecu_process_msg(unsigned char *msg)
{
    // Ecu response
    if (msg[0] == 0x02)
    {
        //DBG("valid message");
        switch (msg[2])
        {
        // Init OK
        case 0x00:
            ecu_send_req(REQ_READ_TABLE_11);
            break;

        // Table contents
        case 0x71:
            WRITE_UPSTREAM(msg);
            if (msg[3] == 0x11)
            {
                ecu_send_req(REQ_READ_TABLE_D1);
            }

            if (msg[3] == 0xD1)
            {
                return MAIN_STM_REQ_11;
            }
            break;
        }
    }

    return MAIN_STM_RUN;
}

int do_main_stm(int reason, unsigned char rx)
{
    static int cnt = 0;

    // State machine init
    if (reason == MAIN_REASON_INIT)
    {
        main_state = MAIN_STM_WAKEUP;
        main_watchdog = 0;
        cnt = 0;
        return 1;
    }

    // Periodic actions every 2.5 seconds when in running state
    if (reason == MAIN_REASON_NONE && main_state >= MAIN_STM_RUN)
    {
        if (++cnt < 50)
        {
            return 1;
        }
        cnt = 0;
    }

    switch (main_state)
    {
    case MAIN_STM_NONE:
        // Empty
        break;

    case MAIN_STM_BREAK:
        // Empty
        break;

    case MAIN_STM_WAKEUP:
        if (reason == MAIN_REASON_NONE)
        {
            ecu_send_req(REQ_WAKEUP);
            main_state = MAIN_STM_INIT;
        }
        break;

    case MAIN_STM_INIT:
        if (reason == MAIN_REASON_NONE)
        {
            ecu_send_req(REQ_INIT);
            main_state = MAIN_STM_RUN;
        }
        break;

    case MAIN_STM_RUN:
        if (reason == MAIN_REASON_RX)
        {
            int res = do_msg_stm(rx);

            if (res == MSG_STATUS_OK)
            {
                main_state = ecu_process_msg(msg_buf);
                main_watchdog = 0;
            }

            if (res == MSG_STATUS_ERR)
            {
                main_state = MAIN_STM_REQ_11;
            }
        }
        else // every ~2.5 seconds
        {
            if (++main_watchdog > MAIN_WATCHDOG_MAX)
            {
                DBG("#reinit");
                return 0;
            }
        }
        break;

    case MAIN_STM_REQ_11:
        // after ~2.5 seconds
        if (reason == MAIN_REASON_NONE)
        {
            ecu_send_req(REQ_READ_TABLE_11);
            main_watchdog = 0;
            main_state = MAIN_STM_RUN;
        }
        break;
    }

    return 1;
}

