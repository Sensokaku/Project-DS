/*
    Copyright 2022-2024 Hydr8gon

    This file is part of Project DS.

    Project DS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published
    by the Free Software Foundation, either version 3 of the License,
    or (at your option) any later version.

    Project DS is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Project DS. If not, see <https://www.gnu.org/licenses/>.
*/

#include <cstring>
#include <maxmod9.h>
#include <nds.h>
#include <malloc.h>

#include "vorbis/codec.h"

#include "audio.h"

static mm_stream stream;
static FILE *song = nullptr;
static int lagConfig = 0;
static int songWait = 0;
static int songOffset = 0;
const int magicNumber = 88344;
// Hitsound state
static int16_t *buttonSndData = nullptr;
static int16_t *slideSndData = nullptr;
static int16_t *holdSndData = nullptr;
static int32_t buttonSndSamples = 0;
static int32_t slideSndSamples = 0;
static int32_t holdSndSamples = 0;
static volatile int32_t buttonSndPos = -1;
static volatile int32_t slideSndPos = -1;
static volatile bool holdSndActive = false;
static volatile uint32_t holdSndFracPos = 0;
static volatile uint32_t holdSndInc = 1 << 12;

static inline int16_t clampSample(int32_t val)
{
    if (val > 32767) return 32767;
    if (val < -32768) return -32768;
    return (int16_t)val;
}

static mm_word audioCallback(mm_word length, mm_addr dest, mm_stream_formats format)
{
    int16_t *buf = (int16_t *)dest;

    // Prepend the stream with empty data if delayed
    if (songWait > 0)
    {
        songWait -= length * 4;
        memset(dest, 0, length * 4);
        if (songWait >= 0)
        {
            // Mix hitsounds into silence
            goto mix_hitsounds;
        }
        fread((uint8_t *)dest + length * 4 + songWait, sizeof(int16_t), -songWait / 2, song);
        goto mix_hitsounds;
    }

    // Load more PCM samples from file
    fread(dest, sizeof(int16_t), length * 2, song);

mix_hitsounds:
    // Mix button hitsound into the stream buffer
    if (buttonSndPos >= 0 && buttonSndData)
    {
        for (uint32_t i = 0; i < length && buttonSndPos < buttonSndSamples; i++, buttonSndPos++)
        {
            int32_t l = buf[i * 2 + 0] + buttonSndData[buttonSndPos * 2 + 0];
            int32_t r = buf[i * 2 + 1] + buttonSndData[buttonSndPos * 2 + 1];
            buf[i * 2 + 0] = clampSample(l);
            buf[i * 2 + 1] = clampSample(r);
        }
        if (buttonSndPos >= buttonSndSamples)
            buttonSndPos = -1;
    }

    // Mix slide hitsound into the stream buffer
    if (slideSndPos >= 0 && slideSndData)
    {
        for (uint32_t i = 0; i < length && slideSndPos < slideSndSamples; i++, slideSndPos++)
        {
            int32_t l = buf[i * 2 + 0] + slideSndData[slideSndPos * 2 + 0];
            int32_t r = buf[i * 2 + 1] + slideSndData[slideSndPos * 2 + 1];
            buf[i * 2 + 0] = clampSample(l);
            buf[i * 2 + 1] = clampSample(r);
        }
        if (slideSndPos >= slideSndSamples)
            slideSndPos = -1;
    }

    // Mix hold hitsound into the stream buffer (variable speed)
    if (holdSndActive && holdSndData)
    {
        for (uint32_t i = 0; i < length; i++)
        {
            uint32_t sampleIdx = holdSndFracPos >> 12;
            if (sampleIdx >= (uint32_t)holdSndSamples)
            {
                holdSndActive = false;
                break;
            }

            // Get the next sample index for interpolation
            uint32_t nextIdx = sampleIdx + 1;
            if (nextIdx >= (uint32_t)holdSndSamples)
                nextIdx = sampleIdx;

            // Linear interpolation between samples for smoother playback
            int32_t frac = holdSndFracPos & 0xFFF;
            int32_t invFrac = 0x1000 - frac;

            int32_t sl = (holdSndData[sampleIdx * 2 + 0] * invFrac +
                          holdSndData[nextIdx   * 2 + 0] * frac) >> 12;
            int32_t sr = (holdSndData[sampleIdx * 2 + 1] * invFrac +
                          holdSndData[nextIdx   * 2 + 1] * frac) >> 12;

            int32_t l = buf[i * 2 + 0] + sl;
            int32_t r = buf[i * 2 + 1] + sr;
            buf[i * 2 + 0] = clampSample(l);
            buf[i * 2 + 1] = clampSample(r);
            holdSndFracPos += holdSndInc;
        }
    }

    return length;
}

void audioInit()
{
    // Prepare the audio stream
    stream.sampling_rate = 44100 / 2;
    stream.buffer_length = 1024;
    stream.callback      = audioCallback;
    stream.format        = MM_STREAM_16BIT_STEREO;
    stream.timer         = MM_TIMER0;
    stream.manual        = true;
}

void loadHitSounds()
{
    // Load button hitsound (stereo 16-bit 22050 Hz raw PCM)
    FILE *f = fopen("/project-ds/pcm/sfx/button.pcm", "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        uint32_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (buttonSndData) { free(buttonSndData); buttonSndData = nullptr; }
        buttonSndData = (int16_t *)malloc(size);
        fread(buttonSndData, 1, size, f);
        fclose(f);
        buttonSndSamples = size / 4;  // 4 bytes per stereo sample pair
        printf("Button SFX: %ld samples\n", buttonSndSamples);
    }
    else
    {
        printf("Button SFX NOT FOUND!\n");
    }

    // Load slide hitsound (stereo 16-bit 22050 Hz raw PCM)
    f = fopen("/project-ds/pcm/sfx/slide.pcm", "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        uint32_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (slideSndData) { free(slideSndData); slideSndData = nullptr; }
        slideSndData = (int16_t *)malloc(size);
        fread(slideSndData, 1, size, f);
        fclose(f);
        slideSndSamples = size / 4;
        printf("Slide SFX: %ld samples\n", slideSndSamples);
    }
    else
    {
        printf("Slide SFX NOT FOUND!\n");
    }
}

    // Load hold hitsound (stereo 16-bit 22050 Hz raw PCM)
    f = fopen("/project-ds/pcm/sfx/hold.pcm", "rb");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        uint32_t size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (holdSndData) { free(holdSndData); holdSndData = nullptr; }
        holdSndData = (int16_t *)malloc(size);
        fread(holdSndData, 1, size, f);
        fclose(f);
        holdSndSamples = size / 4;
        printf("Hold SFX: %ld samples\n", holdSndSamples);
    }
    else
    {
        printf("Hold SFX NOT FOUND!\n");
    }

void playButtonSound()
{
    buttonSndPos = 0;
}

void playSlideSound()
{
    slideSndPos = 0;
}

void playHoldSound(uint32_t durationSamples)
{
    if (durationSamples > 0 && holdSndSamples > 0)
    {
        // Calculate playback speed so the sample fits exactly in the hold duration
        // Uses 12-bit fractional fixed-point to avoid overflow with longer samples
        holdSndInc = ((uint64_t)holdSndSamples << 12) / durationSamples;
        holdSndFracPos = 0;
        holdSndActive = true;
    }
}

void stopHoldSound()
{
    holdSndActive = false;
}

void setLagConfig(int ms)
{
    // Calculate a byte offset from a lag config in milliseconds
    lagConfig = (44100 * 2 * ms / 1000) & ~0x3;
}

void playSong(std::string &name)
{
    // Reset the PCM stream
    if (song) fclose(song);
    songOffset = 0;

    // Open and play a PCM file if it exists, skipping ahead if early
    if ((song = fopen(name.c_str(), "rb")))
    {
        if ((songWait = lagConfig) < 0)
            fseek(song, -lagConfig, SEEK_SET);
        mmStreamOpen(&stream);
    }
}

void playPreview(std::string &name, int &db_offset)
{
    // Reset the PCM stream
    if (song) fclose(song);
    songOffset = db_offset * magicNumber;

    // Open and play a PCM file if it exists, skipping ahead if early
    if ((song = fopen(name.c_str(), "rb")))
    {
        // if ((songWait = lagConfig) < 0)
        fseek(song, songOffset, SEEK_SET);
        mmStreamOpen(&stream);
    }
}

void resumeSong()
{
    // Resume the PCM stream if it's loaded
    if (song)
    {
        fseek(song, songOffset, SEEK_SET);
        mmStreamOpen(&stream);
    }
}

void updateSong()
{
    // Update the PCM stream if it's loaded
    if (song)
        mmStreamUpdate();
}

void stopSong()
{
    // Stop the PCM stream if it's loaded
    if (song)
    {
        mmStreamClose();
        songOffset = ftell(song);
    }
}

bool convertSong(std::string &src, std::string &dst)
{
    // Attempt to load an OGG file for conversion
    if (FILE *oggFile = fopen(src.c_str(), "rb"))
    {
        ogg_sync_state   oy;
        ogg_stream_state os;
        ogg_page         og;
        ogg_packet       op;

        vorbis_info      vi;
        vorbis_comment   vc;
        vorbis_dsp_state vd;
        vorbis_block     vb;

        // Get the file size for progress tracking
        fseek(oggFile, 0, SEEK_END);
        size_t oggSize = ftell(oggFile);
        fseek(oggFile, 0, SEEK_SET);

        // Read the first block from the OGG file
        ogg_sync_init(&oy);
        char *buffer = ogg_sync_buffer(&oy, 4096);
        size_t bytes = fread(buffer, sizeof(uint8_t), 4096, oggFile);
        ogg_sync_wrote(&oy, bytes);

        // Initialize the stream and get the first page
        ogg_sync_pageout(&oy, &og);
        ogg_stream_init(&os, ogg_page_serialno(&og));
        ogg_stream_pagein(&os, &og);
        ogg_stream_packetout(&os, &op);

        // Initialize the decoder with the initial header
        vorbis_info_init(&vi);
        vorbis_comment_init(&vc);
        vorbis_synthesis_headerin(&vi, &vc, &op);
        vorbis_synthesis_halfrate(&vi, 1);

        printf("Converting to PCM16...\n");

        int i = 0;
        FILE *pcmFile = fopen(dst.c_str(), "wb");

        // Decode until the end of the file is reached
        while (true)
        {
            // Read more blocks from the OGG file and track progress
            if (!ogg_sync_pageout(&oy, &og))
            {
                buffer = ogg_sync_buffer(&oy, 4096);
                bytes = fread(buffer, sizeof(uint8_t), 4096, oggFile);
                if (bytes == 0) break;
                ogg_sync_wrote(&oy, bytes);
                printf("\x1b[1;0H%ld%%\n", ftell(oggFile) * 100 / oggSize);
                continue;
            }

            // Get another page
            ogg_stream_pagein(&os, &og);

            while (ogg_stream_packetout(&os, &op))
            {
                // Get the comment and codebook headers and finish initializing the decoder
                if (i < 2)
                {
                    vorbis_synthesis_headerin(&vi, &vc, &op);
                    if (++i < 2) continue;
                    vorbis_synthesis_init(&vd, &vi);
                    vorbis_block_init(&vd, &vb);
                    break;
                }

                // Decode a packet
                vorbis_synthesis(&vb, &op);
                vorbis_synthesis_blockin(&vd, &vb);

                float **pcm;

                while (int samples = vorbis_synthesis_pcmout(&vd, &pcm))
                {
                    // Convert floats and combine channels to produce stereo PCM16
                    int16_t conv[2048] = {};
                    for (int j = 0; j < samples; j++)
                    {
                        for (int c = 0; c < std::min(4, vi.channels); c += 2)
                        {
                            conv[j * 2 + 0] += pcm[c + 0][j] * 32767.0f;
                            conv[j * 2 + 1] += pcm[c + 1][j] * 32767.0f;
                        }
                    }

                    // Write the converted data to file
                    fwrite(conv, sizeof(int16_t), samples * 2, pcmFile);
                    vorbis_synthesis_read(&vd, samples);
                }
            }
        }

        fclose(pcmFile);
        fclose(oggFile);

        printf("Done!\n");
        return true;
    }

    return false;
}
