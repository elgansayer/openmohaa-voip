
#pragma once

#include "../qcommon/q_shared.h"

// Forward declaration to avoid pulling in opus headers everywhere
struct OpusDecoder;
struct OpusEncoder;

// VoIP Constants
#define VOIP_SAMPLE_RATE 48000
#define VOIP_FRAME_SIZE 960  // 20ms at 48kHz
#define VOIP_BITRATE 32000
#define VOIP_MAX_PACKET_SIZE 1024
#define VOIP_MAX_DECODED_SAMPLES (VOIP_FRAME_SIZE)

class VoiceCodec {
public:
	VoiceCodec();
	~VoiceCodec();

	// Initialize the codec. Returns true on success.
	bool Init();

	// Shutdown and free resources.
	void Shutdown();

	// Reset encoder state.
	void Reset();

	// Set encoder bitrate (e.g. 32000 for 32kbps).
	void SetBitrate(int bitrate);

	// Reset a specific client's decoder (e.g. on new generation/sequence).
	void ResetDecoder(int clientNum);

	// Encode PCM audio from mic.
	// input: 16-bit mono 48kHz PCM
	// returns: number of bytes written to output, or -1 on error.
	int Encode(const short *in_pcm, int frame_size, unsigned char *out_packet, int max_out_size);

	// Decode Opus packet from a specific client.
	// Will automatically create/manage the decoder for 'clientNum'.
	// returns: number of samples decoded, or -1 on error.
	// decodes to 16-bit mono 48kHz PCM
	int Decode(int clientNum, const unsigned char *in_packet, int packet_size, short *out_pcm, int max_out_samples, bool decode_fec);

private:
	OpusEncoder *encoder;
	OpusDecoder *decoders[MAX_CLIENTS];

	bool initialized;

	// Helper to create a decoder
	OpusDecoder *CreateDecoder();
};
