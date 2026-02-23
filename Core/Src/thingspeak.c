/*
 * thingspeak.c
 * Author: BlackWiz Portfolio
 */

#include "thingspeak.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;

typedef enum {
    TS_STATE_IDLE = 0,
    TS_STATE_RESOLVING,
    TS_STATE_CONNECTING,
    TS_STATE_SENDING,
    TS_STATE_WAIT_ACK,
} ts_state_t;

typedef struct {
    ts_state_t state;
    struct tcp_pcb *pcb;
    ip_addr_t remote_ip;
    int field1;
    int field2;
} thingspeak_app_t;

static thingspeak_app_t ts;

static void ts_close(void);
static void ts_dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
static err_t ts_connected(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t ts_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void ts_error(void *arg, err_t err);
static err_t ts_poll(void *arg, struct tcp_pcb *tpcb);

static void TS_Log(char *msg) {
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

void thingspeak_init(void) {
    memset(&ts, 0, sizeof(ts));
    ts.state = TS_STATE_IDLE;
    TS_Log("ThingSpeak: Client Initialized (IDLE)\r\n");
}

void thingspeak_send(int val1, int val2) {
    char buf[64];

    // If the old connection hung around, clean it up, but DON'T abort the new send!
    if (ts.state != TS_STATE_IDLE) {
        TS_Log("ThingSpeak: Old connection lingered. Forcing close...\r\n");
        ts_close();
    }

    ts.field1 = val1;
    ts.field2 = val2;
    ts.state = TS_STATE_RESOLVING;

    snprintf(buf, sizeof(buf), "ThingSpeak: Starting... (F1:%d, F2:%d)\r\n", val1, val2);
    TS_Log(buf);

    err_t err = dns_gethostbyname(TS_HOST, &ts.remote_ip, ts_dns_found, NULL);
    if (err == ERR_OK) ts_dns_found(TS_HOST, &ts.remote_ip, NULL);
}

static void ts_dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    char buf[64];
    if ((ipaddr) && (ipaddr->addr)) {
        ts.remote_ip = *ipaddr;
        snprintf(buf, sizeof(buf), "ThingSpeak: IP is %lu.%lu.%lu.%lu\r\n",
                 (ts.remote_ip.addr & 0xff), ((ts.remote_ip.addr >> 8) & 0xff),
                 ((ts.remote_ip.addr >> 16) & 0xff), (ts.remote_ip.addr >> 24));
        TS_Log(buf);

        ts.pcb = tcp_new();
        if (ts.pcb != NULL) {
            tcp_arg(ts.pcb, NULL);
            tcp_err(ts.pcb, ts_error);

            // Set up our graceful 1-second interval timer
            tcp_poll(ts.pcb, ts_poll, 2);

            ts.state = TS_STATE_CONNECTING;
            tcp_connect(ts.pcb, &ts.remote_ip, TS_PORT, ts_connected);
        }
    } else {
        TS_Log("ThingSpeak: DNS Failed (No IP)\r\n");
        ts_close();
    }
}

static err_t ts_connected(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) {
        ts_close();
        return err;
    }

    TS_Log("ThingSpeak: TCP Connected. Sending...\r\n");
    ts.state = TS_STATE_SENDING;

    char payload[64];
    char request[300];

    snprintf(payload, sizeof(payload), "api_key=%s&field1=%d&field2=%d",
             TS_API_KEY, ts.field1, ts.field2);

    snprintf(request, sizeof(request),
             "POST /update HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %d\r\n\r\n"
             "%s",
             TS_HOST, strlen(payload), payload);

    tcp_write(tpcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    ts.state = TS_STATE_WAIT_ACK;
    tcp_recv(tpcb, ts_recv);
    return ERR_OK;
}

static err_t ts_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        TS_Log("ThingSpeak: Success. Server closed connection.\r\n");
        ts_close();
        return ERR_OK;
    }

    tcp_recved(tpcb, p->tot_len);

    if (p->len > 0) {
        char rx_buf[64];
        int len = (p->len < 63) ? p->len : 63;
        memcpy(rx_buf, p->payload, len);
        rx_buf[len] = '\0';
        TS_Log("RX Reply: "); TS_Log(rx_buf); TS_Log("\r\n");
    }

    pbuf_free(p);
    return ERR_OK;
}

// Graceful Watchdog: Closes the socket politely after 4 seconds of waiting
static err_t ts_poll(void *arg, struct tcp_pcb *tpcb) {
    static int timeout_ticks = 0;

    if (ts.state == TS_STATE_WAIT_ACK) {
        timeout_ticks++;
        if (timeout_ticks >= 4) { // 4 polls = ~4 seconds
            TS_Log("ThingSpeak: Transaction complete. Closing socket.\r\n");
            ts_close();
            timeout_ticks = 0;
        }
    } else {
        timeout_ticks = 0;
    }
    return ERR_OK;
}

static void ts_error(void *arg, err_t err) {
    char buf[32];
    snprintf(buf, sizeof(buf), "ThingSpeak: TCP Error %d\r\n", err);
    TS_Log(buf);
    ts.state = TS_STATE_IDLE;
    ts.pcb = NULL;
}

static void ts_close(void) {
    if (ts.pcb != NULL) {
        tcp_arg(ts.pcb, NULL);
        tcp_sent(ts.pcb, NULL);
        tcp_recv(ts.pcb, NULL);
        tcp_err(ts.pcb, NULL);
        tcp_poll(ts.pcb, NULL, 0); // Stop polling
        tcp_close(ts.pcb);
        ts.pcb = NULL;
    }
    ts.state = TS_STATE_IDLE;
}
