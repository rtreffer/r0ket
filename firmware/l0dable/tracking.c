#include <sysinit.h>
#include <string.h>

#include "basic/basic.h"
#include "basic/config.h"

#include "lcd/render.h"
#include "lcd/print.h"

#include "funk/nrf24l01p.h"
#include "usetable.h"

/**************************************************************************/
/* simplistic fork of the people c0d to show nearby beacon ids            */
/**************************************************************************/

void ram(void) {
    struct NRF_CFG config;
    uint8_t buf[16];

    config.nrmacs=1;
    config.maclen[0] = 16;
    config.channel = 81;
    memcpy(config.mac0, "\x01\x02\x03\x02\x01", 5);
    nrf_config_set(&config);
    lcdClear();
    lcdPrintln("Rockets nearby:");
    //lcdPrint("nearby:");
    lcdRefresh();
    do{
        if( nrf_rcv_pkt_time(64,sizeof(buf),buf) == 16 ){
            if( buf[1] == 0x17){
                int seq = uint8ptouint32(&buf[4]);
                int  id = uint8ptouint32(&buf[8]);
                lcdPrintIntHex(id);
                lcdPrintln("");
            }
            lcdRefresh();
        } else {
            lcdPrintln("!!");
        }
    }while ((getInputRaw())==BTN_NONE);
}
