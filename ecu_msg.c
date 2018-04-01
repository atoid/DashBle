#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_simple_timer.h"
#include "ble_nus.h"
#include "app_uart.h"
#include "nrf_gpio.h"

#include "ecu_msg.h"

#define DASH_DISCONNECTED   0
#define DASH_CONNECTED      1

#define RUUVI_LED_RED       17
#define RUUVI_LED_GREEN     19

#define BREAK_TYPE_LO_BAUD  0

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

extern ble_nus_t m_nus;
extern uint16_t m_conn_handle;

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

static void init_timer_handler(void * p_context);

#if BREAK_TYPE_LO_BAUD == 1

// BREAK using very low baudrate
static void break_downstream(int state)
{
    if (state)
    {
        NRF_UART0->BAUDRATE = BREAK_BAUDRATE;
        app_uart_put(0x00);
    }
    else
    {
        NRF_UART0->BAUDRATE = ECU_BAUDRATE;
    }
}

#else

// BREAK using TX pin as GPIO
static void break_downstream(int state)
{
    if (state)
    {
        NRF_UART0->PSELTXD = UART_PIN_DISCONNECTED;
        nrf_gpio_pin_write(RUUVI_UART_TX, 0);
    }
    else
    {
        nrf_gpio_pin_write(RUUVI_UART_TX, 1);
        NRF_UART0->PSELTXD = RUUVI_UART_TX;
    }
}

#endif

static void write_upstream(const char *msg, int n)
{
    int len = n;
    uint8_t *ptr = (uint8_t *)msg;

    while (len)
    {
        int sz = len < BLE_NUS_MAX_DATA_LEN ? len : BLE_NUS_MAX_DATA_LEN;
        ble_nus_string_send(nus_get_service(), ptr, sz);
        len -= sz;
        ptr += sz;
    }
}

static void write_downstream(const unsigned char *msg, int n)
{
    for (int i = 0; i < n; i++)
    {
        while(app_uart_put(msg[i]) != NRF_SUCCESS);
    }
}

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

static void blink_status(void)
{
    static int cnt = 0;

    if (nus_get_conn_handle() == BLE_CONN_HANDLE_INVALID)
    {
        if (cnt == 0)
        {
            nrf_gpio_pin_set(RUUVI_LED_RED);
        }
        if (cnt == 98)
        {
            nrf_gpio_pin_clear(RUUVI_LED_RED);
        }
    }
    else
    {
        if (cnt == 0)
        {
            nrf_gpio_pin_set(RUUVI_LED_RED);
        }
        if (cnt == 90)
        {
            nrf_gpio_pin_clear(RUUVI_LED_RED);
        }
    }

    cnt = (cnt+1) % 100;
}

static void main_timer_handler(void * p_context)
{
    static int prev_state = DASH_DISCONNECTED;

    blink_status();

    if (nus_get_conn_handle() != BLE_CONN_HANDLE_INVALID)
    {
        if (prev_state == DASH_DISCONNECTED)
        {
            do_main_stm(MAIN_REASON_INIT, 0);
            app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 10000, (void *) 0);
        }
        else
        {
            // Restart if main state machine returns 0
            if (!do_main_stm(MAIN_REASON_NONE, 0))
            {
                prev_state = DASH_DISCONNECTED;
                return;
            }
        }

        prev_state = DASH_CONNECTED;
    }
    else
    {
        prev_state = DASH_DISCONNECTED;
    }
}

static void init_timer_handler(void * p_context)
{
    int state = (int) p_context;

    switch (state)
    {
    case 0:
        break_downstream(1);
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 35000, (void *) 1);
        break;
    case 1:
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 35000, (void *) 2);
        break;
    case 2:
        break_downstream(0);
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 50000, (void *) 3);
        break;
    case 3:
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 50000, (void *) 4);
        break;
    case 4:
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 30000, (void *) 5);
        break;
    case 5:
        do_main_stm(MAIN_REASON_NONE, 0);
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_SINGLE_SHOT, init_timer_handler, 50000, (void *) 6);
        break;
    case 6:
        do_main_stm(MAIN_REASON_NONE, 0);
        app_simple_timer_start(APP_SIMPLE_TIMER_MODE_REPEATED, main_timer_handler, 50000, NULL);
        break;
    }
}

void ecu_init(void)
{
    nrf_gpio_cfg_input(RUUVI_UART_RX, NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg(
        RUUVI_UART_TX,
        NRF_GPIO_PIN_DIR_OUTPUT,
        NRF_GPIO_PIN_INPUT_DISCONNECT,
        NRF_GPIO_PIN_NOPULL,
        NRF_GPIO_PIN_H0S1,
        NRF_GPIO_PIN_NOSENSE);

    nrf_gpio_pin_write(RUUVI_UART_TX, 1);

    nrf_gpio_cfg_output(RUUVI_LED_RED);
    nrf_gpio_cfg_output(RUUVI_LED_GREEN);

    nrf_gpio_pin_set(RUUVI_LED_RED);
    nrf_gpio_pin_set(RUUVI_LED_GREEN);

    // Start main timer
    app_simple_timer_init();
    app_simple_timer_start(APP_SIMPLE_TIMER_MODE_REPEATED, main_timer_handler, 50000, NULL);
}
