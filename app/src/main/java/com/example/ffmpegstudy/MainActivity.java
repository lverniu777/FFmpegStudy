package com.example.ffmpegstudy;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import org.jetbrains.annotations.NotNull;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

import butterknife.BindView;
import butterknife.ButterKnife;
import butterknife.OnClick;
import utils.FileUtils;

public class MainActivity extends AppCompatActivity implements View.OnClickListener {
    private final Executor mExecutor = Executors.newSingleThreadExecutor(new ThreadFactory() {
        @Override
        public Thread newThread(@NotNull Runnable r) {
            return new Thread(r, "FFmpeg Thread");
        }
    });
    private final MediaOperationManager mediaOperationManager = MediaOperationManager.getInstance();
    private static final int PERMISSION_REQUEST_CODE = 10086;
    private static final int CHOOSE_FILE_REQUEST_CODE = 6;
    @BindView(R.id.progress_bar)
    ProgressBar mProgressBar;
    @BindView(R.id.media_file_name)
    TextView mFileNameTv;

    private MediaRecord mMediaRecord;


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ButterKnife.bind(this);
        ActivityCompat.requestPermissions(this,
                new String[]{
                        Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.READ_EXTERNAL_STORAGE,
                        Manifest.permission.RECORD_AUDIO
                },
                PERMISSION_REQUEST_CODE
        );
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (requestCode != PERMISSION_REQUEST_CODE) {
            finish();
            return;
        }
        for (int grantResult : grantResults) {
            if (grantResult != PackageManager.PERMISSION_GRANTED) {
                finish();
            }
        }
    }

    @OnClick({R.id.choose_media_file, R.id.extract_aac, R.id.extract_h264, R.id.media_file_name,
            R.id.convert_mp4_flv, R.id.encodeh264, R.id.progress_bar, R.id.extractImage, R.id.record_audio_with_mediarecorder})
    @Override
    public void onClick(View v) {
        switch (v.getId()) {
            case R.id.choose_media_file:
                Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
                intent.setType("*/*");//无类型限制
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                startActivityForResult(intent, CHOOSE_FILE_REQUEST_CODE);
                break;
            case R.id.extract_aac:
                performAction(new ExtractAAC());
                break;
            case R.id.extract_h264:
                performAction(new ExtractH264());
                break;
            case R.id.convert_mp4_flv:
                performAction(new ConvertMP4ToFLV());
                break;
            case R.id.encodeh264:
                performAction(new EncodeH264());
                break;
            case R.id.extractImage:
                performAction(new ExtractImage());
                break;
            case R.id.record_audio_with_mediarecorder:
                new RecordAudio().run();
                break;
        }
    }

    private void performAction(Runnable runnable) {
        mProgressBar.setVisibility(View.VISIBLE);
        mExecutor.execute(runnable);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != CHOOSE_FILE_REQUEST_CODE) {
            return;
        }
        if (resultCode != Activity.RESULT_OK) {
            return;
        }
        if (data == null || data.getData() == null) {
            Toast.makeText(this, "没有选中文件", Toast.LENGTH_LONG).show();
        } else {
            final String filePath = FileUtils.INSTANCE.getPath(this, data.getData());
            if (TextUtils.isEmpty(filePath)) {
                mFileNameTv.setVisibility(View.GONE);
            } else {
                mFileNameTv.setVisibility(View.VISIBLE);
                mFileNameTv.setText(filePath);
            }
        }
    }


    private class ExtractAAC implements Runnable {

        @Override
        public void run() {
            final String inputFilePath = mFileNameTv.getText().toString();
            final String outputFilePath = FileUtils.INSTANCE.getROOT_DIR() + File.separator
                    + System.currentTimeMillis() + ".aac";
            mediaOperationManager.extractAudio(inputFilePath, outputFilePath);
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                mProgressBar.setVisibility(View.GONE);
            });
        }
    }

    private class ExtractH264 implements Runnable {

        @Override
        public void run() {
            final String inputFilePath = mFileNameTv.getText().toString();
            final String outputFilePath = FileUtils.INSTANCE.getROOT_DIR() + File.separator
                    + System.currentTimeMillis() + ".h264";
            mediaOperationManager.extractAudio(inputFilePath, outputFilePath);
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                mProgressBar.setVisibility(View.GONE);
            });
        }
    }

    private class ConvertMP4ToFLV implements Runnable {

        @Override
        public void run() {
            final String inputFilePath = mFileNameTv.getText().toString();
            final String outputFilePath = FileUtils.INSTANCE.getROOT_DIR() + File.separator
                    + System.currentTimeMillis() + ".flv";
            try {
                final File outputFile = new File(outputFilePath);
                if (!outputFile.exists() && !outputFile.createNewFile()) {
                    Toast.makeText(MainActivity.this, "文件创建失败：" + outputFilePath, Toast.LENGTH_LONG).show();
                    return;
                }
            } catch (IOException e) {
                throw new RuntimeException(e);
            }
            mediaOperationManager.mp4CnvertToFLV(inputFilePath, outputFilePath);
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                    mProgressBar.setVisibility(View.GONE);
                }
            });
        }
    }

    private class EncodeH264 implements Runnable {

        @Override
        public void run() {
            final String outputFilePath = FileUtils.INSTANCE.getROOT_DIR() + File.separator
                    + System.currentTimeMillis() + ".h264";
            mediaOperationManager.encodeH264(outputFilePath);
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                mProgressBar.setVisibility(View.GONE);
            });
        }
    }

    private class ExtractImage implements Runnable {
        @Override
        public void run() {
            final String inputFilePath = mFileNameTv.getText().toString();
            final String outputFilePath = FileUtils.INSTANCE.getROOT_DIR() + File.separator
                    + System.currentTimeMillis()+File.separator;
            final File outputDir = new File(outputFilePath);
            if (!outputDir.exists() && !outputDir.mkdirs()) {
                Toast.makeText(MainActivity.this, "目录创建失败：" + outputFilePath, Toast.LENGTH_LONG).show();
                return;
            }
            mediaOperationManager.extractImage(inputFilePath, outputFilePath);
            runOnUiThread(() -> {
                Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                mProgressBar.setVisibility(View.GONE);
            });
        }
    }

    public class RecordAudio implements Runnable {
        @Override
        public void run() {

            if (mMediaRecord == null) {
                mMediaRecord = new MediaRecord();
                mMediaRecord.startRecording();
            } else {
                mMediaRecord.release();
                mMediaRecord = null;
            }
        }
    }
}
