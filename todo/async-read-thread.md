# Async Read Thread & Streaming Mode

## Overview

Replace the synchronous `dartt_read_multi` polling loop (currently blocking inside the
ImGui event loop) with a dedicated background reader thread. The reader continuously
drains the RX kernel buffer, decodes COBS frames, parses dartt read-reply payloads, and
writes results directly into `periph_buf`. This unblocks the graphics loop, dramatically
increases effective read bandwidth, and unlocks **streaming mode** where the device can
push unsolicited read-reply frames without a prior request.

---

## New Source Layout

```
src/
  reader/
    dartt_link.h         # DarttLink class declaration
    dartt_link.cpp       # DarttLink implementation
    CMakeLists.txt       # Standalone build target for easy submodule linking
  plotting.h / plotting.cpp   (modified — add WAV writer)
  dartt_init.h / dartt_init.cpp  (simplified — serial/socket ownership moves to DarttLink)
  main.cpp               (modified — remove dartt_read_multi loop, start DarttLink)
  buffer_sync.h / buffer_sync.cpp  (possibly modified — read-side sync helpers)
```

---

## Component 1: DarttLink Class

`DarttLink` replaces `DarttReader` and is the sole owner of the transport handle
(Serial, UDP socket, or TCP socket). It runs two threads: a **read thread** that
continuously drains the RX kernel buffer, and a **write thread** that drains a TX
queue dispatched from the rendering loop. Protocol semantics (dartt frame types,
periph_buf layout) live above DarttLink; the class itself is transport-agnostic and
treats all outgoing data as opaque pre-encoded byte frames.

### Responsibilities
- Own the Serial/UDP/TCP handle exclusively — no other code touches it
- **Read thread**: drain RX bytes via `cobs_stream()`, decode frames, write read-reply
  data into `periph_buf` under `periph_buf_mutex`
- **Write thread**: block on a condition variable, dequeue pre-encoded raw frames, send
  them over the wire
- Arbitrate half-duplex bus access between the two threads via `bus_mutex_`
- Optionally write raw COBS-delimited read-reply frames to a `.bin` log file

### Half-duplex vs Full-duplex (`is_full_duplex`)

In half-duplex mode (RS485, default), the bus is shared: only one direction can be
active at a time. The read thread acquires `bus_mutex_` on the first byte of each
incoming frame and holds it through `process_frame()`, releasing it after. The write
thread acquires `bus_mutex_` before calling `send_raw()`.

**Intentional starvation in streaming mode**: when the peripheral is streaming data
gaplessly (e.g. continuous audio), the read thread will hold `bus_mutex_` nearly
continuously. The write thread will starve — this is correct behaviour. In a
hardware half-duplex system the peripheral owns the bus during streaming; any TX
attempt from the controller would cause a bus collision. Starvation prevents that.
There is no yield or sleep added to the reader between frames; the writer simply
waits until there is a gap.

In full-duplex mode (`is_full_duplex = true`), TX and RX are independent lines.
Both threads skip `bus_mutex_` entirely and operate without coordination.

### TX Queue Design

The rendering loop (and any other caller) pushes **complete, pre-encoded raw frames**
into the TX queue via `enqueue_frame()`. DarttLink does not know or care whether a
frame is a write request, a read request, or anything else — it just sends bytes.
This means dartt frame construction (COBS encoding, address, CRC) happens on the
caller side before enqueue.

The write thread blocks on a `std::condition_variable` when the queue is empty, so
it consumes zero CPU while idle.

### Class Interface (sketch)

```cpp
class DarttLink {
public:
    void init(dartt_sync_t* sync, CommMode mode, Serial* serial,
              UdpState* udp, TcpState* tcp);

    void start();   // spawn read + write threads
    void stop();    // signal both threads + join

    // Push a complete pre-encoded COBS frame onto the TX queue
    void enqueue_frame(std::vector<uint8_t> frame);

    bool is_full_duplex    = false;   // skip bus_mutex on both threads when true
    bool streaming_mode    = false;   // accept unsolicited read-reply frames
    bool bin_logging_enabled = false;
    void open_bin_log(const std::string& path);
    void close_bin_log();

    // Callers must hold this when reading periph_buf or DarttField values
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

    // Half-duplex bus arbitration (ignored when is_full_duplex == true)
    std::mutex         bus_mutex_;

    // TX queue
    std::queue<std::vector<uint8_t>> tx_queue_;
    std::mutex                       tx_queue_mutex_;
    std::condition_variable          tx_cv_;

    // COBS accumulation (read thread only)
    uint8_t    enc_mem_[DARTT_LINK_BUF_SIZE];
    uint8_t    dec_mem_[DARTT_LINK_BUF_SIZE];
    cobs_buf_t cobs_enc_{};
    cobs_buf_t cobs_dec_{};

    // Raw wire byte staging for bin logger
    uint8_t    raw_frame_[DARTT_LINK_BUF_SIZE + 1];
    size_t     raw_frame_len_ = 0;

    dartt_sync_t* sync_      = nullptr;
    CommMode      comm_mode_ = COMM_SERIAL;
    Serial*       serial_    = nullptr;
    UdpState*     udp_       = nullptr;
    TcpState*     tcp_       = nullptr;

    std::FILE*    bin_log_file_ = nullptr;
};
```

### Read Loop Logic

```
acquire unique_lock(bus_mutex_, defer) — only used when !is_full_duplex

loop:
  b = read_byte()
  if no data: continue

  if !is_full_duplex && !lock.owns: lock.lock()   // claim bus on first byte

  accumulate b into raw_frame_
  ret = cobs_stream(b, enc, dec)

  if COBS_SUCCESS:
      process_frame()
      reset enc, raw_frame_
      if !is_full_duplex: lock.unlock()            // release between frames → writer can win

  if COBS_ERROR_SERIAL_OVERRUN:
      reset enc, raw_frame_
      if !is_full_duplex: lock.unlock()
```

### Write Loop Logic

```
loop:
  wait on tx_cv_ until tx_queue non-empty or !running

  frame = tx_queue_.front(); pop

  if !is_full_duplex:
      lock(bus_mutex_)            // blocks until reader releases (may starve in streaming)
      send_raw(frame)
      unlock
  else:
      send_raw(frame)             // full-duplex: no coordination needed
```

#### Index Offset Handling in Streaming Mode

In non-streaming mode the `base_offset` field in `dartt_sync_t` accounts for reads from
sub-regions of a larger blob (see `dartt_sync.c:393`). In streaming mode the device
sends replies with an absolute word index. The reader should:

1. Extract `reply_index` from the reply payload (bytes 0–1, little-endian, same as
   `dartt_parse_read_reply` does in `dartt.c`)
2. Compute byte offset: `byte_offset = reply_index * 4`
3. Bounds-check against `periph_buf.size`
4. `memcpy` data into `periph_buf.buf + byte_offset`

This bypasses `dartt_parse_read_reply` (which requires `original_msg.num_bytes` for
length validation). A streaming-mode parser should accept any valid-length reply and
derive `data_len = payload.msg.len - NUM_BYTES_READ_REPLY_OVERHEAD_PLD`.

---

## Component 2: TX Queue Dispatch (replaces dartt_read_multi + dartt_write_multi call sites)

`DarttLink` owns frame construction and dispatch for both read requests and writes,
mirroring `dartt_sync`. This decouples the polling cadence from the render loop
entirely — read request dispatch runs inside the write thread at full link speed,
not at 60 Hz.

### Read Request Dispatch (internal to DarttLink)

DarttLink maintains a **subscribed region list** — a set of `dartt_mem_t` slices
representing the memory regions to poll. The render loop registers/removes regions
when the user subscribes or unsubscribes fields (coalescing happens at the caller
side before registering, so DarttLink has no knowledge of `DarttField`).

The write thread calls `dispatch_read_requests()` after draining the explicit TX
queue, so writes always take priority. In streaming mode the dispatch is skipped —
the peripheral drives its own transmit cadence.

```cpp
// Public API
void subscribe_region(dartt_mem_t region);
void clear_subscriptions();

// Private — called from write thread after TX queue drain
void dispatch_read_requests();
```

`dispatch_read_requests` mirrors the TX side of `dartt_read_multi`: for each
subscribed region, chunk it, build read request frames via `dartt_create_read_frame`
+ `cobs_encode_single_buffer`, push each encoded frame to `tx_queue_`.

Chunk size limit (same calc as dartt_sync):
```
max_chunk_bytes = DARTT_LINK_BUF_SIZE - NUM_BYTES_NON_PAYLOAD
                                      - NUM_BYTES_READ_REPLY_OVERHEAD_PLD
                                      - NUM_BYTES_COBS_OVERHEAD
```

### Write Frame Dispatch (caller side)

Dirty write frames are still pushed by the render loop via `enqueue_frame()`.
Frame construction (chunking, index calculation, COBS encoding) mirrors
`dartt_write_multi` but the result is pushed to the TX queue instead of blocking.
This helper can live in `buffer_sync.cpp`.

### What Changes in `main.cpp`

Before:
```cpp
dartt_write_multi(&slice, &ds);          // blocking send
dartt_read_multi(&slice, &ds);           // blocking send + blocking receive
sync_periph_buf_to_fields(config, region);
```

After:
```cpp
// on field subscribe/unsubscribe:
dartt_link.subscribe_region(region);     // DarttLink polls automatically

// on dirty write:
enqueue_write_frames(config, region, dartt_link);   // builds + enqueues

// read replies handled by DarttLink read thread → read_reply_cb → plotter
```

In streaming mode, `subscribe_region` is not called — the peripheral dispatches
its own read-reply frames and the read thread consumes them via the same path.

---

## Component 3: Transport Ownership

`DarttLink` is the **sole owner** of Serial / UdpState / TcpState. All other code that
previously held or called into these handles (dartt_init.cpp callbacks, main.cpp) must
route through `DarttLink::enqueue_frame()` for TX. The `blocking_tx_callback` and
`blocking_rx_callback` fields of `dartt_sync_t` are no longer used for live comms
(they may still be needed for any remaining synchronous bootstrap operations during
init, TBD).

The `serial_mutex` that previously had to be passed around externally is now internal
to DarttLink as `bus_mutex_`. No other translation unit needs to know it exists.

---

## Component 4: Plotter Integration in Read Thread

After writing to `periph_buf`, the read thread should update `DarttField.display_value`
for subscribed fields and call `Line::enqueue_data()`.

Currently `sync_periph_buf_to_fields` + `calculate_display_values` is called on the
main thread after each `dartt_read_multi`. Moving this to the read thread means:

- The plotter data is updated at the actual hardware data rate, not the UI frame rate
- Plotting high-frequency signals (audio, fast sensors) becomes viable

### Mutex on Plotter Data

Add a `std::mutex plot_mutex` to `Plotter`. The read thread holds it while calling
`Line::enqueue_data()`; the render thread holds it while iterating `Line::points` in
`Plotter::render()`.

```cpp
class Plotter {
public:
    std::mutex plot_mutex;
    // ...
};
```

Main thread render:
```cpp
{
    std::lock_guard<std::mutex> lock(plot.plot_mutex);
    plot.render();
}
```

Read thread after `periph_buf` write:
```cpp
sync_periph_buf_to_fields(config, region);   // reads periph_buf (already under periph_buf_mutex)
calculate_display_values(config);

{
    std::lock_guard<std::mutex> lock(plotter->plot_mutex);
    for (auto& line : plotter->lines)
        line.enqueue_data(screen_width);
}
```

---

## Component 5: Binary Frame Logger

Default off. When enabled, every raw COBS-encoded dartt read-reply frame received by
the read thread is appended to a `.bin` file — bytes exactly as they arrive on the wire,
including the `0x00` COBS delimiter.

### File Format

No header. Concatenated COBS frames separated by `0x00` delimiters. This matches the
wire format exactly — the file can be fed back into `cobs_stream` byte-by-byte for
offline replay or analysis.

### API

```cpp
// In DarttReader or a standalone BinLogger helper:
void set_bin_logging(bool enable, const std::string& path = "");
```

File naming convention: `dartt_capture_YYYYMMDD_HHMMSS.bin`

---

## Component 6: WAV Writer in Plotter

Default off. When enabled, each `Line` (or a selected subset) streams its `enqueue_data`
output to a `.wav` file. Intended for audio-rate waveforms captured via the plotter.

### Design

- Add `bool wav_recording = false` and `WavWriter* wav_writer = nullptr` to `Line`
- `WavWriter` buffers PCM samples and writes IEEE 754 32-bit float WAV (format tag 3,
  `WAVE_FORMAT_IEEE_FLOAT`) — no lossy integer conversion
- Sample rate: derived from `sys_sec` timestamps across successive `enqueue_data` calls
  (compute average delta, round to nearest standard rate, or just record the actual rate
  in a metadata comment)
- Channels: one WAV file per `Line`, or interleaved multi-channel (one WAV for the whole
  `Plotter`) — TBD

### WAV File Structure

```
RIFF header (44 bytes, PCM float32, 1 channel, sample_rate Hz)
[float32 samples...]
```

For streaming writes, use the "streaming WAV" trick: write a placeholder header, seek
back and fill in `data` chunk size + `RIFF` size on `close()`.

### API

```cpp
void Plotter::start_wav_recording(const std::string& path, int sample_rate);
void Plotter::stop_wav_recording();
```

File naming convention: `dartt_plot_YYYYMMDD_HHMMSS.wav`

---

## Thread Safety Summary

| Shared Resource         | Accessed by                        | Protection              |
|-------------------------|------------------------------------|-------------------------|
| Serial / socket handle  | DarttLink read thread (RX)         | `bus_mutex_` (half-dup) |
|                         | DarttLink write thread (TX)        | none (full-dup)         |
| `tx_queue_`             | rendering thread (enqueue),        | `tx_queue_mutex_`       |
|                         | DarttLink write thread (dequeue)   | + `tx_cv_`              |
| `periph_buf`            | DarttLink read thread (write),     | `periph_buf_mutex`      |
|                         | main/UI thread (read for display)  |                         |
| `DarttField.value`      | DarttLink read thread (write),     | `periph_buf_mutex`      |
|                         | UI thread (read/display)           | (same lock)             |
| `Line::points`          | DarttLink read thread (enqueue),   | `Plotter::plot_mutex`   |
|                         | render thread (iterate)            |                         |
| `bin_log_file_`         | DarttLink read thread only         | no lock needed          |
| `WavWriter` buffers     | DarttLink read thread (write),     | `Line::wav_mutex`       |
|                         | stop() / flush                     |                         |

---

## Streaming Mode Behaviour

When `DarttReader::streaming_mode = true`:

- The main loop sends **no** read requests
- `DarttReader` accepts any arriving read-reply frame regardless of whether a request was
  pending
- The device firmware is responsible for dispatching frames at its own rate
- This enables maximum read bandwidth — the link is limited only by baud rate and device
  firmware dispatch rate, not by the dashboard request/reply round-trip

The UI should expose a toggle for streaming mode. When switched off, the dashboard
reverts to request-driven polling via the dispatcher.

---

## Implementation Order

1. **DarttLink skeleton** — rename from DarttReader; add write thread + TX queue +
   `bus_mutex_` + `is_full_duplex` flag; read thread unchanged from prior work;
   write thread blocks on `tx_cv_`, dequeues raw frames, calls `send_raw()`
2. **Frame parsing** — plug in `dartt_frame_to_payload` + custom streaming reply parser,
   write to `periph_buf` under `periph_buf_mutex`
3. **Plotter integration** — hold `plot_mutex`, call `enqueue_data` from read thread;
   add lock to `Plotter::render()`
4. **Read request dispatch** — `subscribe_region` / `clear_subscriptions` public API;
   `dispatch_read_requests()` private method mirroring `dartt_read_multi` TX side;
   write thread calls it after queue drain; `enqueue_write_frames` helper in
   `buffer_sync.cpp` for dirty writes; remove `dartt_read_multi` / `dartt_write_multi`
   call sites from `main.cpp`
5. **Streaming mode** — add toggle; skip read-request dispatch when enabled
6. **Binary logger** — append raw COBS frames to `.bin` file from read thread
7. **WAV writer** — `WavWriter` helper class, integrate into `Line::enqueue_data`
8. **Regression testing** — verify parser-desync issue (see
   `todo/parser-desync-investigation.md`) is resolved or at least not worsened

---

## Open Questions

- **Half-duplex serial and simultaneous RX/TX**: Does the hardware need the line to be
  idle before a new request? If so, the serial mutex may need to block the reader thread
  during TX, not just protect the `Serial` object.
	- Answer: Yes. This program acts as the `controller` device in normal operation, 
	where only dispatched **read requests** result in the target/peripheral emenating a 
	response. It may be half duplex if in RS485 mode, so I think the tx dispatchers have to 
	do something to prevent generating line activity if the read thread is active to 
	prevent collisions. Currently thinking that the reader thread should block tx, and that 
	all tx operations (read request dispatch and write_multi) may belong in their own thread.
	That way if we're in streaming mode there's no collisions, and the write thread can grab the 
	resource safely in 'normal mode' since there's a 1:1 read activity with read request in normal 
	operation.


- **`dartt_parse_read_reply` reuse vs. custom parser**: The existing function validates
  `payload.msg.len == original_msg.num_bytes + 2`. In streaming mode we don't have
  `original_msg`. Either relax the check or write a streaming-mode variant.
	- I think the best way to handle this is to cheat - create a local scoped copy of original_msg, load it with payload.msg.len + 2,
	and keep the library as-is. It's reasonable for parse_read_reply to have some expected length argument, and it's easy to bypass
	for a use-case like ours using this method. Plus the original_msg is a super lighweight type, an insignificant impact in the hot loop
	which is dominated by IO timing anyway.


- **Multi-config support**: `periph_buf` is per `DarttConfig`. If multiple configs are
  loaded simultaneously the reader needs to route frames to the correct buffer by address
  or index range.
	- Only one config is loaded at a time. To handle multiple config the user should just spawn multiple dashboard program instances.

- **WAV sample rate**: `Line::enqueue_data` timestamp resolution depends on
  `SDL_GetTicks64()` (millisecond). Audio rates need microsecond timestamps — consider
  `std::chrono::steady_clock` in the read thread.
	- Yes. We should create a dedicated get_microseconds() subroutine that uses std::chrono for audio writing.
	Also - will we be able to set sampling rate this way? IIRC wav files expect a steady sampling rate. 

- **Bin log with/without timestamp prefix**: Timestamped format is more useful for
  analysis but changes the file format. Decide before implementation to avoid breaking
  offline tools.

	- I don't care to add timestamps. Peripherals can include timestamps as part of their layout - prepending them clutters the stream
	 and adds potentially unnecessary and/or redundant data. Just raw cobs delimited data, pretty much just as-is content from the serial 
	 buffer drain.

