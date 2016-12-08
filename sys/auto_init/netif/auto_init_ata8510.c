/*
 * Copyright (C) 2016 Antonio Galea <antonio.galea@gmail.com>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 *
 */

/*
 * @ingroup auto_init_gnrc_netif
 * @{
 *
 * @file
 * @brief   Auto initialization for ata8510 network interfaces
 *
 * @author  Antonio Galea <antonio.galea@gmail.com>
 */

#ifdef MODULE_ATA8510

#include "board.h"
#include "net/gnrc/netdev2.h"
#include "net/gnrc/netdev2/ieee802154.h"
#include "net/gnrc.h"

#include "ata8510.h"
#include "ata8510_params.h"

#define ENABLE_DEBUG (0)
#include "debug.h"

/**
 * @brief   Define stack parameters for the MAC layer thread
 * @{
 */
#define ATA8510_MAC_STACKSIZE     (1600)
#define ATA8510_MAC_PRIO          (THREAD_PRIORITY_MAIN - 4)

#define ATA8510_NUM (sizeof(ata8510_params) / sizeof(ata8510_params[0]))

static ata8510_t ata8510_devs[ATA8510_NUM];
static gnrc_netdev2_t gnrc_adpt[ATA8510_NUM];
static char _ata8510_stacks[ATA8510_MAC_STACKSIZE][ATA8510_NUM];

void auto_init_ata8510(void)
{
    for (unsigned i = 0; i < ATA8510_NUM; i++) {
        const ata8510_params_t *p = &ata8510_params[i];
        int res;

        DEBUG("Initializing ATA8510 radio at SPI_%i\n", p->spi);
        ata8510_setup(&ata8510_devs[i], (ata8510_params_t*) p);
        res = gnrc_netdev2_ieee802154_init(&gnrc_adpt[i],
                                           (netdev2_ieee802154_t *)&ata8510_devs[i]);

        if (res < 0) {
            DEBUG("Error initializing ATA8510 radio device!\n");
        }
        else {
            gnrc_netdev2_init(_ata8510_stacks[i],
                              ATA8510_MAC_STACKSIZE,
                              ATA8510_MAC_PRIO,
                              "ata8510",
                              &gnrc_adpt[i]);
        }
    }
}
#else
typedef int dont_be_pedantic;
#endif /* MODULE_ATA8510 */

/** @} */
