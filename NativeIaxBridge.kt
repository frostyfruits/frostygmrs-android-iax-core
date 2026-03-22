package com.frostygmrs.mobile

import android.util.Log

object NativeIaxBridge {

    private const val TAG = "NativeIaxBridge"

    @Volatile
    var listener: Listener? = null

    interface Listener {
        fun onStateChanged(state: String, detail: String)
        fun onLog(message: String)
    }

    init {
        Log.d(TAG, "Loading native library...")
        System.loadLibrary("realiax")
        Log.d(TAG, "Native library loaded")
    }

    external fun initialize()

    external fun connect(
        username: String,
        password: String,
        host: String,
        port: Int,
        remoteNode: String
    ): Boolean

    external fun disconnect()

    external fun setPttPressed(pressed: Boolean)

    external fun readRxAudio(buffer: ByteArray, maxBytes: Int): Int

    external fun getRxSampleRate(): Int

    external fun getRxChannelCount(): Int

    external fun writeTxAudioPcm16(buffer: ByteArray, length: Int): Int

    external fun release()

    fun onNativeStateChanged(state: String, detail: String) {
        Log.d(TAG, "state=$state detail=$detail")
        listener?.onStateChanged(state, detail)
    }

    fun onNativeLog(message: String) {
        Log.d(TAG, message)
        listener?.onLog(message)
    }
}