#ifndef DARTT_INIT_H
#define DARTT_INIT_H

#include "serial.h"
#include "cobs.h"
#include "dartt.h"
#include "dartt_sync.h"


extern Serial serial;
extern unsigned char tx_mem[64];
extern unsigned char rx_dartt_mem[64];
extern unsigned char rx_cobs_mem[64];

void init_ds(dartt_sync_t * ds);


#endif