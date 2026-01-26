#include "dartt_init.h"


Serial serial;
unsigned char tx_mem[64] = {};
unsigned char rx_dartt_mem[64] = {};
unsigned char rx_cobs_mem[64] = {};


int tx_blocking(unsigned char addr, buffer_t * b, uint32_t timeout)
{
	cobs_buf_t cb = {
		.buf = b->buf,
		.size = b->size,
		.length = b->len,
		.encoded_state = COBS_DECODED
	};
	int rc = cobs_encode_single_buffer(&cb);
	if (rc != 0)
	{
		return rc;
	}
    rc = serial.write(cb.buf, (int)cb.length);
	if(rc == cb.length)
	{
		return DARTT_PROTOCOL_SUCCESS;
	}
	else
	{
		return -1;
	}
}

int rx_blocking(buffer_t * buf, uint32_t timeout)
{
	cobs_buf_t cb_enc =
	{
		.buf = rx_cobs_mem,
		.size = sizeof(rx_cobs_mem),
		.length = 0
	};

    // int rc = serial.read(cb_enc.buf, cb_enc.size);	//implement our own cobs blocking read, similar to hdlc/ppp ~ check
    int rc = serial.read_until_delimiter(cb_enc.buf, cb_enc.size, 0, timeout);

	if (rc >= 0)
	{
		cb_enc.length = rc;	//load encoded length (raw buffer)
	}
	else if (rc == -2)
	{
		return -7;
	}
	else
	{
		return -1;
	}

	cobs_buf_t cb_dec =
	{
		.buf = buf->buf,
		.size = buf->size,
		.length = 0
	};
	rc = cobs_decode_double_buffer(&cb_enc, &cb_dec);
	buf->len = cb_dec.length;	//critical - we are aliasing this read buffer in sync, but must update the length to the cobs decoded value

	if (rc != COBS_SUCCESS)
	{
		return rc;
	}
	else
	{
		return DARTT_PROTOCOL_SUCCESS;
	}
    
}

void init_ds(dartt_sync_t * ds)
{
	ds->address = 0;	//must be mapped
	ds->ctl_base = {};	//must be assigned
	ds->periph_base = {};	//must be assigned
	ds->msg_type = TYPE_SERIAL_MESSAGE;
	ds->tx_buf.buf = tx_mem;
	ds->tx_buf.size = sizeof(tx_mem);	
	ds->tx_buf.len = 0;
	ds->rx_buf.buf = rx_dartt_mem;
	ds->rx_buf.size = sizeof(rx_dartt_mem);	//todo: size this buffer based on the size of the attribute. take size argument and malloc.
	ds->rx_buf.len = 0;
	ds->blocking_tx_callback = &tx_blocking;
	ds->blocking_rx_callback = &rx_blocking;
	ds->timeout_ms = 10;
}