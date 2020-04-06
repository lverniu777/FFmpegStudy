package com.example.ffmpegstudy;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.security.Permission;
import java.util.Random;

public class MainActivity extends AppCompatActivity {

    private final Demo demo = new Demo();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            ActivityCompat.requestPermissions(this,
                    new String[]{
                            Manifest.permission.WRITE_EXTERNAL_STORAGE,
                            Manifest.permission.READ_EXTERNAL_STORAGE
                    },
                    10086
            );
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        final String inputFilePath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "ffmpeg_test.mp4";
        final String outputFilePath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "ffmpeg_test.flv";
        final String outputAudioFilePath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "output.aac";
        final String outputVideoFilePath = Environment.getExternalStorageDirectory().getAbsolutePath() + File.separator + "output.h264";
        final File inputFile = new File(inputFilePath);
        if (inputFile.exists()) {
            Toast.makeText(this, "File exit: " + inputFilePath, Toast.LENGTH_LONG).show();
        } else {
            return;
        }
        TextView tv = findViewById(R.id.sample_text);
        demo.extractAudio(inputFilePath, outputAudioFilePath);
        demo.extractVideo(inputFilePath, outputVideoFilePath);
        demo.mp4CnvertToFLV(inputFilePath, outputFilePath);
    }
}
