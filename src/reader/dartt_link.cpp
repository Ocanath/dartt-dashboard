#include "dartt_link.h"

#include <cstring>


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
        tx_queue_.push(std::vector<uint8_t>(frame.buf, frame.buf + frame.len));
    }
    tx_cv_.notify_one();
}

void DarttLink::subscribe_region(dartt_mem_t region)
{
    std::lock_guard<std::mutex> lock(subscribed_mutex_);
    subscribed_regions_.push_back(region);
}

void DarttLink::clear_subscriptions()
{
    std::lock_guard<std::mutex> lock(subscribed_mutex_);
    subscribed_regions_.clear();
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


int DarttLink::create_read_request_frame(dartt_mem_t & ctl, read_request_backing_store_t & frame)
{
	assert(ctl_base.size == periph_base.size);
    assert(ctl.buf != NULL && ctl_base.buf != NULL);
	assert(periph_base.buf != NULL);

	frame.len = 0;	//delete first - make sure it's invalidated until we finish

    // Runtime checks for buffer bounds - these could be caused by developer error in ctl configuration
    if(ctl.size == 0)
    {
        return DARTT_ERROR_INVALID_ARGUMENT;
    }
    if (ctl.buf < ctl_base.buf || ctl.buf >= (ctl_base.buf + ctl_base.size))
    {
        return DARTT_ERROR_MEMORY_OVERRUN;
    }
    if (ctl.buf + ctl.size > ctl_base.buf + ctl_base.size)
    {
        return DARTT_ERROR_MEMORY_OVERRUN;
    }
    if (ctl.buf + ctl.size > ctl_base.buf + ctl_base.size)
    {
        return DARTT_ERROR_MEMORY_OVERRUN;
    }
    //ensure the read reply we're requesting won't overrun the read buffer
    size_t nb_overhead_read_reply = NUM_BYTES_READ_REPLY_OVERHEAD_PLD;
    if(msg_type == TYPE_SERIAL_MESSAGE)
    {
		nb_overhead_read_reply += NUM_BYTES_ADDRESS + NUM_BYTES_CHECKSUM;
    }
    else if(msg_type == TYPE_ADDR_MESSAGE)
    {
		nb_overhead_read_reply += NUM_BYTES_CHECKSUM;
    }
	if(ctl.size + nb_overhead_read_reply > target_serbuf_rx_size)
	{
		return DARTT_ERROR_MEMORY_OVERRUN;
	}

    unsigned char misc_address = dartt_get_complementary_address(address);

    int field_index = index_of_field( (void*)(&ctl.buf[0]), (void*)(&ctl_base.buf[0]), ctl_base.size );
    if(field_index < 0)
    {
        return field_index; //negative values are error codes, return if you get negative value
    }
    misc_read_message_t read_msg =
    {
            .address = misc_address,
            .index = (uint16_t)(field_index + base_offset),	//load with offset to the destination
            .num_bytes = (uint16_t)ctl.size
    };

	dartt_buffer_t tx_buf = {
		.buf = frame.mem,
		.size = sizeof(frame.mem),
		.len = 0
	};
    int rc = dartt_create_read_frame(&read_msg, msg_type, &tx_buf);
    if(rc != DARTT_PROTOCOL_SUCCESS)
    {
        return rc;
    }
	cobs_buf_t cobs_tx = {
		.buf = tx_buf.buf,
		.size = tx_buf.size,
		.length = tx_buf.len,
		.encoded_state = COBS_DECODED
	};
	rc = cobs_encode_single_buffer(&cobs_tx);
	if(rc != COBS_SUCCESS)
	{
		return rc;
	}
	frame.len = cobs_tx.length;//finally load length to validate frame
	return rc;
}




void DarttLink::dispatch_read_requests()
{

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
