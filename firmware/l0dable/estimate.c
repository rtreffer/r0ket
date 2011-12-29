/*
 *                                r0ket estimate
 *
 * An attemt to estimate the number of r0kets around you. Simple move around
 * to find the total number of r0kets :-)
 *
 */

#include "basic/basic.h"
#include "basic/config.h"

#include "lcd/render.h"
#include "lcd/print.h"

#include "funk/nrf24l01p.h"
#include "funk/rftransfer.h"

#include "usetable.h"

#define HASHCOUNT 16

static uint32_t rol32(uint32_t, int);
static void sha_simple(uint32_t*, char*, uint32_t);
static void sha_transform(uint32_t*, const unsigned char*, uint32_t*);
static void sha_init(uint32_t*);
static void draw();
static void net_scan();
static void hash(uint32_t,uint16_t*);

uint32_t prefix[HASHCOUNT];
uint16_t myhashes[HASHCOUNT];
uint16_t best[HASHCOUNT];

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
    // initialize prefixes
    prefix[ 0] = 0x6220ddaf;
    prefix[ 1] = 0x4f94e8fc;
    prefix[ 2] = 0x540aa8ab;
    prefix[ 3] = 0x728fefad;
    prefix[ 4] = 0xc5a14b8e;
    prefix[ 5] = 0xa0ac8310;
    prefix[ 6] = 0xf20b27dc;
    prefix[ 7] = 0xd539d677;
    prefix[ 8] = 0x145f8491;
    prefix[ 9] = 0xbce8d16e;
    prefix[10] = 0x4a5efcc1;
    prefix[11] = 0xc4da23cf;
    prefix[12] = 0x90c7e131;
    prefix[13] = 0x9e19ea94;
    prefix[14] = 0x7f20073a;
    prefix[15] = 0x5366be65;
    // compute my hash
    uint32_t myid = GetUUID32();
    hash(myid, myhashes);
    for(int i = 0; i < HASHCOUNT; i++) {
        best[i] = 0xffff ^ myhashes[i];
    }
    // main loop
    draw();
    do {
        net_scan();
        lcdRefresh();
    } while (getInputRaw() == BTN_NONE);
}

void net_scan() {
    uint8_t buf[16];
    uint16_t hashes[5];
        for(int i = 0; i < 5; i++) {
        hashes[i] = 0xffff ^ myhashes[i];
    }
    int c;
    int tries = 0;
    while (
        ++tries < 1024 &&
        getInputRaw() == BTN_NONE
    ) {
        if((c = nrf_rcv_pkt_time(64,sizeof(buf),buf)) == 16 &&
           buf[1] == 0x17
        ) {
            uint32_t id = uint8ptouint32(&buf[8]);
            hash(id, hashes);
            for (int i = 0; i < HASHCOUNT; i++) {
                if ((hashes[i] ^ myhashes[i]) < (best[i] ^ myhashes[i])) {
                    best[i] = hashes[i];
                    draw();
                }
            }
        }
    }
}

void hash(uint32_t uid, uint16_t* hashes) {
    uint32_t digest[5];
    uint32_t message[2];

    for (int i = 0; i < HASHCOUNT; i++) {
        message[0] = prefix[i];
        message[1] = uid;
        sha_simple(digest, message, 8);
        digest[0] ^= digest[1] ^ digest[2] ^ digest[3] ^ digest[4];
        hashes[i] = (digest[0] ^ (digest[1] >> 16)) & 0xffff;
    }
}

void draw() {
    uint32_t error = 0;
    lcdClear();
    for (int i = 0; i < HASHCOUNT; i++) {
        lcdPrintInt(best[i] ^ myhashes[i]);
        if (i % 3 == 1) {
            lcdNl();
        } else {
            lcdPrint(" ");
        }
        error += best[i] ^ myhashes[i];
    }
    int estimate = (HASHCOUNT * 0xffff) / error;
    lcdNl();
    lcdPrintInt(estimate);
    lcdPrintln(" r0kets");
    lcdRefresh();
}

static void sha_simple(uint32_t* digest, char* message, uint32_t len) {
    // use the sha1 transform without padding
    uint32_t tmp[80];
    char buf[64];
    int offset;
    sha_init(digest);
    for(offset = 0; offset + 64 < len; offset += 64) {
        memcpy(buf, message + offset - 8, 64);
        memset(tmp, 0, sizeof(tmp));
        sha_transform(digest, buf, tmp);
    }
    memcpy(buf, message + offset, len - offset);
    buf[len - offset] = 128;
    memset(buf + 1 + (len - offset), 0, 63 - (len-offset));
    memset(tmp, 0, sizeof(tmp));
    sha_transform(digest, buf, tmp);
}

/*
 * SHA transform algorithm, originally taken from code written by
 * Peter Gutmann, and placed in the public domain.
 */

static  uint32_t
rol32(uint32_t word, int shift)
{
	return (word << shift) | (word >> (32 - shift));
}

/* The SHA f()-functions.  */

#define f1(x,y,z)   (z ^ (x & (y ^ z)))		/* x ? y : z */
#define f2(x,y,z)   (x ^ y ^ z)			/* XOR */
#define f3(x,y,z)   ((x & y) + (z & (x ^ y)))	/* majority */

/* The SHA Mysterious Constants */

#define K1  1518500249
#define K2  1859775393
#define K3  2400959708
#define K4  3395469782

// #define K1  0x5A827999L			/* Rounds  0-19: sqrt(2) * 2^30 */
// #define K2  0x6ED9EBA1L			/* Rounds 20-39: sqrt(3) * 2^30 */
// #define K3  0x8F1BBCDCL			/* Rounds 40-59: sqrt(5) * 2^30 */
// #define K4  0xCA62C1D6L			/* Rounds 60-79: sqrt(10) * 2^30 */

/**
 * sha_transform - single block SHA1 transform
 *
 * @digest: 160 bit digest to update
 * @data:   512 bits of data to hash
 * @W:      80 words of workspace (see note)
 *
 * This function generates a SHA1 digest for a single 512-bit block.
 * Be warned, it does not handle padding and message digest, do not
 * confuse it with the full FIPS 180-1 digest algorithm for variable
 * length messages.
 *
 * Note: If the hash is security sensitive, the caller should be sure
 * to clear the workspace. This is left to the caller to avoid
 * unnecessary clears between chained hashing operations.
 */
static void sha_transform(uint32_t *digest, const unsigned char *in, uint32_t *W)
{
	uint32_t a, b, c, d, e, t, i;

	for (i = 0; i < 16; i++) {
		int ofs = 4 * i;

		/* word load/store may be unaligned here, so use bytes instead */
		W[i] =
			(in[ofs+0] << 24) |
			(in[ofs+1] << 16) |
			(in[ofs+2] << 8) |
			 in[ofs+3];
	}

	for (i = 0; i < 64; i++)
		W[i+16] = rol32(W[i+13] ^ W[i+8] ^ W[i+2] ^ W[i], 1);

	a = digest[0];
	b = digest[1];
	c = digest[2];
	d = digest[3];
	e = digest[4];

	for (i = 0; i < 20; i++) {
		t = f1(b, c, d) + K1 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 40; i ++) {
		t = f2(b, c, d) + K2 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 60; i ++) {
		t = f3(b, c, d) + K3 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	for (; i < 80; i ++) {
		t = f2(b, c, d) + K4 + rol32(a, 5) + e + W[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}

	digest[0] += a;
	digest[1] += b;
	digest[2] += c;
	digest[3] += d;
	digest[4] += e;
}

/**
 * sha_init - initialize the vectors for a SHA1 digest
 * @buf: vector to initialize
 */
static void sha_init(uint32_t *buf)
{
    buf[0] = 1732584193;
    buf[1] = 4023233417;
    buf[2] = 2562383102;
    buf[3] =  271733878;
    buf[4] = 3285377520;
}

