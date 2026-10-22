// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic telemetry shared by the spare-DK simulator and host decoder tests.
#pragma once
#include <stdint.h>
#include <string.h>
static inline void sim_be16(uint8_t *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static inline void sim_le16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static inline uint16_t sim_modbus(const uint8_t *p,unsigned n) {
    uint16_t c=0xffff;
    while(n--){c^=*p++;for(unsigned k=0;k<8;k++)c=c&1?(c>>1)^0xa001:c>>1;}
    return c;
}
static inline unsigned ble_bms_sim_frame(unsigned protocol,unsigned command,uint8_t *out) {
    memset(out,0,244);
    if(protocol==1) {
        unsigned length=command==4?8:25;out[0]=0xdd;out[1]=command==4?4:3;out[3]=length;
        if(command==4){for(unsigned k=0;k<4;k++)sim_be16(out+4+k*2,3450);}
        else {
            sim_be16(out+4,1380);sim_be16(out+6,(uint16_t)-1230);
            sim_be16(out+8,7700);sim_be16(out+10,10000);
            out[23]=77;out[24]=3;out[25]=4;out[26]=1;sim_be16(out+27,2981);
        }
        uint16_t sum=0;for(unsigned k=2;k<length+4;k++)sum+=out[k];
        sim_be16(out+length+4,(uint16_t)(0u-sum));out[length+6]=0x77;return length+7;
    }
    if(protocol==2) {
        out[0]=0x7e;out[1]=0xa1;out[2]=0x11;out[5]=70;out[8]=1;out[9]=4;
        for(unsigned k=0;k<4;k++)sim_le16(out+34+k*2,3450);
        sim_le16(out+42,25);sim_le16(out+44,26);sim_le16(out+46,27);
        sim_le16(out+48,1380);sim_le16(out+50,(uint16_t)-123);sim_le16(out+52,77);
        out[56]=out[57]=1;sim_le16(out+76,sim_modbus(out+1,75));out[78]=0xaa;out[79]=0x55;
        return 80;
    }
    out[0]=0xd2;out[1]=3;out[2]=124;
    for(unsigned k=0;k<4;k++)sim_be16(out+3+k*2,3450);
    sim_be16(out+3+64,65);sim_be16(out+3+80,138);sim_be16(out+3+82,29877);
    sim_be16(out+3+84,770);out[3+106]=out[3+107]=1;
    sim_le16(out+127,sim_modbus(out,127));return 129;
}
