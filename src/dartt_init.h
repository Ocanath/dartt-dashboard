#ifndef DARTT_INIT_H
#define DARTT_INIT_H

#include "serial.h"
#include "cobs.h"
#include "dartt.h"
#include "dartt_sync.h"

#define SERIAL_BUFFER_SIZE 32

extern Serial serial;
extern unsigned char tx_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_dartt_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_cobs_mem[SERIAL_BUFFER_SIZE];

void init_ds(dartt_sync_t * ds);


#endif