package com.example.ffmpegstudy;

public class Demo {
    static {
        System.loadLibrary("native-lib");
        System.loadLibrary("avcodec");
        System.loadLibrary("avdevice");
        System.loadLibrary("avfilter");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
    }

    public native void extractAudio(String inputPath, String outputPath);

    public native void extractVideo(String inputPath, String outputPath);

    public native void mp4CnvertToFLV(String inputPath, String outputPath);

    public native void cutVideo(String inputPath, String outputPath, int startTime, int endTime);
}
