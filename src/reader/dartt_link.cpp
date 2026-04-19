#include "dartt_link.h"

#include <cstring>

#define NUM_BYTES_COBS_OVERHEAD	2	//we have to tell dartt our serial buffers are smaller than they are, so the COBS layer has room to operate. This allows for functional multiple message handling with write_multi and read_multi for large configs



DarttLink::DarttLink(dartt_mem_t & ctl, dartt_mem_t & periph)
{
	ctl_base = ctl;	//shallow copies, alias these
	periph_base = periph;
	cobs_enc_ = { enc_mem_, sizeof(enc_mem_), 0, COBS_ENCODED };
    cobs_dec_ = { dec_mem_, sizeof(dec_mem_), 0, COBS_DECODED };
	msg_type = TYPE_SERIAL_MESSAGE;
}

DarttLink::~DarttLink()
{

}

void DarttLink::init_serial(int baudrate)
{
    comm_mode = COMM_SERIAL;
	serial.autoconnect(baudrate);
}

void DarttLink::start()
{
    running_      = true;
    read_thread_  = std::thread(&DarttLink::read_loop,  this);
    write_thread_ = std::thread(&DarttLink::write_loop, this);
}

void DarttLink::stop()
{
    running_ = false;
    tx_cv_.notify_all();   // wake write thread so it can exit
    if (read_thread_.joinable())  read_thread_.join();
    if (write_thread_.joinable()) write_thread_.join();
    close_bin_log();
}

void DarttLink::enqueue_frame(dartt_buffer_t & frame)
{
    {
        std::lock_guard<std::mutex> lock(tx_queue_mutex_);
        // tx_queue_.push(std::move(frame));	//TODO: implement this
    }
    tx_cv_.notify_one();
}

void DarttLink::set_read_reply_callback(read_reply_cb_t cb, void* ctx)
{
    on_read_reply_cb_  = cb;
    on_read_reply_ctx_ = ctx;
}

void DarttLink::open_bin_log(const std::string& path)
{
    close_bin_log();
    bin_log_file_ = std::fopen(path.c_str(), "ab");
}

void DarttLink::close_bin_log()
{
    if (bin_log_file_) {
        std::fclose(bin_log_file_);
        bin_log_file_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Read thread
// ---------------------------------------------------------------------------

void DarttLink::read_loop()
{
    std::unique_lock<std::mutex> bus_lock(bus_mutex_, std::defer_lock);
    uint8_t chunk[64];

    while (running_)
    {
        int n = read_bytes(chunk, sizeof(chunk));
        if (n <= 0)
        {
            // Kernel buffer drained — bus is idle, release so TX can send
            if (!is_full_duplex && bus_lock.owns_lock())
			{
				bus_lock.unlock();
			}
        }

        for (int i = 0; i < n; i++)
        {
            uint8_t b = chunk[i];

            // First byte of activity — claim the bus (half-duplex only)
            if (!is_full_duplex && !bus_lock.owns_lock())
            {
				bus_lock.lock();
			}

            int ret = cobs_stream(b, &cobs_enc_, &cobs_dec_);
            if (ret == COBS_SUCCESS)
            {
                process_frame();
                cobs_enc_.length = 0;
            }
            else if (ret == COBS_ERROR_SERIAL_OVERRUN)
            {
                cobs_enc_.length = 0;
            }
            // Hold the lock for the rest of the chunk regardless — more frames may follow
        }
    }
}

// ---------------------------------------------------------------------------
// Write thread
// ---------------------------------------------------------------------------

void DarttLink::write_loop()
{
    while (running_)
    {
        std::vector<uint8_t> frame;
        {
            std::unique_lock<std::mutex> q_lock(tx_queue_mutex_);
            tx_cv_.wait(q_lock, [this]{ return !tx_queue_.empty() || !running_; });
            if (!running_) break;
            frame = std::move(tx_queue_.front());
            tx_queue_.pop();
        }

        if (is_full_duplex)
        {
            send_raw(frame.data(), frame.size());
        }
        else
        {
            // In half-duplex, block until the read thread releases the bus.
            // In gapless streaming mode this will starve — intentional, since
            // the peripheral owns the bus and any TX would cause a collision.
            std::lock_guard<std::mutex> bus(bus_mutex_);
            send_raw(frame.data(), frame.size());
        }
    }
}

// ---------------------------------------------------------------------------
// Frame processing (read thread)
// ---------------------------------------------------------------------------

void DarttLink::process_frame()
{
    dartt_buffer_t frame = {
        cobs_dec_.buf,
        cobs_dec_.size,
        cobs_dec_.length
    };

    payload_layer_msg_t pld{};
    uint8_t pld_buf[DARTT_LINK_BUF_SIZE];
    dartt_buffer_t pld_msg_buf = { pld_buf, sizeof(pld_buf), 0 };
    pld.msg = pld_msg_buf;

    if (dartt_frame_to_payload(&frame, msg_type, PAYLOAD_COPY, &pld) != DARTT_PROTOCOL_SUCCESS)
	{
		return;
	}

    if (pld.msg.len < NUM_BYTES_READ_REPLY_OVERHEAD_PLD)
        return;

    uint16_t index_field = (uint16_t)pld.msg.buf[0] | ((uint16_t)pld.msg.buf[1] << 8);
    if (!(index_field & READ_WRITE_BITMASK))
	{
		return;
	}

    if (bin_logging_enabled && bin_log_file_ && cobs_enc_.length > 0)
    {
		std::fwrite(cobs_enc_.buf, 1, cobs_enc_.length, bin_log_file_);
	}

    // Synthesise original_msg from reply length to satisfy dartt_parse_read_reply's
    // length check without needing the original outgoing request on hand.
    misc_read_message_t synthetic_req{};
    synthetic_req.address   = pld.address;
    synthetic_req.index     = index_field & ~READ_WRITE_BITMASK;
    synthetic_req.num_bytes = (uint16_t)(pld.msg.len - NUM_BYTES_READ_REPLY_OVERHEAD_PLD);

    {
        std::lock_guard<std::mutex> lock(periph_buf_mutex);
        dartt_parse_read_reply(&pld, &synthetic_req, &periph_base);
    }

    if (on_read_reply_cb_ != NULL)
	{
		on_read_reply_cb_(&periph_base, on_read_reply_ctx_);
	}
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void DarttLink::send_raw(const uint8_t* data, size_t len)
{
    switch (comm_mode)
    {
        case COMM_SERIAL:
            serial.write(const_cast<uint8_t*>(data), (int)len);
            break;
        case COMM_UDP:
            // TODO: tcs_send_to(udp_->socket, data, len, ...)
            break;
        case COMM_TCP:
            // TODO: tcs_send(tcp_->socket, data, len, ...)
            break;
    }
}

int DarttLink::read_bytes(uint8_t* buf, int max)
{
    switch (comm_mode)
    {
        case COMM_SERIAL:
            return serial.read(buf, max);
        case COMM_UDP:
            // TODO: tcs_receive_from(udp_.socket, buf, max, ...)
            return 0;
        case COMM_TCP:
            // TODO: tcs_receive(tcp_.socket, buf, max, ...)
            return 0;
    }
    return -1;
}
