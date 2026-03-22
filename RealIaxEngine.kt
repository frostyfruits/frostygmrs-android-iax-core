package com.frostygmrs.mobile

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Build
import android.util.Log
import kotlin.concurrent.thread

class RealIaxEngine : IaxEngine, NativeIaxBridge.Listener {

    companion object {
        private const val TAG = "RealIaxEngine"

        private const val TX_SAMPLE_RATE = 8000
        private const val TX_CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
        private const val TX_ENCODING = AudioFormat.ENCODING_PCM_16BIT
        private const val TX_FRAME_SAMPLES = 160
        private const val TX_FRAME_BYTES = TX_FRAME_SAMPLES * 2
    }

    @Volatile
    override var state: ConnectionState = ConnectionState()

    @Volatile
    private var engineConnected = false

    @Volatile
    private var engineConnecting = false

    @Volatile
    private var currentNodeRef: FrostyNode? = null

    @Volatile
    private var currentStatusText = "Idle"

    @Volatile
    private var audioThreadRunning = false
    private var audioThread: Thread? = null
    private var audioTrack: AudioTrack? = null

    @Volatile
    private var txThreadRunning = false
    private var txThread: Thread? = null
    private var audioRecord: AudioRecord? = null

    @Volatile
    private var pttPressed = false

    private var appContext: Context? = null

    override fun initialize() {
        Log.d(TAG, "initialize() called")

        publishState(
            isConnected = false,
            isConnecting = false,
            currentNode = null,
            statusText = "Real engine ready"
        )

        try {
            NativeIaxBridge.listener = this
            NativeIaxBridge.initialize()
            Log.d(TAG, "NativeIaxBridge.initialize() succeeded")
        } catch (e: Throwable) {
            Log.e(TAG, "NativeIaxBridge.initialize() failed", e)
            publishState(
                isConnected = false,
                isConnecting = false,
                currentNode = null,
                statusText = "Native IAX engine not available"
            )
        }
    }

    fun setContext(context: Context) {
        appContext = context.applicationContext
    }

    override fun connect(
        username: String,
        password: String,
        node: FrostyNode
    ): Result<Unit> {
        Log.d(
            TAG,
            "connect() requested usernameBlank=${username.isBlank()} passwordBlank=${password.isBlank()}"
        )

        if (username.isBlank() || password.isBlank()) {
            publishState(
                isConnected = false,
                isConnecting = false,
                currentNode = null,
                statusText = "Missing DVSwitch credentials"
            )
            return Result.failure(IllegalStateException("Missing DVSwitch credentials"))
        }

        val host = node.host.trim()
        val port = if (node.port > 0) node.port else 4569
        val remoteNode = node.remote_node.ifBlank { node.node }

        if (host.isBlank()) {
            publishState(
                isConnected = false,
                isConnecting = false,
                currentNode = null,
                statusText = "Missing host"
            )
            return Result.failure(IllegalStateException("Missing host"))
        }

        NativeIaxBridge.listener = this

        publishState(
            isConnected = false,
            isConnecting = true,
            currentNode = node,
            statusText = "Connecting to ${node.node}..."
        )

        return try {
            val ok = NativeIaxBridge.connect(
                username = username,
                password = password,
                host = host,
                port = port,
                remoteNode = remoteNode
            )

            if (ok) {
                publishState(
                    isConnected = false,
                    isConnecting = true,
                    currentNode = node,
                    statusText = "Negotiating with ${node.node}..."
                )
                startAudioPlayback()
                startTxCapture()
                Result.success(Unit)
            } else {
                publishState(
                    isConnected = false,
                    isConnecting = false,
                    currentNode = node,
                    statusText = "Connection failed"
                )
                Result.failure(IllegalStateException("Connection failed"))
            }
        } catch (e: Throwable) {
            Log.e(TAG, "NativeIaxBridge.connect() threw", e)
            publishState(
                isConnected = false,
                isConnecting = false,
                currentNode = node,
                statusText = e.message ?: "Native IAX error"
            )
            Result.failure(e)
        }
    }

    override fun disconnect() {
        Log.d(TAG, "disconnect() called")
        pttPressed = false
        stopTxCapture()
        stopAudioPlayback()

        try {
            NativeIaxBridge.disconnect()
        } catch (e: Throwable) {
            Log.e(TAG, "NativeIaxBridge.disconnect() failed", e)
        }

        publishState(
            isConnected = false,
            isConnecting = false,
            currentNode = null,
            statusText = "Disconnected"
        )
    }

    override fun setPttPressed(pressed: Boolean) {
        Log.d(TAG, "setPttPressed($pressed) called")

        val current = currentNodeRef
        pttPressed = pressed

        try {
            NativeIaxBridge.setPttPressed(pressed)
        } catch (e: Throwable) {
            Log.e(TAG, "NativeIaxBridge.setPttPressed() failed", e)
            publishState(
                isConnected = engineConnected,
                isConnecting = engineConnecting,
                currentNode = current,
                statusText = e.message ?: "PTT error"
            )
            return
        }

        if (current != null) {
            when {
                pressed -> {
                    publishState(
                        isConnected = true,
                        isConnecting = false,
                        currentNode = current,
                        statusText = "Transmitting on ${current.node}"
                    )
                }

                engineConnected -> {
                    publishState(
                        isConnected = true,
                        isConnecting = false,
                        currentNode = current,
                        statusText = "Connected to ${current.node}"
                    )
                }

                else -> {
                    publishState(
                        isConnected = false,
                        isConnecting = engineConnecting,
                        currentNode = current,
                        statusText = if (engineConnecting) {
                            "Negotiating with ${current.node}..."
                        } else {
                            "Not connected"
                        }
                    )
                }
            }
        }
    }

    override fun release() {
        Log.d(TAG, "release() called")
        pttPressed = false
        stopTxCapture()
        stopAudioPlayback()

        try {
            NativeIaxBridge.listener = null
            NativeIaxBridge.release()
        } catch (e: Throwable) {
            Log.e(TAG, "NativeIaxBridge.release() failed", e)
        }

        publishState(
            isConnected = false,
            isConnecting = false,
            currentNode = null,
            statusText = "Engine released"
        )
    }

    override fun onStateChanged(state: String, detail: String) {
        Log.d(TAG, "onStateChanged state=$state detail=$detail")

        val current = currentNodeRef

        when (state.lowercase()) {
            "idle" -> {
                publishState(
                    isConnected = false,
                    isConnecting = false,
                    currentNode = current,
                    statusText = if (detail.isBlank()) "Idle" else detail
                )
            }

            "connecting" -> {
                publishState(
                    isConnected = false,
                    isConnecting = true,
                    currentNode = current,
                    statusText = if (detail.isBlank()) {
                        if (current != null) "Negotiating with ${current.node}..." else "Connecting..."
                    } else {
                        detail
                    }
                )
            }

            "connected", "rx" -> {
                publishState(
                    isConnected = true,
                    isConnecting = false,
                    currentNode = current,
                    statusText = if (current != null) "Connected to ${current.node}" else "Connected"
                )
            }

            "tx" -> {
                publishState(
                    isConnected = true,
                    isConnecting = false,
                    currentNode = current,
                    statusText = if (current != null) "Transmitting on ${current.node}" else "Transmitting"
                )
            }

            "disconnected" -> {
                pttPressed = false
                publishState(
                    isConnected = false,
                    isConnecting = false,
                    currentNode = null,
                    statusText = if (detail.isBlank()) "Disconnected" else detail
                )
            }

            "error" -> {
                publishState(
                    isConnected = false,
                    isConnecting = false,
                    currentNode = current,
                    statusText = if (detail.isBlank()) "Native IAX error" else detail
                )
            }

            else -> {
                publishState(
                    isConnected = engineConnected,
                    isConnecting = engineConnecting,
                    currentNode = current,
                    statusText = if (detail.isBlank()) state else detail
                )
            }
        }
    }

    override fun onLog(message: String) {
        Log.d(TAG, "nativeLog=$message")
    }

    private fun publishState(
        isConnected: Boolean,
        isConnecting: Boolean,
        currentNode: FrostyNode?,
        statusText: String
    ) {
        engineConnected = isConnected
        engineConnecting = isConnecting
        currentNodeRef = currentNode
        currentStatusText = statusText

        state = ConnectionState(
            isConnected = isConnected,
            isConnecting = isConnecting,
            currentNode = currentNode,
            statusText = statusText
        )
    }

    private fun configureLoudPlayback() {
        val context = appContext ?: return
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return

        try {
            audioManager.mode = AudioManager.MODE_NORMAL
        } catch (e: Throwable) {
            Log.w(TAG, "Could not set audio mode", e)
        }

        try {
            audioManager.isSpeakerphoneOn = true
        } catch (e: Throwable) {
            Log.w(TAG, "Could not enable speakerphone", e)
        }

        try {
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            val target = if (max > 1) max - 1 else max
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, target, 0)
            Log.d(TAG, "Playback volume set near max: $target/$max")
        } catch (e: Throwable) {
            Log.w(TAG, "Could not raise media volume", e)
        }
    }

    private fun startAudioPlayback() {
        if (audioThreadRunning) {
            return
        }

        configureLoudPlayback()

        val sampleRate = try {
            NativeIaxBridge.getRxSampleRate()
        } catch (_: Throwable) {
            8000
        }

        val channels = try {
            NativeIaxBridge.getRxChannelCount()
        } catch (_: Throwable) {
            1
        }

        val channelConfig = if (channels > 1) {
            AudioFormat.CHANNEL_OUT_STEREO
        } else {
            AudioFormat.CHANNEL_OUT_MONO
        }

        val minBuffer = AudioTrack.getMinBufferSize(
            sampleRate,
            channelConfig,
            AudioFormat.ENCODING_PCM_16BIT
        )

        if (minBuffer <= 0) {
            return
        }

        val track = AudioTrack(
            AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                .build(),
            AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(sampleRate)
                .setChannelMask(channelConfig)
                .build(),
            minBuffer * 4,
            AudioTrack.MODE_STREAM,
            AudioManager.AUDIO_SESSION_ID_GENERATE
        )

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                track.setVolume(AudioTrack.getMaxVolume())
            }
        } catch (e: Throwable) {
            Log.w(TAG, "Could not set AudioTrack volume", e)
        }

        audioTrack = track
        audioThreadRunning = true

        audioThread = thread(start = true, name = "FrostyRxAudio") {
            try {
                track.play()
                val buffer = ByteArray(640)

                while (audioThreadRunning) {
                    val read = try {
                        NativeIaxBridge.readRxAudio(buffer, buffer.size)
                    } catch (_: Throwable) {
                        0
                    }

                    if (read > 0) {
                        track.write(buffer, 0, read)
                    } else {
                        Thread.sleep(10)
                    }
                }
            } finally {
                try {
                    track.stop()
                } catch (_: Throwable) {
                }
                try {
                    track.flush()
                } catch (_: Throwable) {
                }
                try {
                    track.release()
                } catch (_: Throwable) {
                }
            }
        }
    }

    private fun stopAudioPlayback() {
        audioThreadRunning = false
        try {
            audioThread?.join(500)
        } catch (_: Throwable) {
        }
        audioThread = null
        audioTrack = null
    }

    @SuppressLint("MissingPermission")
    private fun startTxCapture() {
        if (txThreadRunning) {
            return
        }

        val minBuffer = AudioRecord.getMinBufferSize(
            TX_SAMPLE_RATE,
            TX_CHANNEL_CONFIG,
            TX_ENCODING
        )

        if (minBuffer <= 0) {
            Log.e(TAG, "startTxCapture: invalid AudioRecord minBuffer=$minBuffer")
            return
        }

        val bufferSize = maxOf(minBuffer, TX_FRAME_BYTES * 8)

        val record = createTxAudioRecord(bufferSize)
        if (record == null) {
            Log.e(TAG, "startTxCapture: all AudioRecord sources failed")
            return
        }

        audioRecord = record
        txThreadRunning = true

        txThread = thread(start = true, name = "FrostyTxAudio") {
            val frame = ByteArray(TX_FRAME_BYTES)

            try {
                record.startRecording()
                Log.d(TAG, "TX capture started")

                while (txThreadRunning) {
                    if (!pttPressed || !engineConnected) {
                        Thread.sleep(10)
                        continue
                    }

                    var offset = 0
                    while (offset < frame.size && txThreadRunning && pttPressed && engineConnected) {
                        val read = try {
                            record.read(frame, offset, frame.size - offset)
                        } catch (e: Throwable) {
                            Log.e(TAG, "AudioRecord.read failed", e)
                            0
                        }

                        if (read > 0) {
                            offset += read
                        } else {
                            Thread.sleep(5)
                        }
                    }

                    if (offset == frame.size && pttPressed && engineConnected) {
                        try {
                            val written = NativeIaxBridge.writeTxAudioPcm16(frame, frame.size)
                            if (written <= 0) {
                                Thread.sleep(10)
                            }
                        } catch (e: Throwable) {
                            Log.e(TAG, "writeTxAudioPcm16 failed", e)
                            Thread.sleep(20)
                        }
                    }
                }
            } finally {
                Log.d(TAG, "TX capture stopping")
                try {
                    record.stop()
                } catch (_: Throwable) {
                }
                try {
                    record.release()
                } catch (_: Throwable) {
                }
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun createTxAudioRecord(bufferSize: Int): AudioRecord? {
        val sources = intArrayOf(
            MediaRecorder.AudioSource.VOICE_COMMUNICATION,
            MediaRecorder.AudioSource.MIC,
            MediaRecorder.AudioSource.CAMCORDER,
            MediaRecorder.AudioSource.DEFAULT
        )

        for (source in sources) {
            try {
                val record = AudioRecord(
                    source,
                    TX_SAMPLE_RATE,
                    TX_CHANNEL_CONFIG,
                    TX_ENCODING,
                    bufferSize
                )

                if (record.state == AudioRecord.STATE_INITIALIZED) {
                    Log.d(TAG, "TX AudioRecord initialized with source=$source bufferSize=$bufferSize")
                    return record
                }

                Log.w(TAG, "TX AudioRecord source=$source failed to initialize state=${record.state}")
                try {
                    record.release()
                } catch (_: Throwable) {
                }
            } catch (e: Throwable) {
                Log.e(TAG, "TX AudioRecord source=$source threw during init", e)
            }
        }

        return null
    }

    private fun stopTxCapture() {
        txThreadRunning = false
        try {
            txThread?.join(500)
        } catch (_: Throwable) {
        }
        txThread = null
        audioRecord = null
    }
}