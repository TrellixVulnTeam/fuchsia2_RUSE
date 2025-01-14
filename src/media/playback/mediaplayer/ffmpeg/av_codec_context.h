// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_AV_CODEC_CONTEXT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_AV_CODEC_CONTEXT_H_

#include "src/media/playback/mediaplayer/graph/types/stream_type.h"
#include "src/media/playback/mediaplayer/graph/types/video_stream_type.h"
extern "C" {
#include "libavformat/avformat.h"
}

// Ffmeg defines this...undefine.
#undef PixelFormat

namespace media_player {

struct AVCodecContextDeleter {
  void operator()(AVCodecContext* context) const {
    avcodec_free_context(&context);
  }
};

using AvCodecContextPtr =
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;

struct AvCodecContext {
  static AvCodecContextPtr Create(const StreamType& stream_type);

  static std::unique_ptr<StreamType> GetStreamType(const AVCodecContext& from);

  static std::unique_ptr<StreamType> GetStreamType(const AVStream& from);
};

// Converts an AVPixelFormat to a PixelFormat.
VideoStreamType::PixelFormat PixelFormatFromAVPixelFormat(
    AVPixelFormat av_pixel_format);

// Converts a PixelFormat to an AVPixelFormat.
AVPixelFormat AVPixelFormatFromPixelFormat(
    VideoStreamType::PixelFormat pixel_format);

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FFMPEG_AV_CODEC_CONTEXT_H_
