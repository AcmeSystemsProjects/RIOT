/*
 * Copyright (C) 2016 Antonio Galea <antonio.galea@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser General
 * Public License v2.1. See the file LICENSE in the top level directory for more
 * details.
 */

/**
 * @defgroup    drivers_ata8510 ATA8510 driver
 * @ingroup     drivers_netdev_netdev2
 *
 * This module contains a driver for the radio device Atmel ATA8510.
 *
 * @{
 *
 * @file
 * @brief       Interface definition for ATA8510 driver
 *
 * @author      Antonio Galea <antonio.galea@gmail.com>
 * @author      Roberto Asquini <bobasquins@gmail.com>
 */

#ifndef ATA8510_H_
#define ATA8510_H_

#include <stdint.h>
#include "ringbuffer.h"

#include "board.h"
#include "periph/spi.h"
#include "periph/gpio.h"
#include "net/netdev2.h"
#include "net/netdev2/ieee802154.h"
#include "net/gnrc/nettype.h"
#include "sema.h"
#include "semaphore.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Maximum possible packet size in byte
 */
#define ATA8510_MAX_PKT_LENGTH  (127)
#define ATA8510_DFIFO_TX_LENGTH (32)
#define ATA8510_DFIFO_RX_LENGTH (32)
#define ATA8510_SFIFO_LENGTH    (16)
/**
 * @brief   Default addresses used if the CPUID module is not present
 * @{
 */
#define ATA8510_DEFAULT_ADDR_SHORT    (0x0230)
#define ATA8510_DEFAULT_ADDR_LONG     (0x1222334455667788)
/** @} */

/**
 * @brief struct holding all params needed for device initialization
 */
typedef struct ata8510_params {
    spi_t spi;              /**< SPI bus the device is connected to */
    spi_speed_t spi_speed;  /**< SPI speed to use */
    gpio_t cs_pin;          /**< GPIO pin connected to chip select */
    gpio_t int_pin;         /**< GPIO pin connected to the interrupt pin */
    gpio_t sleep_pin;       /**< GPIO pin connected to the sleep pin */
    gpio_t reset_pin;       /**< GPIO pin connected to the reset pin */
} ata8510_params_t;

/**
 * @brief   Device descriptor for ATA8510 radio devices
 *
 * @extends netdev2_ieee802154_t
 */
typedef struct {
    netdev2_ieee802154_t netdev;            /**< netdev2 parent struct */
    /**
     * @brief   device specific fields
     * @{
     */
    ata8510_params_t params;              /**< parameters for initialization */
    uint8_t state;                          /**< current state of the radio */
    uint8_t idle_state;                     /**< state to return to after sending */
    uint8_t pending_tx;                     /**< keep track of pending TX calls
                                                 this is required to know when to
                                                 return to @ref ata8510_t::idle_state */

    uint8_t status[4];
    uint8_t busy;

    uint8_t mem[ATA8510_MAX_PKT_LENGTH];
    ringbuffer_t rb;
    uint8_t service;
    uint8_t channel;
    uint8_t RSSI[16];
    uint8_t RSSI_len;
    int interrupts;
    int sys_errors; 
    int unknown_case;
    int blocked;
    sem_t s_send;

    /** @} */
} ata8510_t;

/**
 * @brief   Setup an AT86RF2xx based device state
 *
 * @param[out] dev          device descriptor
 * @param[in]  params       parameters for device initialization
 */
void ata8510_setup(ata8510_t *dev, const ata8510_params_t *params);

/**
 * @brief   Trigger a hardware reset and configure radio with default values
 *
 * @param[in] dev           device to reset
 */
void ata8510_reset(ata8510_t *dev);

/**
 * @brief   Trigger a clear channel assessment
 *
 * @param[in] dev           device to use
 *
 * @return                  true if channel is clear
 * @return                  false if channel is busy
 */
bool ata8510_cca(ata8510_t *dev);

/**
 * @brief   Get the short address of the given device
 *
 * @param[in] dev           device to read from
 *
 * @return                  the currently set (2-byte) short address
 */
uint16_t ata8510_get_addr_short(ata8510_t *dev);

/**
 * @brief   Set the short address of the given device
 *
 * @param[in] dev           device to write to
 * @param[in] addr          (2-byte) short address to set
 */
void ata8510_set_addr_short(ata8510_t *dev, uint16_t addr);

/**
 * @brief   Get the configured long address of the given device
 *
 * @param[in] dev           device to read from
 *
 * @return                  the currently set (8-byte) long address
 */
uint64_t ata8510_get_addr_long(ata8510_t *dev);

/**
 * @brief   Set the long address of the given device
 *
 * @param[in] dev           device to write to
 * @param[in] addr          (8-byte) long address to set
 */
void ata8510_set_addr_long(ata8510_t *dev, uint64_t addr);

/**
 * @brief   Read device ROM parameters
 *
 * @param[in] dev           device to read from
 * @param[out] data         (6-byte) output data
 */
void ata8510_get_version_flash(ata8510_t *dev, uint8_t *data);

/**
 * @brief   Read device ROM signature
 *
 * @param[in] dev           device to read from
 *
 * @return                  the device ROM signature
 */
uint8_t ata8510_get_device_signature(ata8510_t *dev);



/**
 * @brief Switch to polling mode with VCO tuning enabled and starting with polling configuration 0.
 *
 * @param[in] dev           device to read from
 *
 * @return                  event status byte
 */
uint8_t ata8510_SetPollingMode(ata8510_t *dev);

/**
 * @brief Switch to idle mode of the RF transceiver
 *
 * @param[in] dev           device to read from
 *
 * @return                  event status byte
 */
uint8_t ata8510_SetIdleMode(ata8510_t *dev);

/**
 * @brief Switch to TX mode of the RF transceiver
 *
 * @param[in] dev           device to write to
 *
 * @return                  event status byte
 */
uint8_t ata8510_SetTXMode(ata8510_t *dev, uint8_t modeTx);

/**
 * @brief   Read device event 4 bytes
 *
 * @param[in] dev           device to read from
 * @param[out] data         (4-byte) output data
 *
 * @return                  the device 4 event bytes
 */
void ata8510_GetEventBytes(ata8510_t *dev, uint8_t *data);

/**
 * @brief   Read Fill Level of Rx FIFO
 *
 * @param[in] dev           device to read from
 *
 * @return                  Rx FIFO fill level
 */
uint8_t ata8510_ReadFillLevelRxFIFO(ata8510_t *dev);

/**
 * @brief   Read Fill Level of Tx FIFO
 *
 * @param[in] dev           device to read from
 *
 * @return               	Tx FIFO fill level
 */
uint8_t ata8510_ReadFillLevelTxFIFO(ata8510_t *dev);

/**
 * @brief   Read Fill Level of RSSI FIFO
 *
 * @param[in] dev           device to read from
 *
 * @return                  RSSI FIFO fill level
 */
uint8_t ata8510_ReadFillLevelRSSIFIFO(ata8510_t *dev);

/**
 * @brief   Read device RSSI FIFO
 *
 * @param[in] dev           device to read from
 * @param[in] len           num of values bytes to be read
 * @param[out] data         (len+3 bytes) events.system byte + events.events byte + data (len bytes)
 *
 * @return                  the device 4 event bytes
 */
void ata8510_ReadRSSIFIFO(ata8510_t *dev, uint8_t len, uint8_t *data);

/**
 * @brief   translate RSSI in dBM for 8510 at 868 MHz
 *
 * @param[in] rssi           strength of signal received in rssi units
 *
 * @return                  strength of signal received in dBm units
 */
int ata8510_calc_dbm(uint8_t rssi);

/**
 * @brief   Read device event 4 bytes
 *
 * @param[in] dev           device to read from
 * @param[in] len           num of values bytes to be read
 * @param[out] data         (len+3 bytes) events.system byte + events.events byte + data (len bytes)
 *
 * @return                  the device 4 event bytes
 */
void ata8510_ReadRxFIFO(ata8510_t *dev, uint8_t len, uint8_t *data);

/**
 * @brief   Write SRAM / Register
 *
 * @param[in] dev           device to read from
 * @param[in] addr          address (16 bits) from where to write
 * @param[in] data          (1 byte) data to be written
 *
 * @return                  -
 */
void ata8510_write_sram_register(ata8510_t *dev, uint16_t addr, uint8_t data);

/**
 * @brief   Read SRAM / Register
 * @param[in] dev           device to read from
 * @param[in] addr          address (16 bits) of SRAM / register to read
 *
 * @return                  the SRAM / Register wanted
 */
uint8_t ata8510_read_sram_register(ata8510_t *dev, uint16_t addr);


/**
 * @brief   Start RSSI Measurement
 *
 * @param[in] dev           device to read from
 * @param[in] service       service to check
 * @param[in] channel       channel to check
 *
 * @return                  -
 */
void ata8510_StartRSSI_Measurement(ata8510_t *dev, uint8_t service, uint8_t channel);

/**
 * @brief   Get RSSI Value
 *
 * @param[in] dev           device to read from
 * @param[out] data         (4 bytes) events.system byte + events.events byte + RSSI avg + RSSI peak
 *
 * @return                  the 4 data bytes
 */
void ata8510_GetRSSI_Value(ata8510_t *dev, uint8_t *data);













/**
 * @brief   Get the configured channel number of the given device
 *
 * @param[in] dev           device to read from
 *
 * @return                  the currently set channel number
 */
uint8_t ata8510_get_chan(ata8510_t *dev);

/**
 * @brief   Set the channel number of the given device
 *
 * @param[in] dev           device to write to
 * @param[in] chan          channel number to set
 */
void ata8510_set_chan(ata8510_t *dev, uint8_t chan);

/**
 * @brief   Get the configured channel page of the given device
 *
 * @param[in] dev           device to read from
 *
 * @return                  the currently set channel page
 */
uint8_t ata8510_get_page(ata8510_t *dev);

/**
 * @brief   Set the channel page of the given device
 *
 * @param[in] dev           device to write to
 * @param[in] page          channel page to set
 */
void ata8510_set_page(ata8510_t *dev, uint8_t page);

/**
 * @brief   Get the configured PAN ID of the given device
 *
 * @param[in] dev           device to read from
 *
 * @return                  the currently set PAN ID
 */
uint16_t ata8510_get_pan(ata8510_t *dev);

/**
 * @brief   Set the PAN ID of the given device
 *
 * @param[in] dev           device to write to
 * @param[in] pan           PAN ID to set
 */
void ata8510_set_pan(ata8510_t *dev, uint16_t pan);

/**
 * @brief   Get the configured transmission power of the given device [in dBm]
 *
 * @param[in] dev           device to read from
 *
 * @return                  configured transmission power in dBm
 */
int16_t ata8510_get_txpower(ata8510_t *dev);

/**
 * @brief   Set the transmission power of the given device [in dBm]
 *
 * If the device does not support the exact dBm value given, it will set a value
 * as close as possible to the given value. If the given value is larger or
 * lower then the maximal or minimal possible value, the min or max value is
 * set, respectively.
 *
 * @param[in] dev           device to write to
 * @param[in] txpower       transmission power in dBm
 */
void ata8510_set_txpower(ata8510_t *dev, int16_t txpower);

/**
 * @brief   Get the maximum number of retransmissions
 *
 * @param[in] dev           device to read from
 *
 * @return                  configured number of retransmissions
 */
uint8_t ata8510_get_max_retries(ata8510_t *dev);

/**
 * @brief   Set the maximum number of retransmissions
 *
 * This setting specifies the number of attempts to retransmit a frame, when it
 * was not acknowledged by the recipient, before the transaction gets cancelled.
 * The maximum value is 7.
 *
 * @param[in] dev           device to write to
 * @param[in] max           the maximum number of retransmissions
 */
void ata8510_set_max_retries(ata8510_t *dev, uint8_t max);

/**
 * @brief   Get the maximum number of channel access attempts per frame (CSMA)
 *
 * @param[in] dev           device to read from
 *
 * @return                  configured number of retries
 */
uint8_t ata8510_get_csma_max_retries(ata8510_t *dev);

/**
 * @brief   Set the maximum number of channel access attempts per frame (CSMA)
 *
 * This setting specifies the number of attempts to access the channel to
 * transmit a frame. If the channel is busy @p retries times, then frame
 * transmission fails.
 * Valid values: 0 to 5, -1 means CSMA disabled
 *
 * @param[in] dev           device to write to
 * @param[in] retries       the maximum number of retries
 */
void ata8510_set_csma_max_retries(ata8510_t *dev, int8_t retries);

/**
 * @brief   Set the min and max backoff exponent for CSMA/CA
 *
 * - Maximum BE: 0 - 8
 * - Minimum BE: 0 - [max]
 *
 * @param[in] dev           device to write to
 * @param[in] min           the minimum BE
 * @param[in] max           the maximum BE
 */
void ata8510_set_csma_backoff_exp(ata8510_t *dev, uint8_t min, uint8_t max);

/**
 * @brief   Set seed for CSMA random backoff
 *
 * @param[in] dev           device to write to
 * @param[in] entropy       11 bit of entropy as seed for random backoff
 */
void ata8510_set_csma_seed(ata8510_t *dev, uint8_t entropy[2]);

/**
 * @brief   Get the CCA threshold value
 *
 * @param[in] dev           device to read value from
 *
 * @return                  the current CCA threshold value
 */
int8_t ata8510_get_cca_threshold(ata8510_t *dev);

/**
 * @brief   Set the CCA threshold value
 *
 * @param[in] dev           device to write to
 * @param[in] value         the new CCA threshold value
 */
void ata8510_set_cca_threshold(ata8510_t *dev, int8_t value);

/**
 * @brief   Enable or disable driver specific options
 *
 * @param[in] dev           device to set/clear option flag for
 * @param[in] option        option to enable/disable
 * @param[in] state         true for enable, false for disable
 */
void ata8510_set_option(ata8510_t *dev, uint16_t option, bool state);


/* Possible ata8510 states */
typedef enum {
    ATA8510_STATE_IDLE = 0,		/* 0: IDLE state No TRX, waiting for commands */
    ATA8510_STATE_TX_ON,		/* 1: Transmission is ongoing */
    ATA8510_STATE_POLLING,		/* 2: POLLING state */
//  ATA8510_STATE_RX_ON,		/* 3: Reception in progress (for payloads more than 32 bytes) */
//  ATA8510_STATE_RSSIMEAS		/* 4: RSSI Measurement */
} ata8510_state_t;


/**
 * @brief   Set the state of the given device (trigger a state change)
 *
 * @param[in] dev           device to change state of
 * @param[in] state         the targeted new state
 */
void ata8510_set_state(ata8510_t *dev, uint8_t state);

/**
 * @brief   Get the state of the given device 
 *
 * @param[in] dev           device to change state of
 *
 * @return                  the current state
 */
uint8_t ata8510_get_state(ata8510_t *dev);

/**
 * @brief   Set the wanted state of the given device after the end of the current tx 
 *          ( it will be set by the callback function after the end of the Tx) 
 *
 * @param[in] dev           device to change state of
 * @param[in] state         the targeted new state
 */
void ata8510_set_state_after_tx(ata8510_t *dev, uint8_t state);

/**
 * @brief   Get the desired state of the given device after a transmission 
 *          ( it will be set by the callback function after the end of the Tx) 
 *
 * @param[in] dev           device to change state of
 *
 * @return                  the current state
 */
uint8_t ata8510_get_state_after_tx(ata8510_t *dev);

/**
 * @brief   Reset the internal state machine to TRX_OFF mode.
 *
 * This will force a transition to TRX_OFF regardless of whether the transceiver
 * is currently busy sending or receiving. This function is used to get back to
 * a known state during driver initialization.
 *
 * @param[in] dev           device to operate on
 */
void ata8510_reset_state_machine(ata8510_t *dev);

/**
 * @brief   Prepare for sending of data
 *
 * This function puts the given device into the TX state, so no receiving of
 * data is possible after it was called.
 *
 * @param[in] dev            device to prepare for sending
 */
void ata8510_tx_prepare(ata8510_t *dev);

/**
 * @brief   Load chunks of data into the transmit buffer of the given device
 *
 * @param[in] dev           device to write data to
 * @param[in] data          buffer containing the data to load
 * @param[in] len           number of bytes in @p buffer
 *
 * @return                  offset + number of bytes written
 */
size_t ata8510_tx_load(ata8510_t *dev, uint8_t *data, size_t len,
                         size_t offset);

/**
 * @brief   Trigger sending of data previously loaded into transmit buffer
 *
 * @param[in] dev           device to trigger
 */
void ata8510_tx_exec(ata8510_t *dev);

/**
 * @brief   Read Error Code (to be used after a SYS_ERR set bit in first byte of events.system)
 *
 * @param[in] dev           device to read error code
 *
 *  * @return               16 bits System Error (first byte) and SSM state machine state (second byte)   
 */
uint16_t ata8510_read_error_code(ata8510_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* ATA8510_H_ */
/** @} */
