#include <jni.h>
#include <android/log.h>

#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

/*
 * 添加ADTS头部
 */
void adts_header(int dataLen, FILE *pFile) {
    char szAdtsHeader[7];

    /**
     * AAC编码级别
     */
    const int audio_object_type = 2;
    /**
     * 采样率对应的索引值
     */
    const int sampling_frequency_index = 4;
    /**
     * 声道数
     */
    const int channel_config = 1;
    /**
     * 编码数据长度加ADTS头部的长度
     */
    const int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;         //syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;         //syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1);    //Layer:0                                 2bits
    szAdtsHeader[1] |= 1;           //protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1)
            << 6;            //profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)
            << 2; //sampling frequency index:sampling_frequency_index  4bits
    szAdtsHeader[2] |= (0
            << 1);                             //private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04)
            >> 2;           //channel configuration:channel_config               高1bit

    szAdtsHeader[3] =
            (channel_config & 0x03) << 6;     //channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0
            << 5);                      //original：0                               1bit
    szAdtsHeader[3] |= (0
            << 4);                      //home：0                                   1bit
    szAdtsHeader[3] |= (0
            << 3);                      //copyright id bit：0                       1bit
    szAdtsHeader[3] |= (0
            << 2);                      //copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t) ((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t) ((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;

    fwrite(szAdtsHeader, 1, 7, pFile);
}

/**
 * 添加SPS/PPS
 * @param out
 * @param sps_pps
 * @param sps_pps_size
 * @param in
 * @param in_size
 * @return
 */
static int alloc_and_copy(AVPacket *out,
                          const uint8_t *sps_pps, uint32_t sps_pps_size,
                          const uint8_t *in, uint32_t in_size) {
    uint32_t offset = out->size;
    uint8_t nal_header_size = 4;
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);
    if (err < 0)
        return err;

    if (sps_pps)
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(out->data + sps_pps_size, 1);
    } else {
        (out->data + offset + sps_pps_size)[0] =
        (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}

/**
 * 添加start code
 * @param codec_extradata
 * @param codec_extradata_size
 * @param out_extradata
 * @param padding
 * @return
 */
int h264_extradata_to_annexb(const uint8_t *codec_extradata, const int codec_extradata_size,
                             AVPacket *out_extradata, int padding) {
    uint16_t unit_size;
    uint64_t total_size = 0;
    uint8_t *out = NULL, unit_nb, sps_done = 0, sps_seen = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
    const uint8_t *extradata = codec_extradata + 4;
    static const uint8_t nalu_header[4] = {0, 0, 0, 1};
    int length_size =
            (*extradata++ & 0x3) + 1; // retrieve length coded size, 用于指示表示NALU数据长度所需占用的字节数

    sps_offset = pps_offset = -1;

    unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        goto pps;
    } else {
        sps_offset = 0;
        sps_seen = 1;
    }

    while (unit_nb--) {
        int err;

        unit_size = AV_RB16(extradata);
        total_size += unit_size + 4;
        if (total_size > INT_MAX - padding) {
            av_log(NULL, AV_LOG_ERROR,
                   "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size) {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                                       "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;
        pps:
        if (!unit_nb && !sps_done++) {
            unit_nb = *extradata++;
            if (unit_nb) {
                pps_offset = total_size;
                pps_seen = 1;
            }
        }
    }

    if (out)
        memset(out + total_size, 0, padding);

    if (!sps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(NULL, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    out_extradata->data = out;
    out_extradata->size = total_size;

    return length_size;
}


int h264_mp4toannexb(AVFormatContext *fmt_ctx, AVPacket *in, FILE *dst_fd) {

    AVPacket *out = NULL;
    AVPacket spspps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size = 0;
    const uint8_t *buf;
    const uint8_t *buf_end;
    int buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf = in->data;
    buf_size = in->size;
    buf_end = in->data + in->size;

    do {
        ret = AVERROR(EINVAL);
        if (buf + 4 > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i < 4; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf += 4;
        unit_type = *buf & 0x1f;

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;
        if (unit_type == 5) {
            h264_extradata_to_annexb(fmt_ctx->streams[in->stream_index]->codec->extradata,
                                     fmt_ctx->streams[in->stream_index]->codec->extradata_size,
                                     &spspps_pkt,
                                     AV_INPUT_BUFFER_PADDING_SIZE);
            if ((ret = alloc_and_copy(out,
                                      spspps_pkt.data, spspps_pkt.size,
                                      buf, nal_size)) < 0)
                goto fail;

        } else {
            if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;
        }
        len = fwrite(out->data, 1, out->size, dst_fd);
        if (len != out->size) {
            av_log(NULL, AV_LOG_DEBUG,
                   "warning, length of writed data isn't equal pkt.size(%d, %d)\n",
                   len,
                   out->size);
        }
        fflush(dst_fd);

        next_nal:
        buf += nal_size;
        cumul_size += nal_size + 4;
    } while (cumul_size < buf_size);

    fail:
    av_packet_free(&out);

    return ret;
}

/**
 * 提取音频数据
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_MediaOperationManager_extractAudio(JNIEnv *env, jobject thiz,
                                                                jstring input_path,
                                                                jstring output_path) {
    const char *inputFilePath = env->GetStringUTFChars(input_path, NULL);
    const char *outputFilePath = env->GetStringUTFChars(output_path, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "%s %s", inputFilePath, outputFilePath);
    AVFormatContext *avFormatContext = NULL;
    const int openResult = avformat_open_input(&avFormatContext, inputFilePath, NULL, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "open result: %s", av_err2str(openResult));
    const int audioStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1,
                                                     NULL, 0);
    FILE *outputFile = fopen(env->GetStringUTFChars(output_path, NULL), "wb");
    AVPacket avPacket;
    av_init_packet(&avPacket);
    avPacket.data = NULL;
    avPacket.size = 0;
    while (av_read_frame(avFormatContext, &avPacket) >= 0) {
        if (avPacket.stream_index != audioStreamIndex) {
            continue;
        }
        adts_header(avPacket.size, outputFile);
        fwrite(avPacket.data, 1, avPacket.size, outputFile);
        av_packet_unref(&avPacket);
    }
    fclose(outputFile);
    outputFile = NULL;
    avformat_close_input(&avFormatContext);
    avFormatContext = NULL;
}

/**
 * 提取视频数据
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_MediaOperationManager_extractVideo(JNIEnv *env, jobject thiz,
                                                                jstring input_path,
                                                                jstring output_path) {
    const char *inputFilePath = env->GetStringUTFChars(input_path, NULL);
    const char *outputFilePath = env->GetStringUTFChars(output_path, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "%s %s", inputFilePath, outputFilePath);
    AVFormatContext *avFormatContext = NULL;
    const int openResult = avformat_open_input(&avFormatContext, inputFilePath, NULL, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "open result: %s", av_err2str(openResult));
    const int videoStreamIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1,
                                                     NULL, 0);
    AVPacket avPacket;
    av_init_packet(&avPacket);
    avPacket.data = NULL;
    avPacket.size = 0;
    FILE *outputFile = fopen(env->GetStringUTFChars(output_path, NULL), "wb");
    while (av_read_frame(avFormatContext, &avPacket) >= 0) {
        if (avPacket.stream_index != videoStreamIndex) {
            continue;
        }
        //读出来视频编码数据，添加start code 和SPS/PPS
        h264_mp4toannexb(avFormatContext, &avPacket, outputFile);

        //释放AVPacket data数据防止内存泄漏
        av_packet_unref(&avPacket);
    }

    fclose(outputFile);
    outputFile = NULL;
    avformat_close_input(&avFormatContext);
    avFormatContext = NULL;
}


/**
 * MP4转换成FLV
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_MediaOperationManager_mp4CnvertToFLV(JNIEnv *env, jobject thiz,
                                                                  jstring input_path,
                                                                  jstring output_path) {
    const char *inputFilePath = env->GetStringUTFChars(input_path, NULL);
    const char *outputFilePath = env->GetStringUTFChars(output_path, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "%s %s", inputFilePath, outputFilePath);
    AVFormatContext *inputAvFormatContext = NULL;
    //分配一个输入的AVFormatContext
    const int openResult = avformat_open_input(&inputAvFormatContext, inputFilePath, NULL, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", "open result: %s", av_err2str(openResult));
    //获取流信息保存至AVStream结构中
    avformat_find_stream_info(inputAvFormatContext, NULL);
    AVFormatContext *outputAVFormatContext = NULL;
    //分配一个输出的AVFormatContext
    avformat_alloc_output_context2(&outputAVFormatContext, NULL, NULL, outputFilePath);
    //多媒体文件中的流的数量
    const int streamNums = inputAvFormatContext->nb_streams;
    //将输入的多媒体文件中的各种流数据写入到输出的AVFormatContext中
    for (int i = 0; i < streamNums; i++) {
        const AVStream *inputStream = inputAvFormatContext->streams[i];
        const AVCodecParameters *avCodecParameters = inputStream->codecpar;
        if (avCodecParameters->codec_type != AVMEDIA_TYPE_VIDEO
            && avCodecParameters->codec_type != AVMEDIA_TYPE_AUDIO
            && avCodecParameters->codec_type != AVMEDIA_TYPE_SUBTITLE
                ) {
            continue;
        }
        const AVStream *outputStream = avformat_new_stream(outputAVFormatContext, NULL);
        avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
        outputStream->codecpar->codec_tag = 0;
    }
    const AVOutputFormat *avOutputFormat = outputAVFormatContext->oformat;
    //打开输出文件
    if (!(avOutputFormat->flags & AVFMT_NOFILE)) {
        avio_open(&outputAVFormatContext->pb, outputFilePath, AVIO_FLAG_WRITE);
    }
    //向输出文件写入头部
    avformat_write_header(outputAVFormatContext, NULL);
    //向输出文件中写入媒体数据
    AVPacket avPacket;
    while (av_read_frame(inputAvFormatContext, &avPacket) >= 0) {
        const AVStream *inputAVStream = inputAvFormatContext->streams[avPacket.stream_index];
        const AVStream *outputAVStream = outputAVFormatContext->streams[avPacket.stream_index];

        //时间基转换
        avPacket.pts = av_rescale_q_rnd(avPacket.pts, inputAVStream->time_base,
                                        outputAVStream->time_base,
                                        (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        avPacket.dts = av_rescale_q_rnd(avPacket.dts, inputAVStream->time_base,
                                        outputAVStream->time_base,
                                        (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        avPacket.duration = av_rescale_q(avPacket.duration, inputAVStream->time_base,
                                         outputAVStream->time_base);
        avPacket.pos = -1;
        //交错写入至多媒体文件中
        av_interleaved_write_frame(outputAVFormatContext, &avPacket);
        av_packet_unref(&avPacket);
    }
    //写入文件尾部
    av_write_trailer(outputAVFormatContext);
    avformat_close_input(&inputAvFormatContext);
    inputAvFormatContext = NULL;
    avformat_free_context(outputAVFormatContext);
    outputAVFormatContext = NULL;
}

/**
 * 裁剪视频
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_MediaOperationManager_cutVideo(JNIEnv *env, jobject thiz,
                                                            jstring input_path,
                                                            jstring output_path, jint start_time,
                                                            jint end_time) {
    const char *inputFilePath = env->GetStringUTFChars(input_path, NULL);
    const char *outputFilePath = env->GetStringUTFChars(output_path, NULL);
    const long startTime = start_time;
    const long endTime = end_time;
    AVFormatContext *inputAVFormat = NULL;
    avformat_open_input(&inputAVFormat, inputFilePath, NULL, NULL);
    avformat_find_stream_info(inputAVFormat, NULL);
    AVFormatContext *outputAVFormat = NULL;
    avformat_alloc_output_context2(&outputAVFormat, NULL, NULL, outputFilePath);
    for (int i = 0; i < inputAVFormat->nb_streams; i++) {
        const AVStream *inputStream = inputAVFormat->streams[i];
        const AVStream *outputStream = avformat_new_stream(outputAVFormat, NULL);
        avcodec_parameters_copy(outputStream->codecpar, inputStream->codecpar);
    }
    const AVOutputFormat *outputFormat = outputAVFormat->oformat;
    if (!(outputFormat->flags & AVFMT_NOFILE)) {
        avio_open(&(outputAVFormat->pb), outputFilePath, AVIO_FLAG_WRITE);
    }
    avformat_write_header(outputAVFormat, NULL);

    av_seek_frame(inputAVFormat, -1, startTime * AV_TIME_BASE, AVSEEK_FLAG_ANY);

    int64_t *dtsStartFrom = (int64_t *) (malloc(
            sizeof(int64_t) * inputAVFormat->nb_streams));
    memset(dtsStartFrom, 0, sizeof(int64_t) * inputAVFormat->nb_streams);
    int64_t *ptsStartFrom = (int64_t *) malloc(sizeof(int64_t) * inputAVFormat->nb_streams);
    memset(ptsStartFrom, 0, sizeof(int64_t) * inputAVFormat->nb_streams);
    AVPacket avPacket;
    av_init_packet(&avPacket);
    while (av_read_frame(inputAVFormat, &avPacket) >= 0) {
        const AVStream *inputStream = inputAVFormat->streams[avPacket.stream_index];
        const AVStream *outputStream = outputAVFormat->streams[avPacket.stream_index];

        if (av_q2d(inputStream->time_base) * avPacket.pts > endTime) {
            av_packet_unref(&avPacket);
            break;
        }
        if (dtsStartFrom[avPacket.stream_index] == 0) {
            dtsStartFrom[avPacket.stream_index] = avPacket.dts;
        }
        if (ptsStartFrom[avPacket.stream_index] == 0) {
            ptsStartFrom[avPacket.stream_index] = avPacket.pts;
        }

        avPacket.pts = av_rescale_q_rnd(avPacket.pts - ptsStartFrom[avPacket.stream_index],
                                        inputStream->time_base, outputStream->time_base,
                                        (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        avPacket.dts = av_rescale_q_rnd(avPacket.dts - dtsStartFrom[avPacket.stream_index],
                                        inputStream->time_base,
                                        outputStream->time_base,
                                        (AVRounding) (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        if (avPacket.pts < 0) {
            avPacket.pts = 0;
        }
        if (avPacket.dts < 0) {
            avPacket.dts = 0;
        }
        avPacket.duration = (int) av_rescale_q((int64_t) avPacket.duration, inputStream->time_base,
                                               outputStream->time_base);
        avPacket.pos = -1;

        av_interleaved_write_frame(outputAVFormat, &avPacket);
        av_packet_unref(&avPacket);
    }
    av_write_trailer(outputAVFormat);
    avio_close(outputAVFormat->pb);
    avformat_close_input(&inputAVFormat);
    avformat_free_context(outputAVFormat);
    free(dtsStartFrom);
    free(ptsStartFrom);
}


extern "C" JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_MediaOperationManager_encodeH264(JNIEnv *env, jobject thiz,
                                                              jstring output_path) {

    uint8_t endcode[] = {0, 0, 1, 0xb7};
    AVCodec *avCodec = avcodec_find_encoder_by_name("libx264");
    AVCodecContext *avCodecContext = avcodec_alloc_context3(avCodec);
    avCodecContext->bit_rate = 40000;
    avCodecContext->width = 720;
    avCodecContext->height = 1280;
    //时间基以帧率为基准，一秒25帧，那么时间刻度就是1/25
    avCodecContext->time_base = (AVRational) {1, 25};
    //帧率
    avCodecContext->framerate = (AVRational) {25, 1};
    //GOP大小，关键帧的间隔
    avCodecContext->gop_size = 10;
    //GOP中B帧数量
    avCodecContext->max_b_frames = 1;
    //原始数据中的像素格式
    avCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    //如果是H264的编码器，设置一个参数
    if (avCodec->id == AV_CODEC_ID_H264) {
        //针对于H264使用预设参数preset slow级别，慢速度进行压缩，保证H264质量优先
        av_opt_set(avCodecContext->priv_data, "preset", "slow", 0);
    }
    //打开编解码器
    int openResult = avcodec_open2(avCodecContext, avCodec, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "FFmpeg", "open result:%s", av_err2str(openResult));
    const char *outputFilePath = env->GetStringUTFChars(output_path, NULL);
    FILE *outputFile = fopen(outputFilePath, "wb");
    AVFrame *avFrame = av_frame_alloc();
    avFrame->width = avCodecContext->width;
    avFrame->height = avCodecContext->height;
    avFrame->format = avCodecContext->pix_fmt;
    const int getBufferResult = av_frame_get_buffer(avFrame, 0);
    if (getBufferResult < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "FFmpeg", "frame_get_buffer result:%s",
                            av_err2str(getBufferResult));
    }
    AVPacket avPacket;
    int y, x, output/*AVFrame是否压缩成一个数据包AVPacket成功*/;
    for (int i = 0; i < 25; i++) {
        av_init_packet(&avPacket);
        avPacket.data = NULL;
        avPacket.size = 0;
        fflush(stdout);
        const int ret = av_frame_make_writable(avFrame);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "FFmpeg", "av_frame_make_writable result:%s",
                                av_err2str(ret));
        }
        for (y = 0; y < avCodecContext->height; y++) {
            for (x = 0; x < avCodecContext->width; x++) {
                avFrame->data[0][y * avFrame->linesize[0] + x] = x + y + i * 3;
            }
        }
        /* Cb and Cr */
        for (y = 0; y < avCodecContext->height / 2; y++) {
            for (x = 0; x < avCodecContext->width / 2; x++) {
                avFrame->data[1][y * avFrame->linesize[1] + x] = 128 + y + i * 2;
                avFrame->data[2][y * avFrame->linesize[2] + x] = 64 + x + i * 5;
            }
        }
        avFrame->pts = i;

        avcodec_encode_video2(avCodecContext, &avPacket, avFrame, &output);
        if (output) {
            fwrite(avPacket.data, 1, avPacket.size, outputFile);
            av_packet_unref(&avPacket);
        }
    }

    /* get the delayed frames */
    for (output = 1; output;) {
        const int ret = avcodec_encode_video2(avCodecContext, &avPacket, NULL, &output);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "FFmpeg", "flush result:%s",
                                av_err2str(ret));
        }
        if (output) {
            fwrite(avPacket.data, 1, avPacket.size, outputFile);
            av_packet_unref(&avPacket);
        }
    }
    /* add sequence end code to have a real MPEG file */
    fwrite(endcode, 1, sizeof(endcode), outputFile);
    fclose(outputFile);
    avcodec_free_context(&avCodecContext);
    av_frame_free(&avFrame);
}