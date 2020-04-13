package com.example.ffmpegstudy;

/**
 * 多媒体操作管理者
 */
public class MediaOperationManager {
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

    private static volatile MediaOperationManager sInstance;

    private MediaOperationManager() {

    }

    public static MediaOperationManager getInstance() {
        if (sInstance == null) {
            synchronized (MediaOperationManager.class) {
                if (sInstance == null) {
                    sInstance = new MediaOperationManager();
                }
            }
        }
        return sInstance;
    }


    public native void extractAudio(String inputPath, String outputPath);

    public native void extractVideo(String inputPath, String outputPath);

    public native void mp4CnvertToFLV(String inputPath, String outputPath);

    public native void cutVideo(String inputPath, String outputPath, int startTime, int endTime);

    public native void encodeH264(String outputPath);

    public native void extractImage(String inputString, String outputPath);
}
