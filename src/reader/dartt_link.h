#pragma once

#include <atomic>
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

class DarttLink {
public:
    void init(dartt_sync_t* sync, CommMode mode, Serial* serial,
              UdpState* udp, TcpState* tcp);

    void start();   // spawn read + write threads
    void stop();    // signal both + join

    // Push a complete pre-encoded COBS frame onto the TX queue.
    // DarttLink treats it as opaque bytes — caller handles dartt framing.
    void enqueue_frame(std::vector<uint8_t> frame);

    // Half-duplex (default): bus_mutex_ arbitrates read vs write threads.
    // Full-duplex: both threads skip bus_mutex_ entirely.
    bool is_full_duplex = false;

    // Streaming mode: accept unsolicited read-reply frames from the peripheral.
    bool streaming_mode = false;

    // Binary frame logger — off by default. Appends raw COBS-delimited
    // read-reply frames verbatim from the wire, no timestamp prefix.
    bool bin_logging_enabled = false;
    void open_bin_log(const std::string& path);
    void close_bin_log();

    // Callers must hold this when reading periph_buf or DarttField values
    // written by the read thread.
    std::mutex periph_buf_mutex;

private:
    void read_loop();
    void write_loop();
    void process_frame();
    void send_raw(const uint8_t* data, size_t len);
    int  read_byte(uint8_t& out);

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

    // COBS accumulation (read thread only — no sharing, no lock needed)
    uint8_t    enc_mem_[DARTT_LINK_BUF_SIZE];
    uint8_t    dec_mem_[DARTT_LINK_BUF_SIZE];
    cobs_buf_t cobs_enc_{};
    cobs_buf_t cobs_dec_{};

    // Raw wire bytes staged per-frame for the bin logger
    uint8_t    raw_frame_[DARTT_LINK_BUF_SIZE + 1];
    size_t     raw_frame_len_ = 0;

    dartt_sync_t* sync_      = nullptr;
    CommMode      comm_mode_ = COMM_SERIAL;
    Serial*       serial_    = nullptr;
    UdpState*     udp_       = nullptr;
    TcpState*     tcp_       = nullptr;

    std::FILE*    bin_log_file_ = nullptr;
};
