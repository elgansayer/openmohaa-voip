# Opus Implementation Analysis

This document presents an analysis of the Opus codec implementation in OpenMoHAA, compared against the specifications and recommendations in [RFC 6716](https://www.rfc-editor.org/rfc/rfc6716.html).

## Overview

The project uses `libopus` (v1.5.2) for VoIP functionality. The implementation is primarily located in:
*   `code/client/VoiceCodec.cpp`: Wrapper class for Opus encoder/decoder.
*   `code/client/cl_parse.cpp`: Handling of incoming VoIP packets.

## Compliance with RFC 6716

The implementation generally adheres to the RFC recommendations for VoIP applications.

### 1. Initialization Parameters
*   **Sample Rate:** 48 kHz (`VOIP_SAMPLE_RATE`). This matches the Opus "Fullband" (FB) specification (RFC 6716 Section 2).
*   **Channels:** Mono (`VOIP_CHANNELS 1`). Compliant for VoIP.
*   **Application Type:** `OPUS_APPLICATION_VOIP`. Correctly selects the mode optimized for voice quality and low latency.
*   **Frame Size:** 20ms (960 samples). Matches RFC 6716 Section 2.1.4 recommendation ("20 ms frames are a good choice for most applications").
*   **Bitrate:** 32 kbps (`VOIP_BITRATE`). This falls within the recommended "sweet spot" range of 28-40 kbit/s for Fullband speech (RFC 6716 Section 2.1.1).

### 2. Packet Loss Resilience
*   **Encoder:** The encoder is correctly configured to enable In-band Forward Error Correction (FEC) and expects 10% packet loss:
    ```cpp
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    ```
    This aligns with RFC 6716 Section 2.1.7.

## Gap Analysis: Unused FEC

While the **encoder** is configured to send FEC data (consuming extra bandwidth to protect against loss), the **decoder** implementation in `CL_ParseVoip` (`code/client/cl_parse.cpp`) does not appear to utilize it.

### The Issue
RFC 6716 states that FEC information for a frame is added to the *subsequent* packet. To recover a lost packet $N$, the decoder needs to receive packet $N+1$ and invoke the decoder with the `decode_fec` flag set.

In `code/client/cl_parse.cpp`:
```cpp
if (seqdiff != 0) {
    Com_DPrintf("VoIP: Dropped %d frames from client #%d\n", seqdiff, sender);
    // tell opus that we're missing frames...
    for (i = 0; i < seqdiff; i++) {
        // ...
        numSamples = clc.voiceCodec->Decode(sender, NULL, 0, decoded + written, VOIP_MAX_PACKET_SAMPLES, qfalse);
        // ...
    }
}
```
When a gap is detected (`seqdiff > 0`), the code calls `Decode` with `NULL` data and `decode_fec = qfalse`. Passing `NULL` to the Opus decoder triggers **Packet Loss Concealment (PLC)**, which synthesizes audio based on the last received frame. It does **not** recover the actual lost audio using the FEC data present in the current packet.

### Recommendation
To fully leverage the bandwidth used by FEC, the decoding logic should be updated to attempt FEC recovery for the *last* missing frame in a sequence.

If `seqdiff > 0` (packets missed) and we have a valid current packet:
1.  For frames `0` to `seqdiff - 2`: Continue using PLC (`NULL` input).
2.  For the last missing frame (`seqdiff - 1`): Call `Decode` using the **current packet's data** but with `decode_fec = true`.

This requires extracting the length of the first frame in the current packet without advancing the read pointer, so it can be passed to the decoder for FEC recovery of the previous frame.
