package utils

import android.util.Log

object Utils {
    private val TAG = "LFPlayer"
    @JvmOverloads
    fun log(tag: String = TAG, message: String) {
        Log.e(TAG, message);
    }
}