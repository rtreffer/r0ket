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
#include "lcd/image.h"
#include "lcd/print.h"
#include "filesystem/ff.h"

#include "funk/nrf24l01p.h"
#include "funk/rftransfer.h"

#include "usetable.h"

#define HASHCOUNT 18
#define IMAGES 10
#define PATCHES 10

/* routing entry */

struct entry {
    uint32_t age;
    uint32_t uid;
};

/* routing table  */

struct entry table[15 * 4];
struct entry human[15 * 2];

/* global data */

char images[IMAGES][13] = {
    "TROLFACE.LCD",
    "ASTLEY.LCD",
    "DISCORD.LCD",
    "DAFFY.LCD",
    "MUSHROOM.LCD",
    "DALEK.LCD",
    "SPIDEY.LCD",
    "ATAT.LCD",
    "START.LCD",
    "NYAN_CAT.LCD"
};

uint16_t myhash;
uint16_t best[HASHCOUNT];
uint16_t dhtEstimate;
uint8_t buf[16];
uint8_t *patches;

/* helper methods */

static void net_scan();
static void draw();
static void blink();
static int  delta(uint16_t, uint16_t);
static uint32_t rol32(uint32_t, int);
static void draw();
static void net_scan();
static void hash_multi(uint32_t,uint16_t*);
static uint16_t hash(uint32_t);
static void lvlup(uint16_t);
static void estimate();

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
            table[i * 4 + j].age = table[i * 4 + j].uid = 0;
        }
        for (int j = 0; j < 4; j++) {
            human[i * 2 + j].age = human[i * 2 + j].uid = 0;
        }
    }

    // initialize the estimate
    dhtEstimate = 0;

    // compute my hash
    uint32_t myid = GetUUID32();
    myhash = hash(myid);
    for(int i = 0; i < HASHCOUNT; i++) {
        best[i] = 0xffff;
    }
    // main loop
    draw();
    do {
        net_scan();
        lcdRefresh();
        draw();
    } while (getInputRaw() == BTN_NONE);
}

void net_scan() {
    int tries = 0;
    while (
        ++tries < 1024 &&
        getInputRaw() == BTN_NONE
    ) {
        if(nrf_rcv_pkt_time(64,sizeof(buf),buf) == 16 &&
           buf[1] == 0x17
        ) {
            int id = uint8ptouint32(&buf[8]);
            uint16_t crc = hash(id);
            int bucket = delta(crc, myhash);
            // update estimate
            estimate(id);
            // move to front
            int pos;
            for (pos = 0; pos < 3; pos++) {
                if (table[bucket * 4 + pos].uid == crc) {
                    break;
                }
            }
            while (pos-- > 0) {
                table[bucket * 4 + pos + 1].uid =
                    table[bucket * 4 + pos].uid;
                table[bucket * 4 + pos + 1].age =
                    table[bucket * 4 + pos].age;
            }
            table[bucket * 4].uid = crc;
            table[bucket * 4].age = 1;
            // compare against the human table
            pos = 99;
            int total = 0;
            for (int i = 0; i < 2; i++) {
                if (human[bucket * 2 + i].uid == crc) {
                    pos = i;
                    break;
                }
                if (human[bucket * 2 + i].age > 0) {
                    total++;
                }
            }
            if (pos == 99 && total < 4) {
                lvlup(crc);
            }
        }
    }
}

void estimate(uint32_t uid) {
     uint16_t myhashes[HASHCOUNT];
     uint16_t old = dhtEstimate;
     uint32_t myid = GetUUID32();
     hash_multi(myid, &myhashes);

     uint16_t h[HASHCOUNT];
     uint32_t error = 0;
     hash_multi(uid, &h);
     for(int i = 0; i < HASHCOUNT; i++) {
         if ((h[i] ^ myhashes[i]) < best[i]) {
             best[i] = (h[i] ^ myhashes[i]);
         }
         error += best[i];
     }
     dhtEstimate = (HASHCOUNT * 0xffff) / error;

     if (old != dhtEstimate) {
         draw();
     }
}

int delta(uint16_t crc1, uint16_t crc2) {
    if (crc1 == crc2) {
        return 0;
    }
    return 15 - bitcount(crc1 ^ crc2);
}

int bitcount(uint32_t num) {
     for (int i = 0; i < 32; i++) {
         if ((num >> i) == 0) {
             return i - 1;
         }
     }
     return 32;
}

void printLine(uint8_t byte, uint8_t x, uint8_t y) {
    for (int i = 0; i < 8; i++) {
        lcdSetPixel(x, y + i, (byte >> i) & 1);
    }
}

void printPatch(uint8_t patch, uint8_t x, uint8_t y) {
    FIL file;
    UINT readbytes;
    uint8_t bytes[2];
    f_open(&file, "PATCHES.BIN", FA_OPEN_EXISTING|FA_READ);
    f_lseek(&file, patch * (16 * 16 / 8));
    for (int i = 0; i < 16; i++) {
        f_read(&file, &bytes, 2, &readbytes);
        printLine(bytes[0], x + i, y + 8);
        printLine(bytes[1], x + i, y);
    }
    f_close(&file);
}

void draw() {
    // compute string bounds
    int dx  = 0;
    int dy  = 0;
    int lr1 = 0;
    int lr2 = 0;
    dx  = DoString(0,0,GLOBAL(nickname));
    lr1 = DoInt(0,0,dhtEstimate);
    lr2 = DoString(0,0," r0kets");

    // clear
    lcdClear();
    lcdRefresh();

    // draw patches
    int i;
    int current = 0;
    for (i = 0; i < 10; i++) {
        int c = 0;
        for (int j = 0; j < 2; j++) {
            if (human[i * 2 + j].age > 0) {
                c++;
            }
        }
        if (c == 0) {
            printPatch(0, (i % 5) * 18 + 3, (i / 5) * (RESY - 16));
        } else {
            printPatch(i + 1, (i % 5) * 18 + 3, (i / 5) * (RESY - 16));
            if (current == i) {
                current++;
            }
        }
    }

    // draw text
    dx = (RESX - dx) / 2;
    dy = (RESY - getFontHeight()) / 2;
    DoString(dx,dy,GLOBAL(nickname));
    dx = (RESX - lr1 - lr2) / 2;
    dy -= getFontHeight() + (getFontHeight()) / 2;
    dx = DoInt(dx, dy, dhtEstimate);
    DoString(dx, dy, " r0kets");

    // draw progress bar
    int max = bitcount(dhtEstimate);
    if (current > max) {
        current = max;
    }
    uint32_t limit = current;
    limit *= (RESX - 10);
    int y = dy + getFontHeight() * 3;
    for (uint32_t x = 5; x < RESX - 5; x++) {
        lcdSetPixel(x, y, 1);
        lcdSetPixel(x, y + 7, 1);
        int c = 0;
        if ((x - 5) * max <= limit) {
            c = 1;
        }
        for (int k = 1; k < 7; k++) {
            lcdSetPixel(x, y + k, c);
        }
    }

    lcdDisplay();
}

void perform_lvlup(uint16_t hash) {
    int bucket = delta(hash, myhash);

    lcdClear();
    if (bucket < IMAGES) {
        if (lcdLoadImage(images[bucket]) == -1) {
            lcdPrintln("ERROR LOADING");
            lcdPrintln(images[bucket]);
        }
    } else {
        if (lcdLoadImage(images[IMAGES - 1]) == -1) {
            lcdPrintln("ERROR LOADING");
            lcdPrintln(images[IMAGES - 1]);
        }
    }
    lcdRefresh();
    lcdDisplay();

    int pos = 3;
    while (pos-- > 0) {
        human[bucket * 2 + pos + 1].uid = human[bucket * 2 + pos].uid;
        human[bucket * 2 + pos + 1].age = human[bucket * 2 + pos].age;
    }
    human[bucket * 2].uid = hash;
    human[bucket * 2].age = 1;

    int x = 0;
    while (x++ < 20) {
        gpioSetValue (RB_LED1, x%2);
        delayms(50);
    }
    gpioSetValue (RB_LED1, 0);

    getInputWaitRelease();

    draw();
}

void lvlup(uint16_t hash) {
    int x = 0;
    while ((x++ < 40) && (getInputRaw() == BTN_NONE)) {
        gpioSetValue (RB_LED1, x%2);
        delayms(50);
    }
    gpioSetValue (RB_LED1, 0);
    if (x < 40) {
        perform_lvlup(hash);
    }
}

void blink() {
    for (int x=0;x<20;x++) {
        gpioSetValue (RB_LED1, x%2);
        delayms(50);
    }
}

// simplified hashing of beacon ids

uint16_t hash(uint32_t uid) {
    uint8_t buf[4];
    buf[0] = uid >> 24;
    buf[1] = uid >> 16;
    buf[2] = uid >>  8;
    buf[3] = uid;
    return crc16(buf, 4);
}

void hash_multi(uint32_t uid, uint16_t* hashes) {
    for (int i = 0; i < HASHCOUNT; i++) {
        uint32_t prefix = i;
        for (int j = 0; j < 32; j++) {
            unsigned char c = images[(i + j) % IMAGES][(i + j) % 13];
            if (c != 0) {
                prefix = prefix * 31 + images[(i + j) % IMAGES][(i + j) % 13];
            }
        }
        hashes[i] = hash(uid ^ prefix);
    }
}

