#pragma once

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "cobs.h"
#include "dartt.h"
#include "dartt_init.h"
#include "dartt_sync.h"

#define DARTT_READER_BUF_SIZE 512

class DarttReader {
public:
	void init(dartt_sync_t* sync, CommMode mode, Serial* serial,
			  UdpState* udp, TcpState* tcp, std::mutex* tx_mutex);

	void start();
	void stop();

	// Binary frame logger — off by default. Appends raw COBS-delimited
	// read-reply frames verbatim from the serial buffer, no timestamp prefix.
	void open_bin_log(const std::string& path);
	void close_bin_log();
	bool bin_logging_enabled = false;

	// Callers must hold this when reading periph_buf or DarttField values
	// written by the reader thread.
	std::mutex periph_buf_mutex;

private:
	void thread_loop();
	void process_frame();
	int  read_byte(uint8_t& out);  // returns 1 on success, 0 if no data, -1 on error

	std::thread	   thread_;
	std::atomic<bool> running_{false};

	// COBS accumulation — one enc/dec pair, reset between frames
	uint8_t	enc_mem_[DARTT_READER_BUF_SIZE];
	uint8_t	dec_mem_[DARTT_READER_BUF_SIZE];
	cobs_buf_t cobs_enc_{};
	cobs_buf_t cobs_dec_{};

	// Raw wire bytes accumulated during a frame, for the bin logger.
	// Includes the trailing 0x00 delimiter written by process_frame().
	uint8_t raw_frame_[DARTT_READER_BUF_SIZE + 1];
	size_t  raw_frame_len_ = 0;

	dartt_sync_t* sync_	  = nullptr;
	CommMode	  comm_mode_ = COMM_SERIAL;
	Serial*	   serial_	= nullptr;
	UdpState*	 udp_	   = nullptr;
	TcpState*	 tcp_	   = nullptr;

	// Shared with the TX side. Reader does not hold this continuously —
	// it is reserved for coordinating half-duplex (RS485) line arbitration.
	// Full TX-thread design is deferred; see todo/async-read-thread.md.
	std::mutex*   tx_mutex_  = nullptr;

	std::FILE*	bin_log_file_ = nullptr;
};
