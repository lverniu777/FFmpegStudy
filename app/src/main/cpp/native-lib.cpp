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

    /* retrieve sps and pps unit(s) */
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
            unit_nb = *extradata++; /* number of pps unit(s) */
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
        if (buf + 4 /*s->length_size*/ > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i < 4/*s->length_size*/; i++)
            nal_size = (nal_size << 8) | buf[i];

        buf += 4; /*s->length_size;*/
        unit_type = *buf & 0x1f;

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;

        /*
        if (unit_type == 7)
            s->idr_sps_seen = s->new_idr = 1;
        else if (unit_type == 8) {
            s->idr_pps_seen = s->new_idr = 1;
            */
        /* if SPS has not been seen yet, prepend the AVCC one to PPS */
        /*
        if (!s->idr_sps_seen) {
            if (s->sps_offset == -1)
                av_log(ctx, AV_LOG_WARNING, "SPS not present in the stream, nor in AVCC, stream may be unreadable\n");
            else {
                if ((ret = alloc_and_copy(out,
                                     ctx->par_out->extradata + s->sps_offset,
                                     s->pps_offset != -1 ? s->pps_offset : ctx->par_out->extradata_size - s->sps_offset,
                                     buf, nal_size)) < 0)
                    goto fail;
                s->idr_sps_seen = 1;
                goto next_nal;
            }
        }
    }
    */

        /* if this is a new IDR picture following an IDR picture, reset the idr flag.
         * Just check first_mb_in_slice to be 0 as this is the simplest solution.
         * This could be checking idr_pic_id instead, but would complexify the parsing. */
        /*
        if (!s->new_idr && unit_type == 5 && (buf[1] & 0x80))
            s->new_idr = 1;

        */
        /* prepend only to the first type 5 NAL unit of an IDR picture, if no sps/pps are already present */
        if (/*s->new_idr && */unit_type == 5 /*&& !s->idr_sps_seen && !s->idr_pps_seen*/) {


            h264_extradata_to_annexb(fmt_ctx->streams[in->stream_index]->codec->extradata,
                                     fmt_ctx->streams[in->stream_index]->codec->extradata_size,
                                     &spspps_pkt,
                                     AV_INPUT_BUFFER_PADDING_SIZE);

            if ((ret = alloc_and_copy(out,
                                      spspps_pkt.data, spspps_pkt.size,
                                      buf, nal_size)) < 0)
                goto fail;
            /*s->new_idr = 0;*/
            /* if only SPS has been seen, also insert PPS */
        }
            /*else if (s->new_idr && unit_type == 5 && s->idr_sps_seen && !s->idr_pps_seen) {
                if (s->pps_offset == -1) {
                    av_log(ctx, AV_LOG_WARNING, "PPS not present in the stream, nor in AVCC, stream may be unreadable\n");
                    if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                        goto fail;
                } else if ((ret = alloc_and_copy(out,
                                            ctx->par_out->extradata + s->pps_offset, ctx->par_out->extradata_size - s->pps_offset,
                                            buf, nal_size)) < 0)
                    goto fail;
            }*/ else {
            if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;
            /*
            if (!s->new_idr && unit_type == 1) {
                s->new_idr = 1;
                s->idr_sps_seen = 0;
                s->idr_pps_seen = 0;
            }
            */
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
        cumul_size += nal_size + 4;//s->length_size;
    } while (cumul_size < buf_size);

    /*
    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    */
    fail:
    av_packet_free(&out);

    return ret;
}

/**
 * 提取音频数据
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_Demo_extractAudio(JNIEnv *env, jobject thiz, jstring input_path,
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
//        char adtsHeader[7];
        adts_header(avPacket.size, outputFile);
//        fwrite(adtsHeader, 1, 7, outputFile);
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
Java_com_example_ffmpegstudy_Demo_extractVideo(JNIEnv *env, jobject thiz, jstring input_path,
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
extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpegstudy_Demo_mp4CnvertToFLV(JNIEnv *env, jobject thiz, jstring input_path,
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
        //使用枚举进行或运算AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX会报错，不知为何no matching function for call to 'av_rescale_q_rnd'
        //因此这里使用单一的枚举
        avPacket.pts = av_rescale_q_rnd(avPacket.pts, inputAVStream->time_base,
                                        outputAVStream->time_base, AV_ROUND_NEAR_INF);
        avPacket.dts = av_rescale_q_rnd(avPacket.dts, inputAVStream->time_base,
                                        outputAVStream->time_base,
                                        AV_ROUND_NEAR_INF);
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