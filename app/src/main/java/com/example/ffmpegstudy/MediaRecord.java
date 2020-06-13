package com.example.ffmpegstudy;

import android.media.MediaRecorder;

import java.io.File;
import java.io.IOException;

import utils.FileUtils;
import utils.Utils;

public class MediaRecord {

    private MediaRecorder mMediaRecorder;
    private final String encoderPath = FileUtils.INSTANCE.getROOT_DIR() + File.separator +
            System.currentTimeMillis() + ".aac";

    public void startRecording() {
        mMediaRecorder = new MediaRecorder();
        mMediaRecorder.setAudioSource(MediaRecorder.AudioSource.MIC);
        mMediaRecorder.setAudioChannels(2);
        mMediaRecorder.setAudioSamplingRate(44100);
        mMediaRecorder.setAudioEncodingBitRate(128000);
        mMediaRecorder.setOutputFormat(MediaRecorder.OutputFormat.AAC_ADTS);
        mMediaRecorder.setAudioEncoder(MediaRecorder.AudioEncoder.AAC);
        mMediaRecorder.setOutputFile(encoderPath);
        mMediaRecorder.setOnErrorListener((mr, what, extra) -> {
            Utils.INSTANCE.log("On Error");
        });

        mMediaRecorder.setOnInfoListener((mr, what, extra) -> {
            Utils.INSTANCE.log("On Info");
        });
        try {
            mMediaRecorder.prepare();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
        mMediaRecorder.start();
    }

    public void release() {
        if (mMediaRecorder != null) {
            mMediaRecorder.release();
            mMediaRecorder = null;
        }
    }
}
