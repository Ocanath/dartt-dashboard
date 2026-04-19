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

void DarttLink::enqueue_write_frame(std::vector<uint8_t> & frame)
{
	std::lock_guard<std::mutex> lock(tx_queue_mutex_);
	tx_queue_.push(std::move(frame));	//move is more efficient than pushing frame directly-transfers ownership of the heap memory to the tx_queue_
}

/*
This is dartt_write_multi except that it enqueues stuffed frames
*/
int DarttLink::enqueue_writes(dartt_mem_t & ctl_slice)
{
	size_t nbytes_writemsg_overhead = 0;
    if(msg_type == TYPE_SERIAL_MESSAGE)
    {
		nbytes_writemsg_overhead = (NUM_BYTES_ADDRESS + NUM_BYTES_INDEX + NUM_BYTES_CHECKSUM);	//serial message writes have the maximum overhead, 5 bytes
    }
    else if(msg_type == TYPE_ADDR_MESSAGE)
    {
		nbytes_writemsg_overhead = (NUM_BYTES_INDEX + NUM_BYTES_CHECKSUM);	//if inherently addressed, 4 bytes
    }
    else if(msg_type == TYPE_ADDR_CRC_MESSAGE)
    {
		nbytes_writemsg_overhead = NUM_BYTES_INDEX;	//if inherently addressed and error checked, only two bytes
    }
    else
    {
		return DARTT_ERROR_INVALID_ARGUMENT;
    }

	//add cobs overhead. This is an insertion not present in write_multi
	nbytes_writemsg_overhead += NUM_BYTES_COBS_OVERHEAD;

	if(target_serbuf_rx_size < nbytes_writemsg_overhead + sizeof(int32_t)) 	//for completeness, due to DARTT indexing every 4 bytes, you must at minimum be able to write out one full 4 byte word for complete write access
	{
		return DARTT_ERROR_MEMORY_OVERRUN;
	}


	size_t wsize = target_serbuf_rx_size - nbytes_writemsg_overhead;
	wsize -= wsize % sizeof(int32_t);	//must make sure every chunkified write is 32bit aligned due to dartt indexing

	int num_undersized_writes = (int)(ctl_slice.size / wsize);
	int i = 0;
	for(i = 0; i < num_undersized_writes; i++)
	{
		dartt_mem_t ctl_chunk =
        {
            .buf = ctl_slice.buf + wsize * i,
            .size = wsize,
        };
		// int rc = dartt_ctl_write(&ctl_chunk, psync);
		std::vector<uint8_t> frame;
		int rc = create_write_frame(ctl_chunk, frame);
		if(rc != DARTT_PROTOCOL_SUCCESS)
		{
			return rc;
		}
		enqueue_write_frame(frame);
	}


	size_t last_write_pld_size = ctl_slice.size % wsize;
	if(last_write_pld_size != 0)			//last write
	{
		dartt_mem_t ctl_last_chunk =
		{
			.buf = ctl_slice.buf + wsize * i,
			.size = last_write_pld_size,
		};
		std::vector<uint8_t> frame;
		int rc = create_write_frame(ctl_last_chunk, frame);
		if(rc != DARTT_PROTOCOL_SUCCESS)
		{
			return rc;
		}
		enqueue_write_frame(frame);
	}


    tx_cv_.notify_one();
	return DARTT_PROTOCOL_SUCCESS;
}

void DarttLink::subscribe_region(dartt_mem_t region)
{
    subscribed_regions_.push_back(region);
}

void DarttLink::clear_subscriptions()
{
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
            if (bus_lock.owns_lock())
			{
				bus_lock.unlock();
			}

			std::chrono::steady_clock::time_point cur_time = std::chrono::steady_clock::now();
            bool timed_out = awaiting_reply_ &&
                (cur_time - last_request_time_) >
                std::chrono::milliseconds(read_request_timeout_ms);
			if(timed_out)
			{
				std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(cur_time.time_since_epoch());
				printf("Timeout %lld\n", (long long)ms.count() );
			}
            if (!awaiting_reply_ || timed_out)
			{
				dispatch_read_requests(bus_lock);
			}
        }

        for (int i = 0; i < n; i++)
        {
            uint8_t b = chunk[i];

            if (!bus_lock.owns_lock())
				bus_lock.lock();

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

/** @brief Returns a cobs encoded frame
 *
 */
int DarttLink::create_write_frame(dartt_mem_t & ctl, std::vector<uint8_t> & frame)
{
	assert(ctl.buf != NULL && ctl_base.buf != NULL);


    // Runtime checks for buffer bounds - these could be caused by developer error in ctl configuration
    if (ctl.buf < ctl_base.buf || ctl.buf >= (ctl_base.buf + ctl_base.size)) {
        return DARTT_ERROR_INVALID_ARGUMENT;
    }
    if (ctl.buf + ctl.size > ctl_base.buf + ctl_base.size)
	{
        return DARTT_ERROR_MEMORY_OVERRUN;
    }

    int field_index = index_of_field( (void*)(&ctl.buf[0]), (void*)(&ctl_base.buf[0]), ctl_base.size );
    if(field_index < 0)
    {
        return field_index; //negative values are error codes, return if you get negative value
    }

	size_t framesize = 0;
	framesize+=NUM_BYTES_ADDRESS;
	framesize+=NUM_BYTES_INDEX;
	framesize+=ctl.size;
	framesize+=NUM_BYTES_CHECKSUM;
	framesize+=NUM_BYTES_COBS_OVERHEAD;
	frame.resize(framesize);

    unsigned char misc_address = dartt_get_complementary_address(address);
    //write then read the word in question
    misc_write_message_t write_msg =
    {
            .address = misc_address,
            .index = (uint16_t)(field_index + base_offset),
            .payload = {
                    .buf = ctl.buf,
                    .size = ctl.size,
                    .len = ctl.size
            }
    };

	dartt_buffer_t tx_buf = {
		.buf = frame.data(),
		.size = frame.size(),
		.len = 0
	};
    int rc = dartt_create_write_frame(&write_msg, msg_type, &tx_buf);
    if(rc != DARTT_PROTOCOL_SUCCESS)
    {
        return rc;	//return empty vector if fail
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
	//resize again to cobs_tx.length???
	frame.resize(cobs_tx.length);
	return rc;
}

int DarttLink::create_read_request_frame(dartt_mem_t & ctl, std::vector<uint8_t> & frame)
{
	assert(ctl_base.size == periph_base.size);
    assert(ctl.buf != NULL && ctl_base.buf != NULL);
	assert(periph_base.buf != NULL);

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
    size_t nb_overhead_read_reply = NUM_BYTES_READ_REPLY_OVERHEAD_PLD;
    if(msg_type == TYPE_SERIAL_MESSAGE)
    {
		nb_overhead_read_reply += NUM_BYTES_ADDRESS + NUM_BYTES_CHECKSUM;
    }
    else if(msg_type == TYPE_ADDR_MESSAGE)
    {
		nb_overhead_read_reply += NUM_BYTES_CHECKSUM;
    }
	if(ctl.size + nb_overhead_read_reply > target_serbuf_tx_size)
	{
		return DARTT_ERROR_MEMORY_OVERRUN;
	}

    unsigned char misc_address = dartt_get_complementary_address(address);

    int field_index = index_of_field( (void*)(&ctl.buf[0]), (void*)(&ctl_base.buf[0]), ctl_base.size );
    if(field_index < 0)
    {
        return field_index;
    }
    misc_read_message_t read_msg =
    {
            .address = misc_address,
            .index = (uint16_t)(field_index + base_offset),
            .num_bytes = (uint16_t)ctl.size
    };

	size_t framesize = NUM_BYTES_ADDRESS + NUM_BYTES_INDEX + NUM_BYTES_NUMWORDS_READREQUEST + NUM_BYTES_CHECKSUM + NUM_BYTES_COBS_OVERHEAD;
	frame.resize(framesize);

	dartt_buffer_t tx_buf = {
		.buf = frame.data(),
		.size = frame.size(),
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
	frame.resize(cobs_tx.length);
	return rc;
}


int DarttLink::enqueue_read_requests(dartt_mem_t & ctl)
{
	assert(ctl_base.buf != NULL && periph_base.buf != NULL);
	assert(ctl_base.buf != periph_base.buf);	//basic sanity check - the master and shadow copy can't point to the same memory

	if(ctl_base.size != periph_base.size)
	{
		return DARTT_ERROR_MEMORY_OVERRUN;
	}
    if(!(ctl.buf >= ctl_base.buf && ctl.buf < ctl_base.buf + ctl_base.size))
    {
        return DARTT_ERROR_MEMORY_OVERRUN;
    }
    size_t nbytes_read_overhead = NUM_BYTES_READ_REPLY_OVERHEAD_PLD;  //
    if(msg_type == TYPE_SERIAL_MESSAGE)
    {
		nbytes_read_overhead += (NUM_BYTES_ADDRESS + NUM_BYTES_CHECKSUM);
    }
    else if(msg_type == TYPE_ADDR_MESSAGE)
    {
        nbytes_read_overhead += NUM_BYTES_CHECKSUM;
    }
    else if(msg_type != TYPE_ADDR_CRC_MESSAGE)
    {
        return DARTT_ERROR_INVALID_ARGUMENT;
    }

	if(target_serbuf_tx_size < nbytes_read_overhead + sizeof(int32_t))
	{
		return DARTT_ERROR_MEMORY_OVERRUN;
	}
	size_t rsize = target_serbuf_tx_size - nbytes_read_overhead;
    rsize -= rsize % sizeof(uint32_t); //after making sure the dartt framing bytes are removed, you must ensure that the read size is 32 bit aligned for index_of_field

    int num_full_reads_required = (int)(ctl.size/rsize); 
    int i = 0;
    for(i = 0; i < num_full_reads_required; i++)
    {
        dartt_mem_t ctl_chunk = 
        {
            .buf = ctl.buf + rsize * i,
            .size = rsize,
        };
        // int rc = dartt_ctl_read(&ctl_chunk, psync);
		std::vector<uint8_t> frame;
		int rc = create_read_request_frame(ctl_chunk, frame);
        if(rc != DARTT_PROTOCOL_SUCCESS)
        {
            return rc;
        }
		read_request_list_.push_back(std::move(frame));
    }
	size_t last_read_size = ctl.size % rsize;
	if(last_read_size != 0)
	{
		dartt_mem_t ctl_last_chunk = 
		{
			.buf = ctl.buf + rsize * i,
			.size = last_read_size,
		};
		std::vector<uint8_t> frame;
		int rc = create_read_request_frame(ctl_last_chunk, frame);
        if(rc != DARTT_PROTOCOL_SUCCESS)
        {
            return rc;
        }
		read_request_list_.push_back(std::move(frame));
	}
	return DARTT_PROTOCOL_SUCCESS;
}



void DarttLink::build_read_requests()
{
	std::lock_guard<std::mutex> lock(read_request_mutex_);
	read_request_list_.clear();
	for(dartt_mem_t region : subscribed_regions_)
	{
		enqueue_read_requests(region);
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

    awaiting_reply_ = false;

    if (on_read_reply_cb_ != NULL)
	{
		on_read_reply_cb_(&periph_base, on_read_reply_ctx_);
	}
}

void DarttLink::dispatch_read_requests(std::unique_lock<std::mutex>& bus_lock)
{

    {
        std::lock_guard<std::mutex> q_lock(tx_queue_mutex_);
        if (!tx_queue_.empty())
            return;
    }

    std::lock_guard<std::mutex> rr_lock(read_request_mutex_);
    if (read_request_list_.empty())
        return;

    const std::vector<uint8_t>& frame =
        read_request_list_[read_request_index_ % read_request_list_.size()];
    read_request_index_++;

	bus_lock.lock();   // re-acquire — held until reply arrives, blocking write thread
	send_raw(frame.data(), frame.size());
	// intentionally do NOT unlock — bus stays reserved for the incoming reply

    awaiting_reply_    = true;
    last_request_time_ = std::chrono::steady_clock::now();
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
