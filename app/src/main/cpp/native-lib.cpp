#include <jni.h>
#include <string>
#include <android/log.h>
#include <iostream>
#include <string>

using namespace std;


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

/*
 * 添加ADTS头部
 */
void adts_header(char *szAdtsHeader, int dataLen) {

    int audio_object_type = 2;
    int sampling_frequency_index = 4;
    int channel_config = 1;

    int adtsLen = dataLen + 7;

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
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_ffmpegstudy_Demo_getAVFormat(JNIEnv *env, jobject thiz, jstring input_file_path,
                                              jstring out_file_path) {
    const char *inputFilePath = env->GetStringUTFChars(input_file_path, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", inputFilePath);
    AVFormatContext *avFormatContext = NULL;
    int ret = avformat_open_input(&avFormatContext, inputFilePath, NULL, NULL);
    __android_log_print(ANDROID_LOG_ERROR, "AV", av_err2str(ret));
    string metaInfo = "bitRate: " + to_string(avFormatContext->duration);
    FILE *outputFile = fopen(env->GetStringUTFChars(out_file_path, NULL), "wb");
    ret = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    AVPacket avPacket;
    av_init_packet(&avPacket);
    while (av_read_frame(avFormatContext, &avPacket) >= 0) {
        if (avPacket.stream_index != ret) {
            continue;
        }
        char adtsHeader[7];
        adts_header(adtsHeader, avPacket.size);
        fwrite(adtsHeader, 1, 7, outputFile);
        fwrite(avPacket.data, 1, avPacket.size, outputFile);
        av_packet_unref(&avPacket);
    }
    avformat_close_input(&avFormatContext);
    if (outputFile) {
        fclose(outputFile);
        outputFile = NULL;
    }
    avFormatContext = NULL;
    return env->NewStringUTF(metaInfo.data());
}