#ifndef FFMPEG_SDK_H
#define FFMPEG_SDK_H

#ifdef __cplusplus
extern "C" {
#define    __STDC_CONSTANT_MACROS
#endif

#include <libavutil/avutil.h>
#include <libavutil/avstring.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#include <libavdevice/avdevice.h>
#include <libavutil/pixdesc.h>
#include <libavutil/error.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/samplefmt.h>
#include <libavutil/rational.h>
#include <libavutil/channel_layout.h>

#ifdef __cplusplus
}
#endif

#include "utils/av/av.hpp"
#include <memory>

#define AV_PACKET_TYPE_DEF_VIDEO (0)
#define AV_PACKET_TYPE_DEF_AUDIO (1)

typedef enum {
    PRIVATE_DATA_TYPE_UNKNOWN = 0,
    PRIVATE_DATA_TYPE_AVCODECPARAMETERS = 1, // AVCodecParameters*
    PRIVATE_DATA_TYPE_VIDEO_ENC = 2,
    PRIVATE_DATA_TYPE_AUDIO_ENC = 3,
    PRIVATE_DATA_TYPE_DECODER_ID = 4
    // add more types if needed
} PRIVATE_DATA_TYPE;

class FFmpegMediaPacketPrivate {
public:
    FFmpegMediaPacketPrivate() = default;
    ~FFmpegMediaPacketPrivate() = default;
public:
    PRIVATE_DATA_TYPE private_type_ = PRIVATE_DATA_TYPE_UNKNOWN;
    void* private_data_ = nullptr;
    bool private_data_owner_ = false; // whether private_data_ need to be freed in destructor
    AVCodecID codec_id_ = AV_CODEC_ID_NONE; // AVCodecID
};

typedef struct {
    AVCodecID codec_id;
    AVPixelFormat pix_fmt; // AVPixelFormat
    int width;
    int height;
    int fps; // fps * 1000
    int bitrate; // kbps
    int gop; // keyframe interval in seconds
    int threads; // encoding threads
	std::string profile; // high, main, baseline for H264
	std::string tune; // film, animation, grain, stillimage, fastdecode, zerolatency for H264
	std::string preset; // ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow for H264
    int rc_mode; // rate control mode, 0: CBR, 1: VBR, 2: ABR
    int max_bitrate; // kbps, for VBR
    int min_bitrate; // kbps, for ABR
    int buf_size; // kbps, for CBR
    int qp; // for CQP
    int max_qp; // for CQP
    int min_qp; // for CQP
    int vbv_maxrate; // kbps, for H264/H265 CBR/VBR
    int vbv_bufsize; // kbps, for H264/H265 CBR/VBR
} VideoEncInfo;

typedef struct {
    AVCodecID codec_id;
    int sample_rate; // Hz
    int channels; // number of channels
    int bitrate; // kbps
    AVSampleFormat sample_fmt; // AVSampleFormat
    int frame_size; // number of samples per frame
} AudioEncInfo;

class FFmpegMediaPacket
{
public:
    FFmpegMediaPacket(AVPacket* pkt, cpp_streamer::MEDIA_PKT_TYPE pkt_type) {
        pkt_ = pkt;
        frame_ = nullptr;
        pkt_type_ = pkt_type;

        if (pkt->time_base.den > 0 && pkt->time_base.num > 0) {
            // change to AV_TIME_BASE;
			pkt_dts_us_ = pkt->dts * AV_TIME_BASE * pkt->time_base.num / pkt->time_base.den;
			pkt_pts_us_ = pkt->pts * AV_TIME_BASE * pkt->time_base.num / pkt->time_base.den;
		}
    }
    FFmpegMediaPacket(AVFrame* frame, cpp_streamer::MEDIA_PKT_TYPE pkt_type) {
        pkt_ = nullptr;
        frame_ = frame;
        pkt_type_ = pkt_type;
    }
    // Copy constructor: create new references/allocations for underlying AV structures
    FFmpegMediaPacket(const FFmpegMediaPacket& other) {
        pkt_ = nullptr;
        frame_ = nullptr;
        pkt_type_ = other.pkt_type_;
        id_ = other.id_;
        if (other.pkt_) {
            pkt_ = av_packet_alloc();
            if (pkt_) {
                if (av_packet_ref(pkt_, other.pkt_) < 0) {
                    av_packet_free(&pkt_);
                    pkt_ = nullptr;
                }
            }
        }
        if (other.frame_) {
            frame_ = av_frame_alloc();
            if (frame_) {
                if (av_frame_ref(frame_, other.frame_) < 0) {
                    av_frame_free(&frame_);
                    frame_ = nullptr;
                }
            }
        }
    }

    // Copy assignment
    FFmpegMediaPacket& operator=(const FFmpegMediaPacket& other) {
        if (this == &other) return *this;
        // copy id
        id_ = other.id_;
        // copy pkt_type
        pkt_type_ = other.pkt_type_;

        // free existing
        if (pkt_) {
            av_packet_free(&pkt_);
            pkt_ = nullptr;
        }
        if (frame_) {
            av_frame_free(&frame_);
            frame_ = nullptr;
        }

        if (other.pkt_) {
            pkt_ = av_packet_alloc();
            if (pkt_) {
                if (av_packet_ref(pkt_, other.pkt_) < 0) {
                    av_packet_free(&pkt_);
                    pkt_ = nullptr;
                }
            }
        }

        if (other.frame_) {
            frame_ = av_frame_alloc();
            if (frame_) {
                if (av_frame_ref(frame_, other.frame_) < 0) {
                    av_frame_free(&frame_);
                    frame_ = nullptr;
                }
            }
        }

        return *this;
    }
    ~FFmpegMediaPacket() {
        if (pkt_) {
            av_packet_free(&pkt_);
            pkt_ = nullptr;
        }
        if (frame_) {
            av_frame_free(&frame_);
            frame_ = nullptr;
        }
        if (prv_.private_data_) {
            if (prv_.private_data_owner_) {
                if (prv_.private_type_ == PRIVATE_DATA_TYPE_AVCODECPARAMETERS) {
                    AVCodecParameters* params = (AVCodecParameters*)prv_.private_data_;
                    avcodec_parameters_free(&params);
                }
            }
            prv_.private_data_ = nullptr;
        }
    }

    AVPacket* GetAVPacket() const { return pkt_; }
	bool AVPacketIsKey() const { return pkt_ && (pkt_->flags & AV_PKT_FLAG_KEY); }  
    AVFrame* GetAVFrame() const { return frame_; }
    bool IsAVPacket() const { return pkt_ != nullptr; }
    bool IsAVFrame() const { return frame_ != nullptr; }
    FFmpegMediaPacketPrivate GetPrivateData() const { return prv_; }
    void SetPrivateData(FFmpegMediaPacketPrivate data) { prv_ = data; }
    cpp_streamer::MEDIA_PKT_TYPE GetMediaPktType() const { return pkt_type_; }
    
	int64_t GetPktDtsUs() const { return pkt_dts_us_; } // in us
	int64_t GetPktPtsUs() const { return pkt_pts_us_; } // in us

    std::string Dump() {
        std::string info;
        if (pkt_) {
            info += "AVPacket: ";
            info += "size=" + std::to_string(pkt_->size);
            info += ", pts=" + std::to_string(pkt_->pts);
            info += ", dts=" + std::to_string(pkt_->dts);
            info += ", stream_index=" + std::to_string(pkt_->stream_index);
            if (pkt_->time_base.num > 0 && pkt_->time_base.den > 0) {
                info += ", time_base=" + std::to_string(pkt_->time_base.num) + "/" + std::to_string(pkt_->time_base.den);
				int64_t dts = pkt_->dts * pkt_->time_base.num * 1000 / pkt_->time_base.den;
				int64_t pts = pkt_->pts * pkt_->time_base.num * 1000 / pkt_->time_base.den;
                info += ", dts_ms=" + std::to_string(dts);
				info += ", pts_ms=" + std::to_string(pts);
			}
            if (pkt_type_ == cpp_streamer::MEDIA_VIDEO_TYPE) {
                if (pkt_->flags & AV_PKT_FLAG_KEY) {
                    info += ", keyframe";
                }
                else {
                    info += ", non-keyframe";
                }
            }
        }
        if (frame_) {
            info += "AVFrame: ";
            if (pkt_type_ == cpp_streamer::MEDIA_VIDEO_TYPE) {
                enum AVPixelFormat format = (enum AVPixelFormat)frame_->format;
                const char* fmt_name = av_get_pix_fmt_name(format);
                info += "width=" + std::to_string(frame_->width);
                info += ", height=" + std::to_string(frame_->height);

                if (fmt_name) {
                    info += ", format=" + std::string(fmt_name);
                }
            }
            if (pkt_type_ == cpp_streamer::MEDIA_AUDIO_TYPE) {
                enum AVSampleFormat format = (enum AVSampleFormat)frame_->format;
                const char* fmt_name = av_get_sample_fmt_name(format);
                info += "nb_samples=" + std::to_string(frame_->nb_samples);
                info += ", channels=" + std::to_string(frame_->ch_layout.nb_channels);
                info += ", sample_rate=" + std::to_string(frame_->sample_rate);
                if (fmt_name) {
                    info += ", format=" + std::string(fmt_name);
                }
            }

            info += ", format=" + std::to_string(frame_->format);
            info += ", pts=" + std::to_string(frame_->pts);
			info += ", time_base=" + std::to_string(frame_->time_base.num) + "/" + std::to_string(frame_->time_base.den);

            if (frame_->time_base.num > 0 && frame_->time_base.den > 0) {
                int64_t pts_ms = frame_->pts * 1000 * frame_->time_base.num / frame_->time_base.den;
                info += ", pts_ms=" + std::to_string(pts_ms);
            }
            for (size_t i = 0; i < AV_NUM_DATA_POINTERS; i++) {
                int line_size = frame_->linesize[i];
                if (line_size <= 0) {
                    break;
                }
				info += ", linesize[" + std::to_string(i) + "]=" + std::to_string(line_size);
            }
        }
        if (pkt_type_ == cpp_streamer::MEDIA_VIDEO_TYPE) {
            info += ", type=video";
        }
        else if (pkt_type_ == cpp_streamer::MEDIA_AUDIO_TYPE) {
            info += ", type=audio";
        }
        else {
            info += ", type=unknown";
        }
        if (prv_.private_data_) {
            if (prv_.private_type_ == PRIVATE_DATA_TYPE_AVCODECPARAMETERS) {
                AVCodecParameters* params = (AVCodecParameters*)prv_.private_data_;
                const char* codec_name = avcodec_get_name(params->codec_id);
                info += ", codec_name=" + std::string(codec_name);
                info += ", extradata_size=" + std::to_string(params->extradata_size);
                if (params->framerate.num > 0 && params->framerate.den > 0) {
                    info += ", framerate=" + std::to_string(params->framerate.num) + "/" + std::to_string(params->framerate.den);
				}
                if (params->bit_rate > 0) {
                    info += ", bit_rate=" + std::to_string(params->bit_rate);
				}
                if (pkt_type_ == cpp_streamer::MEDIA_AUDIO_TYPE) {
                    if (params->sample_rate > 0) {
                        info += ", sample_rate=" + std::to_string(params->sample_rate);
					}
                }
            }
        }
		return info;
    }
    std::string GetId() const { return id_; }
    void SetId(const std::string& id) { id_ = id; }

private:
    cpp_streamer::MEDIA_PKT_TYPE pkt_type_ = cpp_streamer::MEDIA_UNKNOWN_TYPE;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;
    FFmpegMediaPacketPrivate prv_;
    std::string id_;

private:
    int64_t pkt_dts_us_ = -1;
    int64_t pkt_pts_us_ = -1;
};

class SinkCallbackI
{
public:
    virtual void OnData(std::shared_ptr<FFmpegMediaPacket> pkt) = 0;
};

typedef enum
{
	MEDIA_REPORT_UNKNOWN = 0,
	MEDIA_PULL_END_REPORT = 1,
} REPORT_TYPE;

class MediaReportI
{
public:
    virtual void OnReportEvent(REPORT_TYPE report_type, const std::string& id, const std::string& message) = 0;
};

inline std::string DumpVideoEncInfo(const VideoEncInfo& enc_info) {
    std::string dump;
    const char* codec_name = avcodec_get_name(enc_info.codec_id);
    dump += "codec_name=" + std::string(codec_name);
    dump += ", width=" + std::to_string(enc_info.width);
    dump += ", height=" + std::to_string(enc_info.height);
    dump += ", fps=" + std::to_string(enc_info.fps);
    dump += ", bitrate=" + std::to_string(enc_info.bitrate);
    dump += ", gop=" + std::to_string(enc_info.gop);
    dump += ", threads=" + std::to_string(enc_info.threads);
    dump += ", preset=" + enc_info.preset;
    dump += ", tune=" + enc_info.tune;
    dump += ", profile=" + enc_info.profile;
    dump += ", preset=" + enc_info.preset;
    dump += ", rc_mode=" + std::to_string(enc_info.rc_mode);
    dump += ", max_bitrate=" + std::to_string(enc_info.max_bitrate);
    dump += ", min_bitrate=" + std::to_string(enc_info.min_bitrate);
    dump += ", buf_size=" + std::to_string(enc_info.buf_size);
    dump += ", qp=" + std::to_string(enc_info.qp);
    dump += ", max_qp=" + std::to_string(enc_info.max_qp);
    dump += ", min_qp=" + std::to_string(enc_info.min_qp);
    dump += ", vbv_maxrate=" + std::to_string(enc_info.vbv_maxrate);
    dump += ", vbv_bufsize=" + std::to_string(enc_info.vbv_bufsize);
    return dump;
}

inline std::string DumpAudioEncInfo(const AudioEncInfo& enc_info) {
    std::string dump;
    const char* codec_name = avcodec_get_name(enc_info.codec_id);
    dump += "codec_name=" + std::string(codec_name);
    dump += ", sample_rate=" + std::to_string(enc_info.sample_rate);
    dump += ", channels=" + std::to_string(enc_info.channels);
    dump += ", bitrate=" + std::to_string(enc_info.bitrate);
    const char* sample_fmt_name = av_get_sample_fmt_name(enc_info.sample_fmt);
    dump += ", sample_fmt=" + std::string(sample_fmt_name);
    dump += ", frame_size=" + std::to_string(enc_info.frame_size);
    return dump;
}

inline void InitVideoEncInfo(VideoEncInfo& enc_info) {
    enc_info.codec_id = AV_CODEC_ID_H264;
    enc_info.pix_fmt = AV_PIX_FMT_YUV420P;
    enc_info.width = 1280;
    enc_info.height = 720;
    enc_info.fps = 30;
    enc_info.bitrate = 2 * 1000 * 1000; // kbps
    enc_info.gop = 2; // keyframe interval in seconds
    enc_info.threads = 1;
    enc_info.preset = "ultrafast"; // ultrafast
    enc_info.tune = "zerolatency"; // zerolatency
    enc_info.profile = "baseline"; // basline
    enc_info.rc_mode = 0; // CBR
    enc_info.max_bitrate = 0; // for VBR
    enc_info.min_bitrate = 0; // for ABR
    enc_info.buf_size = 2 * 1000 * 1000; // for CBR, in kbps
    enc_info.qp = 23; // for CQP
    enc_info.max_qp = 0; // for CQP
    enc_info.min_qp = 0; // for CQP
    enc_info.vbv_maxrate = 2 * 1000 * 10000; // for H264/H265 CBR/VBR, in kbps
    enc_info.vbv_bufsize = 2 * 1000 * 10000; // for H264/H265 CBR/VBR, in kbps
}

inline void InitAudioEncInfo(AudioEncInfo& enc_info) {
    enc_info.codec_id = AV_CODEC_ID_AAC;
    enc_info.sample_rate = 44100; // Hz
    enc_info.channels = 2;
    enc_info.bitrate = 128 * 1000; // kbps
    enc_info.sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc_info.frame_size = 2048; // number of samples per frame
}

inline void GetTargetVideoSize(int base_size, int src_width, int src_height, int& target_width, int& target_height) {
    if (src_width > src_height) {
        target_width = int(base_size * ((float)src_width / (float)src_height));
        target_height = base_size;
    } else {
        target_width = base_size;
		target_height = int(base_size * ((float)src_height / (float)src_width));
    }
    // Ensure dimensions are even numbers
    if (target_width % 2 != 0) {
        target_width++;
    }
    if (target_height % 2 != 0) {
        target_height++;
    }
}

inline AVPacket* GenerateAVPacket(uint8_t* data, 
    int size, int64_t pts, int64_t dts, int stream_index, AVRational time_base) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return nullptr;
    }
    int ret = av_new_packet(pkt, size);
    if (ret < 0) {
        av_packet_free(&pkt);
        return nullptr;
    }
    memcpy(pkt->data, data, size);
    pkt->pts = pts;
    pkt->dts = dts;
    pkt->stream_index = stream_index;
    pkt->time_base = time_base;
    return pkt;
}
#endif

