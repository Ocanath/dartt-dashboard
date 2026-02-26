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

struct TcpState {
	TcsSocket socket;
	char ip[64];
	uint16_t port;
	bool connected;
};

enum CommMode {
	COMM_SERIAL = 0,
	COMM_UDP = 1,
	COMM_TCP = 2
};

extern Serial serial;
extern CommMode comm_mode;
extern UdpState udp_state;
extern TcpState tcp_state;
extern unsigned char tx_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_dartt_mem[SERIAL_BUFFER_SIZE];
extern unsigned char rx_cobs_mem[SERIAL_BUFFER_SIZE];

void init_ds(dartt_sync_t * ds);
bool udp_connect(UdpState* state);
void udp_disconnect(UdpState* state);
bool tcp_connect(TcpState* state);
void tcp_disconnect(TcpState* state);

#endif