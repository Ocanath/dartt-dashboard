#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "cobs.h"
#include "dartt.h"
#include "dartt_init.h"
#include "dartt_sync.h"

#define DARTT_LINK_BUF_SIZE 512

#define NUM_BYTES_COBS_OVERHEAD	2	//we have to tell dartt our serial buffers are smaller than they are, so the COBS layer has room to operate. This allows for functional multiple message handling with write_multi and read_multi for large configs



class DarttLink
{
public:

	enum
	{
		COMM_SERIAL = 0,
		COMM_UDP = 1,
		COMM_TCP = 2
	};




	DarttLink(dartt_mem_t & ctl, dartt_mem_t & periph);
	~DarttLink();

	void init_serial(int baudrate);

    void start();   // spawn read + write threads
    void stop();    // signal both + join


	//function to dispatch write frames into the write queue based on a slice. Emulates write_multi dispatcher
	int enqueue_writes(dartt_mem_t & ctl_slice);
	void enqueue_write_frame(std::vector<uint8_t> & frame);

    // Subscribe a memory region for automatic read polling by the write thread.
    // Coalescing is the caller's responsibility — DarttLink has no field knowledge.
    void subscribe_region(dartt_mem_t region);
	void build_read_requests();
    void clear_subscriptions();

    // Half-duplex (default): bus_mutex_ arbitrates read vs write threads.
    // Full-duplex: both threads skip bus_mutex_ entirely.
    bool is_full_duplex = false;


    // Binary frame logger — off by default. Appends raw COBS-delimited
    // read-reply frames verbatim from the wire, no timestamp prefix.
    bool bin_logging_enabled = false;
    void open_bin_log(const std::string& path);
    void close_bin_log();

    // Called from the read thread once per decoded read-reply, after periph_base_
    // has been updated. Fired under periph_buf_mutex — do not re-acquire it.
    // Sentinel: nullptr (default) — no callback.
    typedef void (*read_reply_cb_t)(const dartt_mem_t* periph, void* user_ctx);
    void set_read_reply_callback(read_reply_cb_t cb, void* ctx);

    // Callers must hold this when reading periph_buf or DarttField values
    // written by the read thread.
    std::mutex periph_buf_mutex;

	//dartt sync things
	unsigned char address;	 // Target peripheral address
	dartt_mem_t ctl_base;			// Bounding region of controller control structure
	dartt_mem_t periph_base;		 // Bounding region of shadow copy structure
	uint16_t base_offset;			//offset into the true peripheral blob. Applied when the dartt_sync_t is indexing into a larger blob, with unknown surrounding structure. Should be set to 0 in most situations
	serial_message_type_t msg_type;

	int      comm_mode;

	//hardware interfaces
    Serial       serial;
    UdpState     udp;
    TcpState     tcp;

private:


    void read_loop();
    void write_loop();
    int process_frame();
    void dispatch_read_requests(std::unique_lock<std::mutex>& bus_lock);
    void send_raw(const uint8_t* data, size_t len);
    int  read_bytes(uint8_t* buf, int max);

	int create_read_request_frame(dartt_mem_t & ctl, std::vector<uint8_t> & frame);
	int create_write_frame(dartt_mem_t & ctl, std::vector<uint8_t> & frame);

	int enqueue_read_requests(dartt_mem_t & ctl_region);

	size_t target_serbuf_rx_size = 32;	//TODO: make a setter/getter for this? or set on init
	size_t target_serbuf_tx_size = 32;	//TODO: make a setter/getter for this? or set on init

	std::thread        read_thread_;
    std::thread        write_thread_;
    std::atomic<bool>  running_{false};

    // Half-duplex bus arbitration. Read thread holds this for the duration of
    // each incoming frame; write thread acquires before send_raw().
    // Intentional starvation in half-duplex streaming mode — correct behaviour.
    // Ignored entirely when is_full_duplex == true.
    std::mutex         bus_mutex_;

    // TX queue — write thread blocks here when idle (zero CPU).
    std::queue<std::vector<uint8_t>> tx_queue_;
    std::mutex                       tx_queue_mutex_;
    std::condition_variable          tx_cv_;

	std::vector<std::vector<uint8_t>> read_request_list_;
    std::mutex                        read_request_mutex_;
    size_t                            read_request_index_    = 0;
    int                               read_request_timeout_ms = 10;
    bool                              awaiting_reply_        = false;
    std::chrono::steady_clock::time_point last_request_time_;

    std::vector<dartt_mem_t> subscribed_regions_;

    // COBS accumulation (read thread only — no sharing, no lock needed)
    uint8_t    enc_mem_[DARTT_LINK_BUF_SIZE];
    uint8_t    dec_mem_[DARTT_LINK_BUF_SIZE];
    cobs_buf_t cobs_enc_{};
    cobs_buf_t cobs_dec_{};

    read_reply_cb_t on_read_reply_cb_  = nullptr;
    void*           on_read_reply_ctx_ = nullptr;

    std::FILE*    bin_log_file_ = nullptr;
};
