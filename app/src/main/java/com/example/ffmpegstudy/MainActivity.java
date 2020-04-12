package com.example.ffmpegstudy;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.text.TextUtils;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import org.jetbrains.annotations.NotNull;

import java.io.File;
import java.io.IOException;
import java.net.URISyntaxException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;

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
    private Button mChooseFileBtn;
    private Button mExtractAACBtn;
    private Button mExtractH264Btn;
    private Button mMP4ConvertToFLVBtn;
    private Button mEncodeH264Btn;
    private ProgressBar mProgressBar;
    private TextView mFileNameTv;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        mChooseFileBtn = findViewById(R.id.choose_media_file);
        mExtractAACBtn = findViewById(R.id.extract_aac);
        mExtractH264Btn = findViewById(R.id.extract_h264);
        mFileNameTv = findViewById(R.id.media_file_name);
        mMP4ConvertToFLVBtn = findViewById(R.id.convert_mp4_flv);
        mEncodeH264Btn = findViewById(R.id.encodeh264);
        mProgressBar = findViewById(R.id.progress_bar);
        mChooseFileBtn.setOnClickListener(this);
        mExtractAACBtn.setOnClickListener(this);
        mExtractH264Btn.setOnClickListener(this);
        mMP4ConvertToFLVBtn.setOnClickListener(this);
        mEncodeH264Btn.setOnClickListener(this);
        ActivityCompat.requestPermissions(this,
                new String[]{
                        Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.READ_EXTERNAL_STORAGE
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
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                    mProgressBar.setVisibility(View.GONE);
                }
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
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                    mProgressBar.setVisibility(View.GONE);
                }
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
            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(MainActivity.this, "输出文件：" + outputFilePath, Toast.LENGTH_LONG).show();
                    mProgressBar.setVisibility(View.GONE);
                }
            });
        }
    }
}
