/*
 *                                r0ket 1v1
 *
 * A simplistic social leveling game for your r0ket.
 *                                           -- by rtreffer, lvt, uliwitness
 *
 * Basic game semantics:
 * - Walk around and talk to other r0ket users
 * - If
 *   - they are running r0ket 1v1
 *   - you like them
 * - then generate an item
 * - gain more (rare) items to level up
 *
 * Detailed game semantics:
 *
 * R0KET LVL is highly inspired by the ideas of kademlia dht (*1).
 * Please read the paper before you continue reading.
 *
 * R0KET LVL consists of 2 kademlia dhts, one machine generated and one
 * human generated. The machine generated version is updated all the time
 * while you show the R0KET LVL batch, the human generated one is updated
 * whenever you do a person to person exchange.
 *
 * Your dht position is determined by your BEACONID. The number of leading
 * zeros to the closest (machine) known hash is your target level.
 *
 *   ITEM LVL = leading_zero_count ( MY_BEACON_ID xor YOUR_BEACON_ID )
 *
 * Note that this is symetric, thus both participants get an item of the same
 * level. The chance of getting an item at least level n is:
 *
 *   p(item_n) = 1 / (2 ^ n),  for n >= 0
 *
 * This means it will get exponentially difficult to level up (or, vice versa,
 * an item of level 4 is roughly twice as common as an item of level 5).
 *
 * We use the common hack of multiple routing entries for a single bucket
 * and let entries expire after 5 day of inactivity. This means that a dht
 * will only last for a single conference. You loose Items when a bucket
 * gets empty.
 *
 * (*1) http://pdos.csail.mit.edu/~petar/papers/maymounkov-kademlia-lncs.pdf
 */

/*
 *                               screen design
 *
 * ~10 should be the upper limit of items. It coresponds to roughly 1024
 * meat participants. Or 17h if you need only a minute per participant.
 * We arrange items by 2 rows of 5 16x16 items.
 *
 *          0    1    2    3    4    5    6    7    8    9    x10
 *          ------------------------------------------------
 *         |  ******** ******** ******** ******** ********  |  0
 *         |  ******** ******** ******** ******** *      *  |  4
 *         |  ******** ******** ******** ******** *      *  |  8
 *         |  ******** ****** 2 ******** ******** ********  | 12
 *         |                                                | 16
 *         |                                                | 20
 *         |              *   *    **   *  *                | 24
 *         |               * *    *  *  *  *                | 28
 *         |                *     *  *  *  *                | 32
 *         |                *      **    **                 | 36
 *         |                                                | 40
 *         |  [==============>           |   Level 4 / 6 ]  | 44
 *         |                                                | 48
 *         |  ******** ******** ******** ******** ********  | 52
 *         |  *      * *      * *      * *      * *      *  | 56
 *         |  *      * *      * *      * *      * *      *  | 60
 *         |  ******** ******** ******** ******** ********  | 64
 *          ------------------------------------------------
 *
 */

#include <sysinit.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#include "basic/basic.h"
#include "basic/config.h"

#include "lcd/render.h"
#include "lcd/print.h"

#include "funk/nrf24l01p.h"
#include "funk/rftransfer.h"

#include "usetable.h"

/* routing entry */

struct entry {
    uint32_t age;
    uint32_t uid;
};

/* routing table  */

struct entry table[15][4];

/* network buffer */

uint16_t myhash;

/* helper methods */

static void net_scan();
static void draw();
static void blink();
static int  delta(uint16_t, uint16_t);

void ram(void) {
    // wait until no key is pressed
    getInputWaitRelease();
    // basic initialization
    lcdClear();
    // initialize nrf
    struct NRF_CFG config;
    config.nrmacs=1;
    config.maclen[0] = 16;
    config.channel = 81;
    memcpy(config.mac0, "\x01\x02\x03\x02\x01", 5);
    nrf_config_set(&config);
    // initialize the routing table
    for (int i = 0; i < 15; i++) {
        for (int j = 0; j < 4; j++) {
            table[i][j].age = 0;
            table[i][j].uid = 0;
        }
    }
    // compute my hash
    uint32_t myid = GetUUID32();
    myhash = crc16((uint8_t*) &myid, 4);;
    // main loop
    draw();
    do {
        net_scan();
        lcdRefresh();
    } while (getInputRaw() == BTN_NONE);
}

void net_scan() {
    uint8_t buf[16];
    int c;
    int tries = 0;
    while (
        ++tries < 1024 &&
        getInputRaw() == BTN_NONE
    ) {
        if((c = nrf_rcv_pkt_time(64,sizeof(buf),buf)) == 16 &&
           buf[1] == 0x17
        ) {
            int id = uint8ptouint32(&buf[8]);
            uint16_t crc = crc16((uint8_t*) &id, 4);
            int bucket = delta(crc, myhash);
            if (bucket < 0 || bucket >= 15) {
                 lcdPrintInt(bucket);
                 lcdPrintln("!! ERROR !!");
                 lcdRefresh();
                 return;
            }
            // move to front
            int pos;
            for (pos = 0; pos < 3; pos++) {
                if (table[bucket][pos].uid == crc) {
                    break;
                }
            }
            while (pos-- > 0) {
                table[bucket][pos + 1].uid =
                    table[bucket][pos].uid;
                table[bucket][pos + 1].age =
                    table[bucket][pos].age;
            }
            table[bucket][0].uid = crc;
            table[bucket][0].age = 1;
            draw();
        }
    }
    lcdPrintInt(c);
    lcdPrint(" ");
    lcdPrintInt(tries);
    lcdPrintln("");
    lcdRefresh();
}

int delta(uint16_t crc1, uint16_t crc2) {
    if (crc1 == crc2) {
        return 0;
    }
    uint16_t xor = crc1 ^ crc2;
    int result = 15;
    if (xor > 255) {
        xor >>= 8;
        result -= 8;
    }
    if (xor > 15) {
        xor >>= 4;
        result -= 4;
    }
    if (xor > 3) {
        xor >>= 2;
        result -= 2;
    }
    if (xor > 1) {
        xor >>= 1;
        result -= 1;
    }
    return result;
}

void draw() {
    lcdClear();
    lcdPrintIntHex(myhash);
    lcdPrintln("");
    for (int i = 1; i < 9; i++) {
        int c = 0;
        for (int j = 0; j < 4; j++) {
            if (table[i][j].age > 0) {
                c++;
            }
        }
        lcdPrintInt(i);
        lcdPrint(": ");
        lcdPrintInt(c);
        if (c > 0) {
            lcdPrint(" ");
            lcdPrintIntHex(table[i][0].uid);
        }
        lcdPrintln("");
    }
    lcdRefresh();
}

void blink() {
/*
    for (int x=0;x<20;x++) {
        gpioSetValue (RB_LED1, x%2);
        delayms(50);
    }
  lcdPrintln("blink");
    lcdRefresh();
*/
}

void error() {
    lcdPrintln("error");
/*
    for (int x=0;x<20;x++) {
        gpioSetValue (RB_LED2, x%2);
        delayms(50);
    }
*/
}
