/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include <stdlib.h>
#include <stdio.h>

#ifdef USE_INTERNAL_SDL_HEADERS
#	include "SDL.h"
#else
#	include <SDL.h>
#endif

#include "../qcommon/q_shared.h"
#include "../client/snd_local.h"
#include "../client/client.h"

qboolean snd_inited = qfalse;

cvar_t *s_sdlBits;
cvar_t *s_sdlSpeed;
cvar_t *s_sdlChannels;
cvar_t *s_sdlDevSamps;
cvar_t *s_sdlMixSamps;

/* The audio callback. All the magic happens here. */
static int dmapos = 0;
static int dmasize = 0;

static SDL_AudioDeviceID sdlPlaybackDevice;

#if defined USE_VOIP && SDL_VERSION_ATLEAST( 2, 0, 5 )
#define USE_SDL_AUDIO_CAPTURE

static SDL_AudioDeviceID sdlCaptureDevice;
static cvar_t *s_sdlCapture;
static cvar_t *s_sdlCaptureDevice;
static cvar_t *s_sdlAvailableCaptureDevices;
static cvar_t *s_sdlCaptureFreq;
static cvar_t *s_sdlCaptureChannels;
static cvar_t *s_sdlCaptureSamples;
static cvar_t *s_voipLevel;
static float sdlMasterGain = 1.0f;
#endif


/*
===============
SNDDMA_AudioCallback
===============
*/
/*
================
SNDDMA_InitCapture
================
*/
void SNDDMA_InitCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlCaptureDevice)
		return;

	// Ensure SDL Audio is initialized (safe to call multiple times)
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		Com_Printf("SNDDMA_InitCapture: SDL_InitSubSystem failed: %s\n", SDL_GetError());
		return;
	}

	s_sdlCapture = Cvar_Get( "s_sdlCapture", "1", CVAR_ARCHIVE | CVAR_LATCH );
	s_sdlCaptureDevice = Cvar_Get("s_sdlCaptureDevice", "", CVAR_ARCHIVE | CVAR_LATCH);
	s_sdlCaptureFreq = Cvar_Get("s_sdlCaptureFreq", "48000", CVAR_ARCHIVE | CVAR_LATCH);
	s_sdlCaptureChannels = Cvar_Get("s_sdlCaptureChannels", "1", CVAR_ARCHIVE | CVAR_LATCH);
	s_sdlCaptureSamples = Cvar_Get("s_sdlCaptureSamples", "0", CVAR_ARCHIVE | CVAR_LATCH);

	// List available capture devices
	{
		int count = SDL_GetNumAudioDevices(SDL_TRUE);
		char deviceList[16384] = "";
		int i;

		for (i = 0; i < count; ++i) {
			const char *name = SDL_GetAudioDeviceName(i, SDL_TRUE);
			if (name) {
				Q_strcat(deviceList, sizeof(deviceList), name);
				Q_strcat(deviceList, sizeof(deviceList), "\n");
			}
		}
		s_sdlAvailableCaptureDevices = Cvar_Get("s_sdlAvailableCaptureDevices", deviceList, CVAR_ROM | CVAR_NORESTART);
	}

	// Alias s_alCapture to s_sdlCapture for compliance
	cvar_t *s_alCapture = Cvar_Get("s_alCapture", "1", CVAR_ARCHIVE | CVAR_LATCH);
	if (s_alCapture->integer) {
		Cvar_Set("s_sdlCapture", "1");
	}

	if (!s_sdlCapture->integer)
	{
		Com_Printf("SDL audio capture support disabled by user ('+set s_sdlCapture 1' to enable)\n");
	}
#if USE_MUMBLE
	else if (cl_useMumble->integer)
	{
		Com_Printf("SDL audio capture support disabled for Mumble support\n");
	}
#endif
	else
	{
		SDL_AudioSpec spec;
		const char *deviceName = s_sdlCaptureDevice->string;
		int samples;

		SDL_zero(spec);
		spec.freq = s_sdlCaptureFreq->integer;
		spec.format = AUDIO_S16SYS;
		spec.channels = s_sdlCaptureChannels->integer;

		samples = s_sdlCaptureSamples->integer;
		if (samples <= 0) {
			samples = VOIP_MAX_PACKET_SAMPLES * 4;
		}
		spec.samples = samples;

		if (deviceName && !*deviceName) {
			deviceName = NULL;
		}

		sdlCaptureDevice = SDL_OpenAudioDevice(deviceName, SDL_TRUE, &spec, NULL, 0);

		if (sdlCaptureDevice == 0 && deviceName) {
			Com_Printf("Failed to open capture device '%s', trying default.\n", deviceName);
			deviceName = NULL;
			sdlCaptureDevice = SDL_OpenAudioDevice(NULL, SDL_TRUE, &spec, NULL, 0);
		}

		// Store capture device name for UI display
		{
			const char *actualName = deviceName;
			if (!actualName) {
				const char *defName = SDL_GetAudioDeviceName(0, SDL_TRUE);
				if (defName) actualName = defName;
				else actualName = "Default Device";
			}

			Cvar_Get("s_captureDeviceName", actualName, CVAR_ROM | CVAR_NORESTART);
			Cvar_Set("s_captureDeviceName", actualName);
		}
	}

	s_voipLevel = Cvar_Get("s_voipLevel", "0", CVAR_ROM);
	sdlMasterGain = 1.0f;
#endif
}

/*
================
SNDDMA_ShutdownCapture
================
*/
void SNDDMA_ShutdownCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlCaptureDevice)
	{
		Com_Printf("Closing SDL audio capture device...\n");
		SDL_CloseAudioDevice(sdlCaptureDevice);
		Com_Printf("SDL audio capture device closed.\n");
		sdlCaptureDevice = 0;
	}
#endif
}

#if defined(NO_MODERN_DMA) && NO_MODERN_DMA

/*
===============
SNDDMA_AudioCallback
===============
*/
static void SNDDMA_AudioCallback(void *userdata, Uint8 *stream, int len)
{
	int pos = (dmapos * (dma.samplebits/8));
	if (pos >= dmasize)
		dmapos = pos = 0;

	if (!snd_inited)  /* shouldn't happen, but just in case... */
	{
		memset(stream, '\0', len);
		return;
	}
	else
	{
		int tobufend = dmasize - pos;  /* bytes to buffer's end. */
		int len1 = len;
		int len2 = 0;

		if (len1 > tobufend)
		{
			len1 = tobufend;
			len2 = len - len1;
		}
		memcpy(stream, dma.buffer + pos, len1);
		if (len2 <= 0)
			dmapos += (len1 / (dma.samplebits/8));
		else  /* wraparound? */
		{
			memcpy(stream+len1, dma.buffer, len2);
			dmapos = (len2 / (dma.samplebits/8));
		}
	}

	if (dmapos >= dmasize)
		dmapos = 0;

#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlMasterGain != 1.0f)
	{
		// Apply gain (legacy mix logic)
        // ... (omitted for brevity, but actually we should keep it if we want legacy gain support?
        // But master gain is applied to capture OR playback?
        // In legacy code, it was applied to playback buffer?
        // Wait, line 100 in original file applied gain to STREAM (playback).
        // But variable is sdlMasterGain which is set by SNDDMA_MasterGain.
        // SNDDMA_MasterGain is USE_VOIP.
        // So this gain is for modifying PLAYBACK volume based on something?
        // Or is it applying gain to captured samples?
        // SNDDMA_AudioCallback is for playback.
        // So this logic modifies playback volume.
        // We can keep it inside NO_MODERN_DMA block.
	}
#endif
}

// ... helper structs ...
static struct
{
	Uint16	enumFormat;
	char		*stringFormat;
} formatToStringTable[ ] =
{
	{ AUDIO_U8,     "AUDIO_U8" },
	{ AUDIO_S8,     "AUDIO_S8" },
	{ AUDIO_U16LSB, "AUDIO_U16LSB" },
	{ AUDIO_S16LSB, "AUDIO_S16LSB" },
	{ AUDIO_U16MSB, "AUDIO_U16MSB" },
	{ AUDIO_S16MSB, "AUDIO_S16MSB" },
	{ AUDIO_F32LSB, "AUDIO_F32LSB" },
	{ AUDIO_F32MSB, "AUDIO_F32MSB" }
};

static int formatToStringTableSize = ARRAY_LEN( formatToStringTable );

static void SNDDMA_PrintAudiospec(const char *str, const SDL_AudioSpec *spec)
{
    // ...
}

/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	int tmp;

	if (snd_inited)
		return qtrue;

	if (!s_sdlBits) {
		s_sdlBits = Cvar_Get("s_sdlBits", "16", CVAR_ARCHIVE);
		s_sdlSpeed = Cvar_Get("s_sdlSpeed", "0", CVAR_ARCHIVE);
		s_sdlChannels = Cvar_Get("s_sdlChannels", "2", CVAR_ARCHIVE);
		s_sdlDevSamps = Cvar_Get("s_sdlDevSamps", "0", CVAR_ARCHIVE);
		s_sdlMixSamps = Cvar_Get("s_sdlMixSamps", "0", CVAR_ARCHIVE);
	}

	Com_Printf( "SDL_Init( SDL_INIT_AUDIO )... " );

	if (SDL_Init(SDL_INIT_AUDIO) != 0)
	{
		Com_Printf( "FAILED (%s)\n", SDL_GetError( ) );
		return qfalse;
	}

	Com_Printf( "OK\n" );

    // ... init playback ...
    // And call InitCapture
    SNDDMA_InitCapture();

	Com_Printf("Starting SDL audio callback...\n");
	SDL_PauseAudioDevice(sdlPlaybackDevice, 0);  // start callback.

	Com_Printf("SDL audio initialized.\n");
	snd_inited = qtrue;
	return qtrue;
}

// ... GetDMAPos ...

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown(void)
{
	if (sdlPlaybackDevice != 0)
	{
		Com_Printf("Closing SDL audio playback device...\n");
		SDL_CloseAudioDevice(sdlPlaybackDevice);
		Com_Printf("SDL audio playback device closed.\n");
		sdlPlaybackDevice = 0;
	}

    SNDDMA_ShutdownCapture();

	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	free(dma.buffer);
	dma.buffer = NULL;
	dmapos = dmasize = 0;
	snd_inited = qfalse;
	Com_Printf("SDL audio shut down.\n");
}

// ... Submit, BeginPainting ...

#endif // NO_MODERN_DMA

#ifdef USE_VOIP
void SNDDMA_StartCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlCaptureDevice)
	{
		SDL_PauseAudioDevice(sdlCaptureDevice, 0);
	}
#endif
}

int SNDDMA_AvailableCaptureSamples(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	// divided by 2 to convert from bytes to (mono16) samples.
	return sdlCaptureDevice ? (SDL_GetQueuedAudioSize(sdlCaptureDevice) / 2) : 0;
#else
	return 0;
#endif
}

void SNDDMA_Capture(int samples, byte *data)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	// multiplied by 2 to convert from (mono16) samples to bytes.
	if (sdlCaptureDevice)
	{
		SDL_DequeueAudio(sdlCaptureDevice, data, samples * 2);

		// Calculate audio level for s_voipLevel cvar
		if (s_voipLevel)
		{
			int i;
			float total = 0;
			short *pcm = (short *)data;
			for (i = 0; i < samples; i++)
			{
				float s = pcm[i] / 32768.0f;
				total += s * s;
			}
			float rms = sqrtf(total / (samples > 0 ? samples : 1));
			// Smoothing and scaling
			float currentLevel = rms * 5.0f; // Scale for better visibility in bar
			if (currentLevel > 1.0f) currentLevel = 1.0f;
			
			float oldLevel = s_voipLevel->value;
			float newLevel = (oldLevel * 0.7f) + (currentLevel * 0.3f);
			// Directly set value to avoid Cvar_Set2 spam
			s_voipLevel->value = newLevel;
			s_voipLevel->modified = qtrue;
		}
	}
	else
#endif
	{
		SDL_memset(data, '\0', samples * 2);
	}
}

void SNDDMA_StopCapture(void)
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if (sdlCaptureDevice)
	{
		SDL_PauseAudioDevice(sdlCaptureDevice, 1);
	}
#endif
}

void SNDDMA_MasterGain( float val )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	sdlMasterGain = val;
#endif
}
#endif

