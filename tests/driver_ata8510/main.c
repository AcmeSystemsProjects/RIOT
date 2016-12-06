/*
 * Copyright (C) 2016 Antonio Galea <antonio.galea@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     tests
 * @{
 *
 * @file
 * @brief       Test application for ATA8510 network device driver
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 * @author      Roberto Asquini <bobasquins@gmail.com>
 *
 * @}
 */

#include <stdio.h>

#include "net/netdev2.h"
#include "shell.h"
#include "shell_commands.h"
#include "thread.h"
#include "xtimer.h"
#include "timex.h"

#include "common.h"
#include "ps.h"

#include "ata8510.h"
#include "od.h"
#include "net/ieee802154.h"
#include "net/netdev2.h"

#include "common.h"

#define MAX_LINE    (80)

/* set intervals */
#define INTERVALTX (5U * SEC_IN_USEC)
#define INTERVALRX 300000U
#define INTERVALRSSI 100000U
#define INTERVALTXRANDMAX 3000000U
#define INTERVALTXRANDMIN 1000000U
#define INTERVALCHECKRX 200000U

#define ENABLE_DEBUG (0)
#include "debug.h"

#include "msg.h"


#include <string.h>


#if FEATURE_PERIPH_RTC
#include "periph/rtc.h"
#endif


#define _STACKSIZE      (THREAD_STACKSIZE_DEFAULT + THREAD_EXTRA_STACKSIZE_PRINTF)
#define MSG_TYPE_ISR    (0x3456)
#define MAXYARMTX 5     // max permitted value is 9 for now
#include "checksum/fletcher16.h"

#define RCV_QUEUE_SIZE  (2)
static msg_t rcv_queue[RCV_QUEUE_SIZE];

void my_recv(netdev2_t *dev);

static char stack[_STACKSIZE];
static kernel_pid_t _recv_pid;

ata8510_t devs[ATA8510_NUM];

static const shell_command_t shell_commands[] = {
    { "ifconfig", "Configure netdev2", ifconfig },
    { "txtsnd", "Send IEEE 802.15.4 packet", txtsnd },
    { NULL, NULL, NULL }
};

int lost_interrupts=0;

static void _event_cb(netdev2_t *dev, netdev2_event_t event)
{
    if (event == NETDEV2_EVENT_ISR) {
        msg_t msg;

        msg.type = MSG_TYPE_ISR;
        msg.content.ptr = dev;

        if (msg_send(&msg, _recv_pid) <= 0) {
            lost_interrupts++;
            puts("event_cb gnrc_netdev2: possibly lost interrupt.");
        }
    }
    else {
        switch (event) {
            case NETDEV2_EVENT_RX_COMPLETE:
#if THREADCHECKRXERRORS
                my_recv(dev);
#else
#if THREADTXRAND
                printf(
                    "event_cb gnrc_netdev2: received a packet on service %d, channel %d\n",
                    ((ata8510_t *)dev)->service, ((ata8510_t *)dev)->channel
                );
#else
                recv(dev);
#endif
#endif
                break;
            case NETDEV2_EVENT_TX_MEDIUM_BUSY:
                puts("event_cb gnrc_netdev2: transfer still pending.");
                break;
            case NETDEV2_EVENT_TX_COMPLETE:
                puts("event_cb gnrc_netdev2: transfer complete.");
                break;
            default:
                printf("Unexpected event received %d\n", event);
                break;
        }
    }
}

void *_recv_thread(void *arg)
{
    msg_init_queue(rcv_queue, RCV_QUEUE_SIZE);
    while (1) {
        msg_t msg;
        msg_receive(&msg);
        if (msg.type == MSG_TYPE_ISR) {
            netdev2_t *dev = msg.content.ptr;
            dev->driver->isr(dev);
        }
        else {
            puts("unexpected message type");
        }
    }
}

// ------------------------------------------------------------------------------


#ifdef THREADTXRAND
#include "random.h"
#define RAND_SEED 0xC0FFEE

void *thread_tx_rand(void *arg)     // Still has a problem on the very first message sent: the sniffing is returning zeroes. To be fixed..
{
    // Transmit  a message every .... seconds where first byte is ID8510, then 6 bytes as counter of transmissions sent.
    ata8510_t *dev = &devs[0]; // acquires the 8510 device handle
    int numtx = 1;  // counter for trasmissions
    char msg[ATA8510_MAX_PKT_LENGTH+1];
    char msg2[16];
    uint32_t time_between_tx;
    uint8_t checksum;
    xtimer_ticks32_t last_wakeup = xtimer_now();
    uint8_t myturn;
    uint8_t i, n;
    struct iovec vector[1];

    printf("thread tx rand, pid: %" PRIkernel_pid "\n", thread_getpid());
    random_init(RAND_SEED);

    time_between_tx = 1000000U;
    while (1) {
        xtimer_periodic_wakeup(&last_wakeup, time_between_tx);
        printf("state: %d\n", ata8510_get_state(dev));
        dev->service = 2;
        dev->channel = 0;

        sprintf(msg2, "%d%06d_", ID8510, numtx);
        checksum = fletcher16((const uint8_t*)msg2, strlen(msg2));
        sprintf(msg, "%s%02x", msg2, checksum);
        n = strlen(msg);
        for(i=n;i<ATA8510_MAX_PKT_LENGTH-1;i++){ msg[i] = 'A' + (numtx - 1 + i - n) % 26; }
        msg[ATA8510_MAX_PKT_LENGTH-1]='.';
        msg[ATA8510_MAX_PKT_LENGTH]=0; // terminate msg (just needed for printf)
        numtx++;
        printf("Sending %d bytes using service %d:\n%s\n", ATA8510_MAX_PKT_LENGTH, dev->service, msg);

        // test listen before talk
        (void)myturn;
/*
        do {
            while (!ata8510_cca(dev)) {
                xtimer_usleep(100);
            }
            myturn = 1;
            // after the end of other transmission, sense channel for a random number of times + 2
            // if someone else took control, restart waiting with first loop, otherwise start transmit
            for (i=0; i<(((random_uint32() % 10)+2)*2); i++) {
                if (!ata8510_cca(dev)) { // if sensing channel occupied abort second loop and restart first loop of sensing
                    myturn = 0;
                    break;
                }
            }
        } while (myturn == 0);
*/

        vector[0].iov_base = msg;
        vector[0].iov_len = ATA8510_MAX_PKT_LENGTH;
        ((netdev2_t *)dev)->driver->send((netdev2_t *)dev, vector, 1);

        time_between_tx = (dev->service==0 ? 5 : 1 ) * SEC_IN_USEC;
        time_between_tx = 0U;
        //time_between_tx = 1000000U + (random_uint32() % 3000000 ); // between 1 and 4 s
        last_wakeup = xtimer_now();
   }

    return NULL;
}

char thread_tx_rand_stack[THREAD_STACKSIZE_MAIN];
#endif

static uint8_t buffer[ATA8510_MAX_PKT_LENGTH];

int rxcounter[MAXYARMTX+1];  // YARM TX ID start from 1
int rxerrors[MAXYARMTX+1];    // YARM TX ID start from 1
int msgcounter[MAXYARMTX+1];  // YARM TX ID start from 1
int rxfirstreceived[MAXYARMTX+1];  // marker for first message received or not
int tot_messages = 0;
int tot_restarts = 0;

void my_recv(netdev2_t *dev)
{
    size_t data_len;
    netdev2_ieee802154_rx_info_t rx_info;
    int i;
    int discard;
    int yarmtxidreceived = 0;
    int yarmtxreceivedcounter = 0;
    int power10;
    uint8_t checksum, checksum_received;

    data_len = dev->driver->recv(dev, buffer, sizeof(buffer), &rx_info);
#if ENABLE_DEBUG
    DEBUG(
        "RECV %d bytes on service %d, channel %d:\n",
        data_len,
        ((ata8510_t *)dev)->service,
        ((ata8510_t *)dev)->channel
    );
    od_hex_dump(buffer, data_len, 0);
    DEBUG("txt:\n     ");
    for (i = 0; i < data_len; i++) {
        if ((buffer[i] > 0x1F) && (buffer[i] < 0x80)) {
            putchar((char)buffer[i]);
        }
        else {
            putchar('?');
        }
        if ((((i + 1) % (MAX_LINE - sizeof("txt: "))) == 1) && i != 0) {
            DEBUG("\n     ");
        }
    }
    DEBUG("\n");
    DEBUG("RSSI: %u, dBm: %d\n", rx_info.rssi, ata8510_calc_dbm(rx_info.rssi));
#endif


    // analysis of received message
    tot_messages++;
    discard = 0;
    if (buffer[0] > '0' && buffer[0] <= ('0'+MAXYARMTX)) {
        yarmtxidreceived = buffer[0] - '0';  // extract YARM ID
        power10 = 1;
        yarmtxreceivedcounter = 0;
        for (i=6; i>=1; i--) {  // extract counter sent by YARM (bytes 1 to 7)
            if (buffer[i] >= '0' && buffer[i] <= '9') { // is a digit
                yarmtxreceivedcounter += ((buffer[i]-'0') * power10);
                power10 *= 10;
            } else {
                printf("ERROR: not a digit in %d position of message: %c. Discard Message!\n",
                    i, buffer[i]);
                discard = 1;
                break;
            }
        }
        printf("Message extracted: Yarm ID %d:  Counter %d\n", yarmtxidreceived, yarmtxreceivedcounter);
        if (discard == 0) {
            if (rxfirstreceived[yarmtxidreceived] == 1) {
                // check msg length
                if (data_len < 10) {
                    printf("ERROR: wrong message length %d: Discard Message!\n", data_len);
                } else {
                    // checksum control
                    checksum_received = (buffer[8]<=0x39 ? (buffer[8]-0x30)*16 :
                        ((buffer[8]-0x61)+10)*16) +
                        (buffer[9]<=0x39 ? (buffer[9]-0x30) :
                        (buffer[9]-0x61)+10);
                    checksum = fletcher16(buffer, 8);
                    if (checksum != checksum_received) {
                        printf("ERROR: wrong checksum received %02x instead of %02x: Discard Message!\n",
                            checksum_received, checksum);
                    } else {
                        // length and checksum ok and already received a message from this YARM. We can analyze errors
                        if (yarmtxreceivedcounter == (rxcounter[yarmtxidreceived] + 1)) { // message received is correct
                            rxcounter[yarmtxidreceived]++;
                            msgcounter[yarmtxidreceived]++;
                        } else {
                            if (yarmtxreceivedcounter < rxcounter[yarmtxidreceived]) {
                                // YARM TX probably restarted
                                printf("ERROR: YARM TX %d probably restarted. Reset counters for it\n", yarmtxidreceived);
                                rxerrors[yarmtxidreceived] = 0;
                                tot_restarts++;
                            } else {
                                // errors happened on this YARM TX
                                rxerrors[yarmtxidreceived] += yarmtxreceivedcounter - rxcounter[yarmtxidreceived] -1;
                            }
                            rxcounter[yarmtxidreceived] = yarmtxreceivedcounter;
                        }
                    }
                }
            } else {
                // first message received from this YARM: set the counter to this value and skip errors analysis
                rxcounter[yarmtxidreceived] = yarmtxreceivedcounter;
                // mark first message received
                rxfirstreceived[yarmtxidreceived] = 1;
            }
        }
    } else {
       printf("error: wrong yarm tx id received: %c. discard message\n",buffer[0]);
    }
    printf("\n\n");
}

#ifdef THREADCHECKRXERRORS
void *thread_check_rx_errors(void *arg)
{
    int i;
    int perc_error_integer;
    int perc_error_decimal;
    int time_elapsed;
    int time_elapsed_saved;
    int overflows_time = 0;
    int time_printed = -1;
    int tot_errors = 0;
    xtimer_ticks32_t start_time;
    ata8510_t *dev = &devs[0]; // acquires the 8510 device handle

    printf("thread check rx errors started, pid: %" PRIkernel_pid "\n", thread_getpid());
    xtimer_ticks32_t last_wakeup = xtimer_now();

    start_time = xtimer_now();
    time_elapsed_saved = 0;

    while (1) {
        xtimer_periodic_wakeup(&last_wakeup, INTERVALCHECKRX);

        // print statistics
        time_elapsed = ((xtimer_now()).ticks32 - start_time.ticks32) / 1000000;
        if (time_elapsed % 10 == 0 && time_elapsed != time_printed) { // every 10 seconds prints statistics
            time_printed = time_elapsed;
            // there is here still an error on the calc of overflows since the printf at the end is wrong...
            if (time_elapsed < time_elapsed_saved) {
                // overflow at 4295 s
                overflows_time++;
            } else {
                time_elapsed_saved = time_elapsed;
            }
            printf("Errors/msgs: ");
            tot_errors = 0;
            for (i=1; i<=MAXYARMTX; i++) {
                if (rxfirstreceived[i] == 1) {
                    printf("[%d]: %d/%d  ", i, rxerrors[i], msgcounter[i]);
                    tot_errors += rxerrors[i];
                }
            }
            perc_error_integer = 100*tot_errors/tot_messages;
            perc_error_decimal = 10000*tot_errors/tot_messages - 100*perc_error_integer;

            // the print of Time(s) still is not working well after 4295s
            printf("\nTime(s) %d Msgs: %d Restarts: %d SysErrs %d Errors: %d %%Error: %d.%02d%%\n\n",
                time_elapsed + overflows_time*4295, tot_messages, tot_restarts, dev->sys_errors,
                tot_errors, perc_error_integer, perc_error_decimal);
        }
    }
    return NULL;
}

char thread_check_rx_errors_stack[THREAD_STACKSIZE_MAIN];
#endif


int main(void)
{
    puts("\n\r\n\rATA8510 device driver test");
    xtimer_init();

    for (unsigned i = 0; i < ATA8510_NUM; i++) {
        const ata8510_params_t *p = &ata8510_params[i];
        netdev2_t *dev = (netdev2_t *)(&devs[i]);

        printf("Initializing ATA8510 radio at SPI_%d\n", p->spi);
        ata8510_setup(&devs[i], (ata8510_params_t*) p);
        dev->event_callback = _event_cb;
        dev->driver->init(dev);
        printf("STATE: %d\n", ata8510_get_state(&devs[i]));
    }

    _recv_pid = thread_create(stack, sizeof(stack), THREAD_PRIORITY_MAIN - 1,
                              THREAD_CREATE_STACKTEST, _recv_thread, NULL,
                              "recv_thread");

    if (_recv_pid <= KERNEL_PID_UNDEF) {
        puts("Creation of receiver thread failed");
        return 1;
    }

#ifdef THREADTXRAND
    printf("[main] Starting tx rand thread...\n");
    thread_create(thread_tx_rand_stack, sizeof(thread_tx_rand_stack),
                            THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST,
                            thread_tx_rand, NULL, "tx_rand_daemon");
#endif

#ifdef THREADCHECKRXERRORS
    // zero errors and counter current tx value for each YARM Tx
    for (int i=0; i<MAXYARMTX+1; i++) {
        rxcounter[i] = 0;
        rxerrors[i] = 0;
        msgcounter[i] = 0;
        rxfirstreceived[i] = 0;
    }

    printf("[main] Starting check rx errors thread...\n");
    thread_create(thread_check_rx_errors_stack, sizeof(thread_check_rx_errors_stack),
                            THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST,
                            thread_check_rx_errors, NULL, "check_rx_errors_daemon");
#endif

#ifdef FEATURE_PERIPH_RTC
    rtc_init();
#endif

    /* start the shell */
    puts("Initialization successful - starting the shell now");
    puts("\nWelcome to YARM for RIOT test8!");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
