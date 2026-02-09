#ifndef DARTT_INIT_H
#define DARTT_INIT_H

#include "serial.h"
#include "cobs.h"
#include "dartt.h"
#include "dartt_sync.h"
#include "tinycsocket.h"

#define SERIAL_BUFFER_SIZE 32

struct UdpState {
	TcsSocket socket;
	char ip[64];
	uint16_t port;
	bool connected;
};

extern Serial serial;
extern bool use_udp;
extern UdpState udp_state;
extern unsigned char tx_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_dartt_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_cobs_mem[SERIAL_BUFFER_SIZE];

void init_ds(dartt_sync_t * ds);
bool udp_connect(UdpState* state);
void udp_disconnect(UdpState* state);

#endif