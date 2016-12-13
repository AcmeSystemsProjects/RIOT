/*
 * Copyright (C) 2016 Antonio Galea <antonio.galea@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_ata8510
 * @{
 *
 * @file
 * @brief       Netdev adaption for the ATA8510 driver
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 * @author      Roberto Asquini <bobasquins@gmail.com>
 *
 * @}
 */

#include <string.h>
#include <assert.h>
#include <errno.h>

#include "net/eui64.h"
#include "net/ieee802154.h"
#include "net/netdev2.h"
#include "net/netdev2/ieee802154.h"

#include "ata8510.h"
#include "ata8510_netdev.h"
#include "ata8510_internal.h"
#include "xtimer.h"

#define DEBUG_ISR             0x01
#define DEBUG_ISR_EVENTS      0x02
#define DEBUG_ISR_EVENTS_TRX  0x04
#define DEBUG_PKT_DUMP        0x08
#define DEBUG_SEND            0x10
#define DEBUG_RECV            0x20

//#define ENABLE_DEBUG      (0)
//#define ENABLE_DEBUG     (DEBUG_ISR | DEBUG_ISR_EVENTS | DEBUG_ISR_EVENTS_TRX | DEBUG_SEND | DEBUG_RECV | DEBUG_PKT_DUMP)
#define ENABLE_DEBUG     (DEBUG_ISR | DEBUG_ISR_EVENTS | DEBUG_ISR_EVENTS_TRX | DEBUG_RECV)
#include "debug.h"

/**
 * backoff algorithm, from csma_sender.c
 *
 */
#include "net/csma_sender.h"


#define ATA_CSMA_SENDER_BACKOFF_PERIOD_UNIT  (8200U)

static csma_sender_conf_t conf_data = {
    .min_be = 3,			/**< minimum backoff exponent */
    .max_be = 6,			/**< maximum backoff exponent */
    .max_backoffs = 10,		/**< maximum number of retries */
    .backoff_period = ATA_CSMA_SENDER_BACKOFF_PERIOD_UNIT /**< backoff period in microseconds */
};
csma_sender_conf_t *conf = &conf_data;

#include "random.h"

/**
 * @brief choose an adequate random backoff period in microseconds,
 *        from the given Backoff Exponent
 *
 * @param[in] be        Backoff Exponent for the computation of period
 *
 * @return              An adequate random backoff exponent in microseconds
 */
static inline uint32_t choose_backoff_period(int be,
                                             const csma_sender_conf_t *conf)
{
    if (be < conf->min_be) {
        be = conf->min_be;
    }
    if (be > conf->max_be) {
        be = conf->max_be;
    }
    uint32_t max_backoff = ((1 << be) - 1) * ATA_CSMA_SENDER_BACKOFF_PERIOD_UNIT;
	uint32_t rv = random_uint32();
    uint32_t period = rv % max_backoff;

    //printf ("*********** choose_backoff_period: max_backoff: %lu period: %lu random: %lu\n", max_backoff, period, rv);

    if (period < ATA_CSMA_SENDER_BACKOFF_PERIOD_UNIT) {
        period = ATA_CSMA_SENDER_BACKOFF_PERIOD_UNIT;
    }

    return period;
}


#ifdef ENABLE_DEBUG
#include "ringbuffer.h"
#include <stdio.h>
#include <stdarg.h>
static char _dbg_buf_array[1024];
static ringbuffer_t _dbg_buf = RINGBUFFER_INIT(_dbg_buf_array);
static void debug_later(char *fmt, ...) __attribute__((__format__(__printf__, 1, 2)));
static void debug_later(char *fmt, ...);
static void debug_now(void);
#define DEBUG_LATER(...) debug_later(__VA_ARGS__)
#define DEBUG_NOW()      debug_now()
static void debug_later(char *fmt, ...){
  char buf[256];
  va_list argp;
  va_start(argp, fmt);
  int i;
  int n = vsprintf(buf, fmt, argp);
  va_end(argp);
  for (i=0;i<n;i++) { ringbuffer_add_one(&_dbg_buf, buf[i]); }
}
static void debug_now(void){
  char buf[256];
  int n;
  while((n=ringbuffer_get(&_dbg_buf,buf,sizeof(buf)))>0){
    buf[n]=0;
    puts(buf);
  }
}
#else
#define DEBUG_LATER(...)
#define DEBUG_NOW()
#endif

#define _MAX_MHR_OVERHEAD   (25)

static int _send(netdev2_t *netdev, const struct iovec *vector, unsigned count);
static int _recv(netdev2_t *netdev, void *buf, size_t len, void *info);
static int _init(netdev2_t *netdev);
static void _isr(netdev2_t *netdev);
static int _get(netdev2_t *netdev, netopt_t opt, void *val, size_t max_len);
static int _set(netdev2_t *netdev, netopt_t opt, void *val, size_t len);

const netdev2_driver_t ata8510_driver = {
    .send = _send,
    .recv = _recv,
    .init = _init,
    .isr = _isr,
    .get = _get,
    .set = _set,
};

#define DEBUG_PIN  GPIO_PIN(PA, 11)
static void _irq_handler(void *arg)
{
    netdev2_t *netdev = (netdev2_t *) arg;
    ata8510_t *dev = (ata8510_t *)netdev;

    uint8_t data[37];
    uint8_t status[4];
    uint8_t dataSFIFO[19];
    uint8_t mystate8510, mynextstate8510;
	int i, n;
    int sfifo_len=0, dfifo_rx_len=0;
    uint16_t errorcode;


	dev->interrupts++;
	ata8510_GetEventBytes(dev, status);

	mystate8510 = ata8510_get_state(dev);
	mynextstate8510 = ata8510_get_state_after_tx(dev);

    // SYS_ERR event
    errorcode = 0;
	if (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_SYS_ERR) {
		errorcode = ata8510_read_error_code(dev);
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS_TRX
	    DEBUG_LATER("_isr#%d: SysErr=%d  SSM state=%d\n", dev->interrupts, errorcode>>8, errorcode&0xff);
#endif
        switch (errorcode>>8) {
            case 21: //DEBUG_ERROR_CODE_SFIFO_OVER_UNDER_FLOW
                break;
            default:
		        dev->sys_errors++;
                break;
        }
	}

    // WCOK: preamble sensed
    if (status[ATA8510_EVENTS] & ATA8510_EVENTS_WCOKA) {
		//#if ENABLE_DEBUG && DEBUG_ISR_EVENTS_TRX
				DEBUG_LATER("\n**************************************_isr#%d: WCOKA, state=%d\n", dev->interrupts, mystate8510);
		//#endif
	    switch (mystate8510) {
			case ATA8510_STATE_POLLING:
				dev->busy = 1;
			break;
			
		    default:
				dev->unknown_case++;
		        break;
		}
	}

    // SOTA event
	if (status[ATA8510_EVENTS] & ATA8510_EVENTS_SOTA) {
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS_TRX
        DEBUG_LATER("_isr#%d: SOTA, state=%d\n", dev->interrupts, mystate8510);
#endif
	    switch (mystate8510) {
            case ATA8510_STATE_POLLING:
                // flush RX ringbuffer
                if (!ringbuffer_empty(&dev->rb)) {
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS_TRX
                    DEBUG_LATER(
                        "_isr#%d: RX start, discarding %d stale bytes from buffer\n",
                        dev->interrupts, dev->rb.avail
                    );
#endif
                    ringbuffer_remove(&dev->rb, dev->rb.avail);
                }
                dev->busy = 1;
                break;
        }
    }

    // empty SFIFO 
    if (
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_SFIFO)    || // SFIFO fill event
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_DFIFO_RX) || // DFIFO_RX fill event
	    (status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA)        // EOTA event
    ) {
        switch (mystate8510) {
            case ATA8510_STATE_TX_ON:
                // TODO: in TX mode, SFIFO fills up when preamble is big
                break;
            default:
                sfifo_len = ata8510_ReadFillLevelRSSIFIFO(dev);
                if (sfifo_len>0) {
                    ata8510_ReadRSSIFIFO(dev, sfifo_len, dataSFIFO);
                    for (i=0; i<sfifo_len; i++) {
                        dev->RSSI[ (i + dev->RSSI_len) % sizeof(dev->RSSI) ] = dataSFIFO[i+3];
                    }
                    dev->RSSI_len += sfifo_len;
                    if(dev->RSSI_len > sizeof(dev->RSSI)){
                        dev->RSSI_len = sizeof(dev->RSSI);
                    }
                }
                break;
        }
    }

    // empty DFIFO_RX
	if (
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_DFIFO_RX) || // DFIFO_RX fill event
	    (status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA)        // EOTA event
    ) {
        if(mystate8510==ATA8510_STATE_POLLING){
            dfifo_rx_len = ata8510_ReadFillLevelRxFIFO(dev);
            if (dfifo_rx_len>0) { // there is data to read
                ata8510_ReadRxFIFO(dev, dfifo_rx_len, data);
                for (i=0;i<dfifo_rx_len;i++) { ringbuffer_add_one(&dev->rb, data[i+3]); }
            }
        }
	}

    // empty DFIFO_TX
	if (
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_DFIFO_TX) || // DFIFO_TX fill event
	    (status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA)        // EOTA event
    ) {
        if(mystate8510==ATA8510_STATE_TX_ON){
            n = ata8510_ReadFillLevelTxFIFO(dev);
            if (n < ATA8510_DFIFO_TX_LENGTH) { // DFIFO_TX has free space
                n = ringbuffer_get(&dev->rb, (char *)data, ATA8510_DFIFO_TX_LENGTH - n);
                if (n) {
#if ENABLE_DEBUG & DEBUG_SEND
                    DEBUG_LATER("_isr#%d: send batch %d bytes\n", dev->interrupts, n);
#endif
                    ata8510_WriteTxFifo(dev, n, data);
                }
            }
        }
	}

    // EOTA event
	if (status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA) {
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS_TRX
        DEBUG_LATER("_isr#%d: EOTA, state=%d\n", dev->interrupts, mystate8510);
#endif
	    switch (mystate8510) {
            case ATA8510_STATE_TX_ON:
                // flush TX ringbuffer
                if (!ringbuffer_empty(&dev->rb)) {
                    DEBUG_LATER(
                        "_isr#%d: TX end, discarding %d stale bytes from buffer\n",
                        dev->interrupts, dev->rb.avail
                    );
                    ringbuffer_remove(&dev->rb, dev->rb.avail);
                }

                /* check for more pending TX calls and return to idle state if
                 * there are none */
                assert(dev->pending_tx != 0);
                if ((--dev->pending_tx) == 0) {
                    ata8510_set_state(dev, ATA8510_STATE_IDLE);
                    switch (mynextstate8510) {
                        case ATA8510_STATE_IDLE:
                            break;
                        case ATA8510_STATE_POLLING:
                            ata8510_set_state(dev, ATA8510_STATE_POLLING);
                            break;
                        default:
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS_TRX
                             DEBUG_LATER("_isr#%d: Cannot handle state %d after TX\n", dev->interrupts, mynextstate8510);
#endif
                             break;
                    }
                }
                for(int i=0;i<4;i++){ dev->status[i]=status[i]; }
                netdev->event_callback(netdev, NETDEV2_EVENT_ISR);
                dev->busy = 0;
                break;

		    case ATA8510_STATE_POLLING:
                if (dev->busy) {  // avoid spurious EOTA

                    dev->service = ATA8510_CONFIG_SERVICE(status[ATA8510_CONFIG]);
                    dev->channel = ATA8510_CONFIG_CHANNEL(status[ATA8510_CONFIG]);

                    ata8510_set_state(dev, ATA8510_STATE_IDLE);
                    ata8510_set_state(dev, ATA8510_STATE_POLLING);

                    for(int i=0;i<4;i++){ dev->status[i]=status[i]; }
                    netdev->event_callback(netdev, NETDEV2_EVENT_ISR);
                    dev->busy = 0;
                }
                break;

		    default:
				dev->unknown_case++;
		        break;
        }
    }

#if ENABLE_DEBUG & DEBUG_ISR
    DEBUG_LATER("_isr#%d: state=%d pending_tx=%d busy=%d\n", dev->interrupts, mystate8510, dev->pending_tx, dev->busy);
    DEBUG_LATER(
        "_isr#%d: Get Event Bytes: %02x %02x %02x %02x\n",
        dev->interrupts, dev->status[0], dev->status[1], dev->status[2], dev->status[3]
    );
#endif
#if ENABLE_DEBUG & DEBUG_ISR_EVENTS
    DEBUG_LATER(
        "_isr#%d: SYS_ERR=%d CMD_RDY=%d SYS_RDY=%d SFIFO=%d DFIFO_RX=%d DFIFO_TX=%d\n",
        dev->interrupts,
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_SYS_ERR  ? 1 : 0),
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_CMD_RDY  ? 1 : 0),
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_SYS_RDY  ? 1 : 0),
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_SFIFO    ? 1 : 0),
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_DFIFO_RX ? 1 : 0),
        (status[ATA8510_SYSTEM] & ATA8510_SYSTEM_DFIFO_TX ? 1 : 0)
    );
    printf(
        "_isr#%d: SOTA=%d EOTA=%d WCOKA=%d\n",
        dev->interrupts,
        (status[ATA8510_EVENTS] & ATA8510_EVENTS_SOTA ? 1 : 0),
        (status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA ? 1 : 0),
        (status[ATA8510_EVENTS] & ATA8510_EVENTS_WCOKA ? 1 : 0)
    );
#endif

#if ENABLE_DEBUG & (DEBUG_ISR | DEBUG_PKT_DUMP) == (DEBUG_ISR | DEBUG_PKT_DUMP)
    if (sfifo_len) {
       DEBUG_LATER("_isr#%d: SFIFO len=%d data=[", dev->interrupts, sfifo_len);
       for (i=0; i<sfifo_len; i++) DEBUG_LATER(" %02x", dataSFIFO[i+3]);
       DEBUG_LATER(" ]\n");
       DEBUG_LATER("_isr#%d: RSSI len=%d data=[", dev->interrupts, dev->RSSI_len);
       for (i=0; i<dev->RSSI_len; i++) DEBUG_LATER(" %02x", dev->RSSI[i]);
       DEBUG_LATER(" ]\n");
    }
    if (dfifo_rx_len) {
       DEBUG_LATER("_isr#%d: DFIFO_RX len=%d\n", dev->interrupts, dfifo_rx_len);
    }
#endif

#if ENABLE_DEBUG & DEBUG_ISR
	if ((dev->status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA) && (mystate8510==ATA8510_STATE_TX_ON)) {
       DEBUG_LATER("_isr#%d: new state after TX: %d\n", dev->interrupts, ata8510_get_state(dev));
    }
    DEBUG_LATER("\n");
#endif
}

static int _init(netdev2_t *netdev)
{
    ata8510_t *dev = (ata8510_t *)netdev;

    /* initialise GPIOs */
    gpio_init(dev->params.cs_pin, GPIO_OUT);
    gpio_set(dev->params.cs_pin);
    gpio_init(dev->params.sleep_pin, GPIO_OUT);
    gpio_clear(dev->params.sleep_pin);
    gpio_init(dev->params.reset_pin, GPIO_OUT);
    gpio_set(dev->params.reset_pin);
    gpio_init_int(dev->params.int_pin, GPIO_IN, GPIO_FALLING, _irq_handler, dev);

    // define an hw debug pin
    gpio_init(DEBUG_PIN, GPIO_OUT);
    gpio_clear(DEBUG_PIN);

    ata8510_power_on(dev);
    spi_poweron(dev->params.spi);

    /* test if the SPI is set up correctly and the device is responding */
    if (ata8510_get_device_signature(dev) != ATA8510_PARTNUM){
        DEBUG("[ata8510] error: unable to read correct part number\n");
        return -1;
    }
    ringbuffer_init(&dev->rb, (char *)dev->mem, sizeof(dev->mem));

    DEBUG("[ata8510] init done\n");

#ifdef MODULE_NETSTATS_L2
    memset(&netdev->stats, 0, sizeof(netstats_t));
#endif
    /* reset device to default values and put it into RX state */
    ata8510_reset(dev);

    return 0;
}

static int _send(netdev2_t *netdev, const struct iovec *vector, unsigned count)
{
    ata8510_t *dev = (ata8510_t *)netdev;
    const struct iovec *ptr = vector;
    size_t len = 0;
    uint8_t data[ATA8510_DFIFO_TX_LENGTH];
    int i,n;

// assert the hw debug pin
gpio_set(DEBUG_PIN);
xtimer_usleep(10);
gpio_clear(DEBUG_PIN);

    for (unsigned i = 0; i < count; i++) { len += vector[i].iov_len; }
    /* current packet data too long */
    if (len > ATA8510_MAX_PKT_LENGTH) {
        DEBUG("_send: packet too large (%u byte) to be sent\n", (unsigned)len);
        return -EOVERFLOW;
    }

#if ENABLE_DEBUG & (DEBUG_SEND | DEBUG_PKT_DUMP) == (DEBUG_SEND | DEBUG_PKT_DUMP)
    DEBUG("_send: data=[");
    unsigned c=0;
    for (unsigned i=0; i<count; i++) {
        for (unsigned j=0; j<vector[i].iov_len; j++) {
            if ((c++%16) == 0) DEBUG("\n\t");
            DEBUG(" %02x", ((char *)vector[i].iov_base)[j]);
        }
        DEBUG("\n-----\n\t");
    }
    DEBUG("\n], len=%d\n", c);
#endif

    dev->pending_tx++;
    /* make sure ongoing radio activity is finished */
    i=0;
    while(dev->busy) {
        i++;
        xtimer_usleep(0);
    }
    DEBUG("START busy loops: %d\n", i);

    /* if we arrive here, local radio is believed in idle state
     * then we must perform the CSMA/CA procedure in order to avoid
     * that multiple node in the same broadcast domain, starting to
     * transmit simultaneously as reaction to some broadcat packet,
     * elide each other causing an endless loop of retransmissions
     */
    random_init(_xtimer_now());
    DEBUG("_send: Starting software CSMA/CA....\n");

    int nb = 0, be = conf->min_be;

    while (nb <= conf->max_be) {
        /* delay for an adequate random backoff period */
        uint32_t bp = choose_backoff_period(be, conf);
        DEBUG("_send: backoff: %d period: %d\n", nb, (int)bp);
        xtimer_usleep(bp);

        // check again for local radio being idle
        if (dev->busy == 0) break;

        /* radio is still busy: increment CSMA counters */
        DEBUG("_send: Radio medium busy.\n");
        be++;
        if (be > conf->max_be) {
            be = conf->max_be;
        }
        nb++;
        /* ... and try again if we have no exceeded the retry limit */
    }
    dev->busy=1;

    ata8510_tx_prepare(dev);

    /* load packet data into buffer */
    len = 0;
    for (unsigned i = 0; i < count; i++, ptr++) {
        len = ata8510_tx_load(dev, ptr->iov_base, ptr->iov_len, len);
    }

    // send first bytes of message until FIFO is full
    n = ata8510_ReadFillLevelTxFIFO(dev);
#if ENABLE_DEBUG & DEBUG_SEND
    DEBUG("_send: ReadFillLevelTxFIFO %d bytes\n", n);
#endif
    if (n < ATA8510_DFIFO_TX_LENGTH) { // DFIFO_TX has free space
        n = ringbuffer_get(&dev->rb, (char *)data, ATA8510_DFIFO_TX_LENGTH - n);
        if (n) {
#if ENABLE_DEBUG & DEBUG_SEND
            DEBUG("_send: first batch %d of %d bytes\n", n, len);
#endif
            ata8510_WriteTxFifo(dev, n, data);
        }
    }

    // activate tx mode 
    ata8510_set_state(dev, ATA8510_STATE_TX_ON);

    // wait until trasmission ends
    i=0;
    while(dev->busy) {
        i++;
        xtimer_usleep(0);
    }
    DEBUG("END busy loops: %d\n", i);

#ifdef MODULE_NETSTATS_L2
    netdev->stats.tx_bytes += len;
#endif
	DEBUG_NOW();
    /* return the number of bytes that were sent */
    return (int)len;
}

static int _recv(netdev2_t *netdev, void *buf, size_t len, void *info)
{
    size_t pkt_len;
    uint8_t i;
	int rssicalc;

    ata8510_t *dev = (ata8510_t *)netdev;

    if(ringbuffer_empty(&dev->rb)){ return 0; }
    pkt_len = dev->rb.avail;

    /* just return length when buf == NULL */
    if (buf == NULL) {
        return pkt_len;
    }
    /* not enough space in buf */
    if (pkt_len > len) {
        return -ENOBUFS;
    }
#ifdef MODULE_NETSTATS_L2
    netdev->stats.rx_count++;
    netdev->stats.rx_bytes += pkt_len;
#endif

    /* copy payload */
    ringbuffer_get(&dev->rb, (char *)buf, pkt_len);

#if ENABLE_DEBUG && (DEBUG_RECV | DEBUG_PKT_DUMP)
    DEBUG_LATER(
        "_recv: service=%d, channel=%d, pkt_len=%d, data=[", 
        dev->service, dev->channel, pkt_len
    );
    for (i=0; i<pkt_len; i++) {
        if ((i%16) == 0) DEBUG("\n\t");
        DEBUG_LATER(" %02x", ((char *)buf)[i]);
    }
    DEBUG_LATER("\n]\n");
#endif

    if (info != NULL) {
        netdev2_ieee802154_rx_info_t *radio_info = info;
        radio_info->lqi=0;
        radio_info->rssi=0;
        rssicalc=0;
        if (dev->RSSI_len > 0) {
            for (i=0; i<dev->RSSI_len; i++) { rssicalc+=dev->RSSI[i]; }
            radio_info->rssi = rssicalc/dev->RSSI_len;
#if ENABLE_DEBUG && DEBUG_RECV
            DEBUG_LATER("_recv: RSSI values = %d RSSI = %d \n", dev->RSSI_len, radio_info->rssi);
#endif
        } else {
            DEBUG_LATER("_recv: no RSSI values read\n");
        }
    }
//printf("_recv: %d\n", pkt_len);
    return pkt_len;
}

static int _set_state(ata8510_t *dev, netopt_state_t state)
{
    switch (state) {
        case NETOPT_STATE_SLEEP:
            ata8510_set_state(dev, ATA8510_STATE_IDLE);
            break;
        case NETOPT_STATE_IDLE:
            ata8510_set_state(dev, ATA8510_STATE_POLLING);
            break;
        case NETOPT_STATE_TX:
            ata8510_set_state(dev, ATA8510_STATE_TX_ON);
            break;
        case NETOPT_STATE_RESET:
            ata8510_reset(dev);
            break;
        default:
            return -ENOTSUP;
    }
    return sizeof(netopt_state_t);
}

netopt_state_t _get_state(ata8510_t *dev)
{
    switch (ata8510_get_state(dev)) {
        case ATA8510_STATE_IDLE:
            return NETOPT_STATE_SLEEP;
        case ATA8510_STATE_POLLING:
            return NETOPT_STATE_RX;
        case ATA8510_STATE_TX_ON:
            return NETOPT_STATE_TX;
        default:
            return NETOPT_STATE_IDLE;
    }
}

static int _get(netdev2_t *netdev, netopt_t opt, void *val, size_t max_len)
{
    ata8510_t *dev = (ata8510_t *) netdev;

    if (netdev == NULL) {
        return -ENODEV;
    }

DEBUG("_get: opt=%d\n", opt);

    /* getting these options doesn't require the transceiver at all */
    switch (opt) {
        case NETOPT_MAX_PACKET_SIZE:
            if (max_len < sizeof(int16_t)) {
                return -EOVERFLOW;
            }
            *((uint16_t *)val) = ATA8510_MAX_PKT_LENGTH - _MAX_MHR_OVERHEAD;
            return sizeof(uint16_t);

        case NETOPT_STATE:
            if (max_len < sizeof(netopt_state_t)) {
                return -EOVERFLOW;
            }
            *((netopt_state_t *)val) = _get_state(dev);
            return sizeof(netopt_state_t);

        default:
            /* Can still be handled in second switch */
            break;
    }

    int res;

    if (((res = netdev2_ieee802154_get((netdev2_ieee802154_t *)netdev, opt, val,
                                       max_len)) >= 0) || (res != -ENOTSUP)) {
        return res;
    }

    uint8_t old_state = ata8510_get_state(dev);
    res = 0;

    /* temporarily switch to IDLE state */
    if (old_state != ATA8510_STATE_IDLE) {
        ata8510_assert_awake(dev);
    }

    /* these options require the transceiver to be not sleeping*/
    switch (opt) {
        case NETOPT_IS_CHANNEL_CLR:
            if (ata8510_cca(dev)) {
                *((netopt_enable_t *)val) = NETOPT_ENABLE;
            }
            else {
                *((netopt_enable_t *)val) = NETOPT_DISABLE;
            }
            res = sizeof(netopt_enable_t);
            break;
/*
        case NETOPT_CCA_THRESHOLD:
            if (max_len < sizeof(int8_t)) {
                res = -EOVERFLOW;
            }
            else {
                *((int8_t *)val) = ata8510_get_cca_threshold(dev);
                res = sizeof(int8_t);
            }
            break;
*/
        default:
            res = -ENOTSUP;
    }

    /* go back to original state */
    if (old_state != ATA8510_STATE_IDLE) {
        ata8510_set_state(dev, old_state);
    }

    return res;
}

static int _set(netdev2_t *netdev, netopt_t opt, void *val, size_t len)
{
    ata8510_t *dev = (ata8510_t *) netdev;
    uint8_t old_state = ata8510_get_state(dev);
    int res = -ENOTSUP;

    if (dev == NULL) {
        return -ENODEV;
    }

DEBUG("_set: opt=%d\n", opt);

    /* temporarily switch to IDLE state */
    if (old_state == ATA8510_STATE_IDLE) {
        ata8510_assert_awake(dev);
    }

    switch (opt) {
        case NETOPT_ADDRESS:
            if (len > sizeof(uint16_t)) {
                res = -EOVERFLOW;
            }
            else {
				DEBUG("_set: opt NETOPT_ADDRESS: len: %d val: %04x\n", len, *((uint16_t *)val));
                ata8510_set_addr_short(dev, *((uint16_t *)val));
                /* don't set res to set netdev2_ieee802154_t::short_addr */
            }
            break;

        case NETOPT_ADDRESS_LONG:
            if (len > sizeof(uint64_t)) {
                res = -EOVERFLOW;
            }
            else {
                ata8510_set_addr_long(dev, *((uint64_t *)val));
                /* don't set res to set netdev2_ieee802154_t::long_addr */
            }
            break;

        case NETOPT_NID:
            if (len > sizeof(uint16_t)) {
                res = -EOVERFLOW;
            }
            else {
                ata8510_set_pan(dev, *((uint16_t *)val));
                /* don't set res to set netdev2_ieee802154_t::pan */
            }
            break;

        case NETOPT_SRC_LEN:
        // get/set the address length to choose for the network
        // device's source address as uint16_t in host byte order 
            if (len > sizeof(uint16_t)) {
                res = -EOVERFLOW;
            }
            else {
				DEBUG("_set: TO BE DONE NETOPT_SRC_LEN: len: %d val: %04x\n", len, *((uint16_t *)val));
            }
            break;


//      case NETOPT_CHANNEL:
//          if (len != sizeof(uint16_t)) {
//              res = -EINVAL;
//          }
//          else {
//              uint8_t chan = ((uint8_t *)val)[0];
//              if (chan < ATA8510_MIN_CHANNEL ||
//                  chan > ATA8510_MAX_CHANNEL) {
//                  res = -EINVAL;
//                  break;
//              }
//              ata8510_set_chan(dev, chan);
//              /* don't set res to set netdev2_ieee802154_t::chan */
//          }
//          break;

//      case NETOPT_CHANNEL_PAGE:
//          if (len != sizeof(uint16_t)) {
//              res = -EINVAL;
//          }
//          else {
//              uint8_t page = ((uint8_t *)val)[0];
//              if (page != 0) {
//                  res = -EINVAL;
//              }
//              else {
//                  res = sizeof(uint16_t);
//              }
//          }
//          break;

//      case NETOPT_TX_POWER:
//          if (len > sizeof(int16_t)) {
//              res = -EOVERFLOW;
//          }
//          else {
//              ata8510_set_txpower(dev, *((int16_t *)val));
//              res = sizeof(uint16_t);
//          }
//          break;

        case NETOPT_STATE:
            if (len > sizeof(netopt_state_t)) {
                res = -EOVERFLOW;
            }
            else {
                res = _set_state(dev, *((netopt_state_t *)val));
            }
            break;

//      case NETOPT_AUTOACK:
//          ata8510_set_option(dev, ATA8510_OPT_AUTOACK,
//                               ((bool *)val)[0]);
//          /* don't set res to set netdev2_ieee802154_t::flags */
//          break;

//      case NETOPT_RETRANS:
//          if (len > sizeof(uint8_t)) {
//              res = -EOVERFLOW;
//          }
//          else {
//              ata8510_set_max_retries(dev, *((uint8_t *)val));
//              res = sizeof(uint8_t);
//          }
//          break;

//      case NETOPT_PRELOADING:
//          ata8510_set_option(dev, ATA8510_OPT_PRELOADING,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_PROMISCUOUSMODE:
//          ata8510_set_option(dev, ATA8510_OPT_PROMISCUOUS,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_RX_START_IRQ:
//          ata8510_set_option(dev, ATA8510_OPT_TELL_RX_START,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_RX_END_IRQ:
//          ata8510_set_option(dev, ATA8510_OPT_TELL_RX_END,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_TX_START_IRQ:
//          ata8510_set_option(dev, ATA8510_OPT_TELL_TX_START,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_TX_END_IRQ:
//          ata8510_set_option(dev, ATA8510_OPT_TELL_TX_END,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_CSMA:
//          ata8510_set_option(dev, ATA8510_OPT_CSMA,
//                               ((bool *)val)[0]);
//          res = sizeof(netopt_enable_t);
//          break;

//      case NETOPT_CSMA_RETRIES:
//          if ((len > sizeof(uint8_t)) ||
//              (*((uint8_t *)val) > 5)) {
//              res = -EOVERFLOW;
//          }
//          else if (dev->netdev.flags & ATA8510_OPT_CSMA) {
//              /* only set if CSMA is enabled */
//              ata8510_set_csma_max_retries(dev, *((uint8_t *)val));
//              res = sizeof(uint8_t);
//          }
//          break;

//      case NETOPT_CCA_THRESHOLD:
//          if (len > sizeof(int8_t)) {
//              res = -EOVERFLOW;
//          }
//          else {
//              ata8510_set_cca_threshold(dev, *((int8_t *)val));
//              res = sizeof(int8_t);
//          }
//          break;

        default:
			DEBUG("_set: UNCATCHED: %d ************+\n", opt);
            break;
    }

    /* go back to original state if were not idle and state hasn't been changed */
    if ((old_state != ATA8510_STATE_IDLE) &&
        (opt != NETOPT_STATE)) {
        ata8510_set_state(dev, old_state);
    }

//  if (res == -ENOTSUP) {
//      res = netdev2_ieee802154_set((netdev2_ieee802154_t *)netdev, opt,
//                                   val, len);
//  }

    return res;
}

static void _isr(netdev2_t *netdev)
{
    ata8510_t *dev = (ata8510_t *)netdev;
    uint8_t mystate8510 = ata8510_get_state(dev);

    // EOTA event
	if (dev->status[ATA8510_EVENTS] & ATA8510_EVENTS_EOTA) {
	    switch (mystate8510) {
            case ATA8510_STATE_TX_ON:
                netdev->event_callback(netdev, NETDEV2_EVENT_TX_COMPLETE);
                break;

		    case ATA8510_STATE_POLLING:
                netdev->event_callback(netdev, NETDEV2_EVENT_RX_COMPLETE);
                break;
        }
    }

    DEBUG_NOW();
}





































