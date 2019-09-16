/*
 * This file is part of Chiaki.
 *
 * Chiaki is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Chiaki is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Chiaki.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "video-decoder.h"

#include <jni.h>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window_jni.h>

#include <string.h>

static void android_chiaki_video_decoder_flush(AndroidChiakiVideoDecoder *decoder);

ChiakiErrorCode android_chiaki_video_decoder_init(AndroidChiakiVideoDecoder *decoder, ChiakiLog *log)
{
	decoder->log = log;
	decoder->codec = NULL;
	decoder->timestamp_cur = 0;
	return chiaki_mutex_init(&decoder->mutex, false);
}

void android_chiaki_video_decoder_fini(AndroidChiakiVideoDecoder *decoder)
{
	chiaki_mutex_fini(&decoder->mutex);
}

void android_chiaki_video_decoder_set_surface(AndroidChiakiVideoDecoder *decoder, JNIEnv *env, jobject surface)
{
	chiaki_mutex_lock(&decoder->mutex);

	if(decoder->codec)
	{
		// TODO: destroy old
		CHIAKI_LOGE(decoder->log, "Video Decoder already initialized");
		goto beach;
	}

	decoder->window = ANativeWindow_fromSurface(env, surface);

	const char *mime = "video/avc";

	decoder->codec = AMediaCodec_createDecoderByType(mime);
	if(!decoder->codec)
	{
		CHIAKI_LOGE(decoder->log, "Failed to create AMediaCodec for mime type %s", mime);
		ANativeWindow_release(decoder->window);
		decoder->window = NULL;
		goto beach;
	}

	AMediaFormat *format = AMediaFormat_new();
	AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, 1280); // TODO: correct values
	AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, 720);

	AMediaCodec_configure(decoder->codec, format, decoder->window, NULL, 0);
	AMediaCodec_start(decoder->codec);

	AMediaFormat_delete(format);

beach:
	chiaki_mutex_unlock(&decoder->mutex);
}

void android_chiaki_video_decoder_video_sample(uint8_t *buf, size_t buf_size, void *user)
{
	AndroidChiakiVideoDecoder *decoder = user;
	chiaki_mutex_lock(&decoder->mutex);

	if(!decoder->codec)
	{
		CHIAKI_LOGE(decoder->log, "Received video data, but decoder is not initialized!");
		goto beach;
	}

	ssize_t codec_buf_index;

	CHIAKI_LOGD(decoder->log, "Got video sample of size %zu", buf_size);

	while(buf_size > 0)
	{
		codec_buf_index = AMediaCodec_dequeueInputBuffer(decoder->codec, 100); // TODO: lower timeout?
		if(codec_buf_index < 0)
		{
			// TODO: handle better
			CHIAKI_LOGE(decoder->log, "Failed to get input buffer");
			goto beach;
		}

		size_t codec_buf_size;
		uint8_t *codec_buf = AMediaCodec_getInputBuffer(decoder->codec, (size_t)codec_buf_index, &codec_buf_size);
		size_t codec_sample_size = buf_size;
		if(codec_sample_size > codec_buf_size)
		{
			CHIAKI_LOGD(decoder->log, "Sample is bigger than buffer, splitting");
			codec_sample_size = codec_buf_size;
		}
		memcpy(codec_buf, buf, codec_sample_size);
		AMediaCodec_queueInputBuffer(decoder->codec, (size_t)codec_buf_index, 0, codec_sample_size, decoder->timestamp_cur++, 0); // timestamp just raised by 1 for maximum realtime
		buf += codec_sample_size;
		buf_size -= codec_sample_size;

		AMediaCodecBufferInfo info;
		ssize_t status = AMediaCodec_dequeueOutputBuffer(decoder->codec, &info, 0);
		if(status >= 0)
		{
			AMediaCodec_releaseOutputBuffer(decoder->codec, (size_t)status, info.size != 0);
		}
	}

beach:
	android_chiaki_video_decoder_flush(decoder);
	chiaki_mutex_unlock(&decoder->mutex);
}

static void android_chiaki_video_decoder_flush(AndroidChiakiVideoDecoder *decoder)
{
	// decoder->mutex must be already locked
}