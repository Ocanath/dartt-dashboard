#include "dartt_reader.h"

#include <cstring>
#include <ctime>

void DarttReader::init(dartt_sync_t* sync, CommMode mode, Serial* serial,
					   UdpState* udp, TcpState* tcp, std::mutex* tx_mutex)
{
	sync_	  = sync;
	comm_mode_ = mode;
	serial_	= serial;
	udp_	   = udp;
	tcp_	   = tcp;
	tx_mutex_  = tx_mutex;

	cobs_enc_ = { enc_mem_, sizeof(enc_mem_), 0, COBS_ENCODED  };
	cobs_dec_ = { dec_mem_, sizeof(dec_mem_), 0, COBS_DECODED  };
}

void DarttReader::start()
{
	running_ = true;
	thread_  = std::thread(&DarttReader::thread_loop, this);
}

void DarttReader::stop()
{
	running_ = false;
	if (thread_.joinable())
		thread_.join();
	close_bin_log();
}

void DarttReader::open_bin_log(const std::string& path)
{
	close_bin_log();
	bin_log_file_ = std::fopen(path.c_str(), "ab");
}

void DarttReader::close_bin_log()
{
	if (bin_log_file_) {
		std::fclose(bin_log_file_);
		bin_log_file_ = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Thread
// ---------------------------------------------------------------------------

void DarttReader::thread_loop()
{
	while (running_) 
	{
		uint8_t b;
		int r = read_byte(b);
		// if (r < 0)
		// {
		// 	break;   // unrecoverable error — stop thread
		// }
		if (r == 0)	//
		{
			continue;
		}

		// Accumulate raw bytes for the bin logger
		if (raw_frame_len_ < sizeof(raw_frame_))
		{
			raw_frame_[raw_frame_len_++] = b;
		}

		int ret = cobs_stream(b, &cobs_enc_, &cobs_dec_);
		if (ret == COBS_SUCCESS) 
		{
			process_frame();
			// Reset for next frame
			cobs_enc_.length = 0;
			raw_frame_len_   = 0;
		}
		else if (ret == COBS_ERROR_SERIAL_OVERRUN) 
		{
			// Frame was too long for our buffer — discard and resync
			cobs_enc_.length = 0;
			raw_frame_len_   = 0;
		}
		// COBS_STREAMING_IN_PROGRESS is normal mid-frame; all other negatives
		// are also non-fatal — keep draining.
	}
}

// ---------------------------------------------------------------------------
// Frame processing
// ---------------------------------------------------------------------------

void DarttReader::process_frame()
{
	// dartt_frame_to_payload aliases into cobs_dec_.buf — zero-copy
	dartt_buffer_t frame = 
	{
		cobs_dec_.buf,
		cobs_dec_.size,
		cobs_dec_.length
	};

	payload_layer_msg_t pld{};
	uint8_t pld_buf[DARTT_READER_BUF_SIZE];
	dartt_buffer_t pld_msg_buf = { pld_buf, sizeof(pld_buf), 0 };
	pld.msg = pld_msg_buf;

	if (dartt_frame_to_payload(&frame, sync_->msg_type, PAYLOAD_COPY, &pld) != DARTT_PROTOCOL_SUCCESS)
	{
		return;
	}

	// Detect read-reply: READ_WRITE_BITMASK set on index (first two payload bytes)
	if (pld.msg.len < NUM_BYTES_READ_REPLY_OVERHEAD_PLD)
		return;

	uint16_t index_field = (uint16_t)pld.msg.buf[0] | ((uint16_t)pld.msg.buf[1] << 8);
	bool is_read_reply   = (index_field & READ_WRITE_BITMASK) != 0;

	if (!is_read_reply)
		return;

	// --- Bin log: write the raw frame bytes including trailing 0x00 ---
	if (bin_logging_enabled && bin_log_file_ && raw_frame_len_ > 0) {
		// The 0x00 delimiter was the byte that triggered COBS_SUCCESS.
		// raw_frame_ already contains every byte up to and including 0x00
		// because read_byte feeds it before cobs_stream.
		std::fwrite(raw_frame_, 1, raw_frame_len_, bin_log_file_);
	}

	// --- Write to periph_buf ---
	// Cheat: synthesise a misc_read_message_t from the reply length so that
	// dartt_parse_read_reply's length check passes without needing the original
	// outgoing request. See todo/async-read-thread.md Component 1.
	misc_read_message_t synthetic_req{};
	synthetic_req.address   = pld.address;
	synthetic_req.index	 = index_field & ~READ_WRITE_BITMASK;
	synthetic_req.num_bytes = (uint16_t)(pld.msg.len - NUM_BYTES_READ_REPLY_OVERHEAD_PLD);

	{
		std::lock_guard<std::mutex> lock(periph_buf_mutex);
		dartt_parse_read_reply(&pld, &synthetic_req, &sync_->periph_base);
	}

	// TODO (step 3): update Plotter here under plot_mutex
}

// ---------------------------------------------------------------------------
// Transport abstraction
// ---------------------------------------------------------------------------

int DarttReader::read_byte(uint8_t& out)
{
	switch (comm_mode_) 
	{
		case COMM_SERIAL: 
		{
			int n = serial_->read(&out, 1);
			return n;  // Serial::read returns bytes read (0 or 1), negative on error
		}
		case COMM_UDP:
			// TODO: tcs_receive_from(udp_->socket, &out, 1, ...)
			return 0;
		case COMM_TCP:
			// TODO: tcs_receive(tcp_->socket, &out, 1, ...)
			return 0;
	}
	return -1;
}

