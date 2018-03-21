#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MSG_STM_IDLE     0
#define MSG_STM_LENGTH   1
#define MSG_STM_DATA     2

#define MSG_STATUS_NONE  0
#define MSG_STATUS_OK    1
#define MSG_STATUS_ERR   2

static int msg_state = MSG_STM_IDLE;
static int msg_index = 0;
static int msg_length = 0;
static unsigned char msg_buf[32];

extern void write_upstream(const char *msg, int n);
extern void write_downstream(const unsigned char *msg, int n);

#define DBG(...) {\
  char __buf[80];\
  snprintf(__buf, sizeof(__buf), __VA_ARGS__);\
  write_upstream(__buf, strlen(__buf));\
}\

int verify_msg_csum(unsigned char *msg)
{
    //DBG("verify_msg_csum\n");

    int csum = 0;
    int len = msg[1];

    for (int i = 0; i < len-1; i++)
    {
        csum += msg[i];
    }

    csum = (0x100 - csum) & 0xff;
    return csum == msg[len-1];
}

void reset_msg_stm(void)
{
    //DBG("reset_msg_stm\n");
    msg_index = 0;
    msg_length = 0;
    msg_state = MSG_STM_IDLE;
}

int do_msg_stm(unsigned char rx)
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

#define MAIN_STM_NONE       0
#define MAIN_STM_BREAK      1
#define MAIN_STM_WAKEUP     2
#define MAIN_STM_INIT       3
#define MAIN_STM_RUN        4
#define MAIN_STM_REQ_11     5

#define MAIN_REASON_NONE    0
#define MAIN_REASON_RX      1
#define MAIN_REASON_INIT    2

#define MAIN_WATCHDOG_MAX   5

static const unsigned char REQ_WAKEUP[] = {0xfe, 0x04, 0xff, 0xff};
static const unsigned char REQ_INIT[] = {0x72, 0x05, 0x00, 0xf0, 0x99};

static const unsigned char REQ_READ_TABLE_11[] = {0x72, 0x05, 0x71, 0x11, 0x07};
static const unsigned char REQ_READ_TABLE_D1[] = {0x72, 0x05, 0x71, 0xd1, 0x47};

static int main_state = MAIN_STM_NONE;
static int main_watchdog = 0;

void ecu_send_req(const unsigned char *msg)
{
    write_downstream(msg, msg[1]);
}

int ecu_process_msg(unsigned char *msg)
{
    // Ecu response
    if (msg[0] == 0x02)
    {
        DBG("valid message\n");

        switch (msg[2])
        {
        // Init OK
        case 0x00:
            ecu_send_req(REQ_READ_TABLE_11);
            break;

        // Table contents
        case 0x71:
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

void do_main_stm(int reason, unsigned char rx)
{
    static int cnt = 0;
    cnt++;

    if (reason == MAIN_REASON_NONE && main_state >= MAIN_STM_RUN)
    {
        if (cnt < 100)
        {
            return;
        }
        cnt = 0;
    }

    if (reason == MAIN_REASON_INIT)
    {
        main_state = MAIN_STM_WAKEUP;
    }

    switch (main_state)
    {
    case MAIN_STM_NONE:
        break;

    case MAIN_STM_BREAK:
        break;

    case MAIN_STM_WAKEUP:
        ecu_send_req(REQ_WAKEUP);
        main_state = MAIN_STM_INIT;
        break;

    case MAIN_STM_INIT:
        ecu_send_req(REQ_INIT);
        main_watchdog = 0;
        cnt = 0;
        main_state = MAIN_STM_RUN;
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
                //main_state = MAIN_STM_INIT;
            }
        }
        else
        {
            if (++main_watchdog > MAIN_WATCHDOG_MAX)
            {
                DBG("main state to init\n");
                //reset_msg_stm();
                //main_state = MAIN_STM_INIT;
            }
        }
        break;

    case MAIN_STM_REQ_11:
        if (reason == MAIN_REASON_NONE)
        {
            ecu_send_req(REQ_READ_TABLE_11);
            main_state = MAIN_STM_RUN;
        }
        break;
    }
}

#if 0

int main(void)
{
    //unsigned char msg[] = {0x72, 0x05, 0x71, 0x00, 0x18};
    //unsigned char msg[] = {0x01, 0x03, 0xfc};
    //unsigned char msg[] = {0x02, 0x04, 0x00, 0xfa};
    unsigned char msg[] = {0x02, 0x05, 0x71, 0x11, 0x77};

    do_main_stm(MAIN_REASON_NONE, 0);

    while (1)
    {
        for (int i = 0; i < sizeof(msg); i++)
        {
            do_main_stm(MAIN_REASON_RX, msg[i]);
        }

        for (int i = 0; i < 7; i++)
        {
            do_main_stm(MAIN_REASON_NONE, 0);
            sleep(1);
        }
    }

    return 0;
}

#endif











