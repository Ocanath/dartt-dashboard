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
    dartt_reader.h       # DarttReader class declaration
    dartt_reader.cpp     # DarttReader implementation
    CMakeLists.txt       # Standalone build target for easy submodule linking
  plotting.h / plotting.cpp   (modified — add WAV writer)
  dartt_init.h / dartt_init.cpp  (modified — serial mutex)
  main.cpp               (modified — remove dartt_read_multi loop, start reader thread)
  buffer_sync.h / buffer_sync.cpp  (possibly modified — read-side sync helpers)
```

---

## Component 1: DarttReader Class

### Responsibilities
- Own a `std::thread` that runs for the lifetime of the connection
- Drain RX bytes one at a time from `Serial::read()` or socket `recv()`
- Feed each byte into `cobs_stream()` to accumulate COBS frames
- On a complete frame (COBS delimiter `0x00`), parse it as a dartt payload
- If the payload is a read-reply, write the data into the correct slice of `periph_buf`
- Notify / wake the main thread that new data is available (condition variable or atomic flag)
- Optionally write raw byte-stuffed frames to a `.bin` log file

### Class Interface (sketch)

```cpp
class DarttReader {
public:
    // Initialise with the shared dartt_sync_t so we can reach periph_buf,
    // the comms handles, and the message type (serial / UDP / TCP).
    void init(dartt_sync_t* sync, CommMode mode, Serial* serial,
              tcs_socket_t* sock, Plotter* plotter);

    void start();   // spawn thread
    void stop();    // signal thread + join

    // Streaming mode: accept unsolicited frames (no pending request needed)
    bool streaming_mode = false;

    // Optional logging
    bool bin_logging_enabled = false;
    void open_bin_log(const std::string& path);
    void close_bin_log();

    // Mutex that callers must hold when accessing periph_buf
    std::mutex periph_buf_mutex;

    // Set by the request dispatcher so the reader can validate non-streaming replies
    // (optional; reader can also just trust the index embedded in the reply)
    struct PendingRequest {
        uint16_t index;
        uint16_t num_bytes;
    };
    void push_pending_request(PendingRequest req);

private:
    void thread_loop();
    void handle_frame(const uint8_t* cobs_decoded_buf, size_t len);
    void write_to_periph_buf(uint16_t reply_index, const uint8_t* data,
                             size_t data_len);

    std::thread           thread_;
    std::atomic<bool>     running_{false};

    // COBS accumulation state
    cobs_buf_t            cobs_enc_buf_;
    cobs_buf_t            cobs_dec_buf_;
    // (or use cobs_stream byte-by-byte if reading one byte at a time from serial)

    dartt_sync_t*         sync_;
    CommMode              comm_mode_;
    Serial*               serial_;
    tcs_socket_t*         socket_;
    Plotter*              plotter_;

    std::FILE*            bin_log_file_ = nullptr;

    // Pending request queue (non-streaming mode)
    std::mutex            pending_mutex_;
    std::queue<PendingRequest> pending_queue_;
};
```

### Thread Loop Logic

```
loop:
  read N bytes from serial or socket (non-blocking or short timeout)
  for each byte b:
      ret = cobs_stream(b, &cobs_enc_buf_, &cobs_dec_buf_)
      if ret == COBS_COMPLETE:
          handle_frame(cobs_dec_buf_.buf, cobs_dec_buf_.length)
          reset cobs_enc_buf_.length = 0
```

**Alternative**: If `Serial::read_until_delimiter` is more efficient on the platform,
read a full COBS frame at once (up to 0x00), then call `cobs_decode_double_buffer`.
This is essentially what `rx_blocking` does today, but off the main thread.

### Frame Handling

```
handle_frame():
  dartt_frame_to_payload(&cobs_dec_frame, msg_type, PAYLOAD_MODE_RX, &pld)
  if pld is read-reply:
      if bin_logging_enabled:
          fwrite(raw_cobs_bytes_including_delimiter, ...)
      if streaming_mode OR pending_queue has matching request:
          acquire periph_buf_mutex
          write data into periph_buf at correct byte offset
          release periph_buf_mutex
          acquire plotter_mutex
          update plotter Line sources
          release plotter_mutex
          set data_ready_flag
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

## Component 2: Read Request Dispatcher

Replace the `dartt_read_multi` call site in `main.cpp` with a dispatcher that only
sends the read request frame (TX side) and does **not** wait for the reply.

### What Changes in `main.cpp`

Before:
```cpp
// for each region:
dartt_read_multi(&slice, &ds);           // blocking send + blocking receive
sync_periph_buf_to_fields(config, region);
```

After:
```cpp
// for each region (non-streaming mode):
dispatch_read_request(&slice, &ds);      // send request frame only, push PendingRequest
// ... DarttReader thread handles the reply asynchronously
// main thread checks data_ready_flag each frame and calls sync_periph_buf_to_fields
```

In streaming mode, no requests are dispatched at all — the main thread only consumes
whatever the reader thread has written.

### `dispatch_read_request` (sketch)

```cpp
void dispatch_read_request(dartt_mem_t* ctl, dartt_sync_t* psync) {
    // Build read frame(s) exactly as dartt_read_multi does (chunking logic),
    // but only call blocking_tx_callback — skip blocking_rx_callback entirely.
    // For each chunk, push a PendingRequest into DarttReader's queue.
    // Wrap the TX call in serial_mutex.
}
```

This function can live in `buffer_sync.cpp` or be a thin wrapper in `main.cpp`.

---

## Component 3: Serial Mutex for dartt_write_multi and dispatch_read_request

`dartt_write_multi` and `dispatch_read_request` call `blocking_tx_callback` and which calls `Serial::write` and/or uses TCP/UDP sockets. The read
thread also uses the serial handle (RX side). Even though serial is half-duplex, the
`Serial` object itself may not be thread-safe. Add a mutex around all TX operations.

```cpp
std::mutex serial_mutex;

// Wrap dartt_write_multi calls:
{
    std::lock_guard<std::mutex> lock(serial_mutex);
    dartt_write_multi(&slice, &ds);
}

// dispatch_read_request must also hold serial_mutex when calling blocking_tx_callback.
```

Pass `serial_mutex` (or a pointer to it) into `DarttReader` so the reader can avoid
reading while a write is in progress if needed (e.g. if the hardware is strict about
not receiving while transmitting).

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

| Shared Resource     | Accessed by                  | Protection            |
|---------------------|------------------------------|-----------------------|
| `Serial` handle (TX)| main thread, read dispatcher | `serial_mutex`        |
| `Serial` handle (RX)| DarttReader thread           | owned exclusively     |
| `periph_buf`        | DarttReader thread (write),  | `periph_buf_mutex`    |
|                     | main thread (read for UI)    |                       |
| `DarttField.value`  | DarttReader thread (write),  | `periph_buf_mutex`    |
|                     | UI thread (read/display)     | (same lock)           |
| `Line::points`      | DarttReader thread (enqueue),| `Plotter::plot_mutex` |
|                     | render thread (iterate)      |                       |
| `bin_log_file_`     | DarttReader thread only      | no lock needed        |
| `WavWriter` buffers | DarttReader thread (write),  | `Line::wav_mutex`     |
|                     | stop() / flush               |                       |

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

1. **DarttReader skeleton** — thread, cobs_stream loop, no parsing yet (just prints
   frame lengths)
2. **Frame parsing** — plug in `dartt_frame_to_payload` + custom streaming reply parser,
   write to `periph_buf` under mutex
3. **Plotter integration** — hold `plot_mutex`, call `enqueue_data` from read thread;
   add lock to `Plotter::render()`
4. **Dispatcher** — replace `dartt_read_multi` call site with TX-only dispatch; add
   `serial_mutex` around all TX paths
5. **Streaming mode** — add toggle, skip pending-request validation in reader
6. **Binary logger** — append raw frames to `.bin` file
7. **WAV writer** — `WavWriter` helper class, integrate into `Line::enqueue_data`
8. **Folder / CMakeLists** — extract `src/reader/` as its own build target
9. **Regression testing** — verify parser-desync issue (see
   `todo/parser-desync-investigation.md`) is resolved or at least not worsened; confirm
   write path (`dartt_write_multi`) still works under `serial_mutex`

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

