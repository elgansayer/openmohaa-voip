
#include "client.h"
#include "VoiceCodec.h"
#include <opus.h>

// Standard Opus settings for VoIP
#define VOIP_SAMPLE_RATE 48000
#define VOIP_CHANNELS 1
#define VOIP_APPLICATION OPUS_APPLICATION_VOIP

VoiceCodec::VoiceCodec() : encoder(NULL), initialized(false) {
	for (int i = 0; i < MAX_CLIENTS; i++) {
		decoders[i] = NULL;
	}
}

VoiceCodec::~VoiceCodec() {
	Shutdown();
}

bool VoiceCodec::Init() {
	if (initialized) {
		return true;
	}

	int error;

	// Initialize Encoder
	encoder = opus_encoder_create(VOIP_SAMPLE_RATE, VOIP_CHANNELS, VOIP_APPLICATION, &error);
	if (error != OPUS_OK) {
		Com_Printf("VoiceCodec: Failed to create encoder: %s\n", opus_strerror(error));
		return false;
	}

	// Set bitrate (~32kbps)
	opus_encoder_ctl(encoder, OPUS_SET_BITRATE(32000));
	
	// Enable in-band FEC (Forward Error Correction)
	opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
	
	// Set expected packet loss percentage (10%)
	opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
	
	Com_Printf("VoiceCodec: Initialized with FEC enabled (10%% packet loss tolerance)\n");
	
	initialized = true;
	return true;
}

void VoiceCodec::Shutdown() {
	if (encoder) {
		opus_encoder_destroy(encoder);
		encoder = NULL;
	}
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (decoders[i]) {
			opus_decoder_destroy(decoders[i]);
			decoders[i] = NULL;
		}
	}
	initialized = false;
}

void VoiceCodec::Reset() {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_RESET_STATE);
	}
	// Reset all decoders
	for (int i = 0; i < MAX_CLIENTS; i++) {
		ResetDecoder(i);
	}
}

void VoiceCodec::SetBitrate(int bitrate) {
	if (encoder) {
		opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrate));
	}
}

void VoiceCodec::ResetDecoder(int clientNum) {
	if (clientNum < 0 || clientNum >= MAX_CLIENTS) return;
	
	if (decoders[clientNum]) {
		opus_decoder_ctl(decoders[clientNum], OPUS_RESET_STATE);
	}
}

int VoiceCodec::Encode(const short *in_pcm, int frame_size, unsigned char *out_packet, int max_out_size) {
	if (!initialized || !encoder) return -1;

	int len = opus_encode(encoder, in_pcm, frame_size, out_packet, max_out_size);
	if (len < 0) {
		Com_DPrintf("VoiceCodec: Encode failed: %s\n", opus_strerror(len));
		return -1;
	}
	return len;
}

OpusDecoder *VoiceCodec::CreateDecoder() {
	int error;
	OpusDecoder *dec = opus_decoder_create(VOIP_SAMPLE_RATE, VOIP_CHANNELS, &error);
	if (error != OPUS_OK) {
		Com_Printf("VoiceCodec: Failed to create decoder: %s\n", opus_strerror(error));
		return NULL;
	}
	return dec;
}

int VoiceCodec::Decode(int clientNum, const unsigned char *in_packet, int packet_size, short *out_pcm, int max_out_samples, bool decode_fec) {
	if (clientNum < 0 || clientNum >= MAX_CLIENTS) return -1;

	// Lazily create decoder
	if (!decoders[clientNum]) {
		decoders[clientNum] = CreateDecoder();
		if (!decoders[clientNum]) return -1;
	}

	int decoded_samples = opus_decode(decoders[clientNum], in_packet, packet_size, out_pcm, max_out_samples, decode_fec ? 1 : 0);

	if (decoded_samples < 0) {
		Com_DPrintf("VoiceCodec: Decode failed: %s\n", opus_strerror(decoded_samples));
		return -1;
	}

	return decoded_samples;
}
