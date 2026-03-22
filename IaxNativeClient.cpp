#include "IaxNativeClient.h"

#include <chrono>

#if __has_include("libiax2_adapter.h")
#include "libiax2_adapter.h"
#define FROSTY_HAVE_REAL_IAX 1
#else
#define FROSTY_HAVE_REAL_IAX 0
#endif

namespace {
    static long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

IaxNativeClient::IaxNativeClient(JavaVM* vm, jobject bridgeObj)
        : vm_(vm),
          bridgeGlobalRef_(nullptr),
          onNativeStateChanged_(nullptr),
          onNativeLog_(nullptr),
          initialized_(false),
          running_(false),
          connected_(false),
          pttPressed_(false),
          port_(4569),
          iaxHandle_(nullptr),
          rxSampleRate_(8000),
          rxChannelCount_(1),
          txAudioAllowedAtMs_(0) {
    JNIEnv* env = nullptr;
    if (vm_ && vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
        bridgeGlobalRef_ = env->NewGlobalRef(bridgeObj);
        jclass cls = env->GetObjectClass(bridgeObj);
        onNativeStateChanged_ = env->GetMethodID(
                cls, "onNativeStateChanged", "(Ljava/lang/String;Ljava/lang/String;)V");
        onNativeLog_ = env->GetMethodID(
                cls, "onNativeLog", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(cls);
    }
}

IaxNativeClient::~IaxNativeClient() {
    release();
}

void IaxNativeClient::initialize() {
    if (initialized_.exchange(true)) {
        return;
    }

#if FROSTY_HAVE_REAL_IAX
    iaxHandle_ = frosty_iax_create();
    if (!iaxHandle_) {
        notifyState("error", "frosty_iax_create failed");
        return;
    }
    rxSampleRate_ = frosty_iax_get_rx_sample_rate(iaxHandle_);
    rxChannelCount_ = frosty_iax_get_rx_channel_count(iaxHandle_);
    notifyLog("Native IAX engine created");
#else
    notifyLog("Native IAX engine adapter not present yet");
#endif

    connected_ = false;
    pttPressed_ = false;
    txAudioAllowedAtMs_ = 0;
    notifyState("idle", "native bridge initialized");
}

bool IaxNativeClient::connect(
        const std::string& username,
        const std::string& password,
        const std::string& host,
        int port,
        const std::string& remoteNode) {
    std::lock_guard<std::mutex> lock(stateMutex_);

    username_ = username;
    password_ = password;
    host_ = host;
    port_ = port;
    remoteNode_ = remoteNode;

    if (!initialized_) {
        initialize();
    }

#if FROSTY_HAVE_REAL_IAX
    if (!iaxHandle_) {
        notifyState("error", "IAX engine missing");
        return false;
    }

    const bool ok = frosty_iax_connect(
            iaxHandle_,
            username_.c_str(),
            password_.c_str(),
            host_.c_str(),
            port_,
            remoteNode_.c_str());

    if (!ok) {
        notifyState("error", "connect failed");
        return false;
    }

    rxSampleRate_ = frosty_iax_get_rx_sample_rate(iaxHandle_);
    rxChannelCount_ = frosty_iax_get_rx_channel_count(iaxHandle_);

    connected_ = false;
    pttPressed_ = false;
    txAudioAllowedAtMs_ = 0;
    frosty_iax_set_ptt(iaxHandle_, 0);
    notifyState("connecting", "IAX connect requested");
    startWorker();
    return true;
#else
    notifyState("error", "No real IAX adapter compiled in");
    return false;
#endif
}

void IaxNativeClient::disconnect() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
#if FROSTY_HAVE_REAL_IAX
        if (iaxHandle_) {
            frosty_iax_set_ptt(iaxHandle_, 0);
            if (pttPressed_) {
                sendPhoneModeDtmf("#");
                notifyLog("Disconnect: sent #");
            }
            frosty_iax_disconnect(iaxHandle_);
        }
#endif
        pttPressed_ = false;
        connected_ = false;
        txAudioAllowedAtMs_ = 0;
    }

    stopWorker();
    notifyState("disconnected", "user disconnected");
}

void IaxNativeClient::setPttPressed(bool pressed) {
    const bool wasPressed = pttPressed_.exchange(pressed);

#if FROSTY_HAVE_REAL_IAX
    if (iaxHandle_) {
        frosty_iax_set_ptt(iaxHandle_, pressed ? 1 : 0);
    }

    if (iaxHandle_ && connected_) {
        if (pressed && !wasPressed) {
            txAudioAllowedAtMs_ = nowMs() + 250;
            sendPhoneModeDtmf("*99");
            notifyLog("PTT down: sent *99 and delaying TX audio for 250 ms");
        } else if (!pressed && wasPressed) {
            sendPhoneModeDtmf("#");
            txAudioAllowedAtMs_ = 0;
            notifyLog("PTT up: sent #");
        }
    }
#endif

    if (connected_) {
        notifyState(pressed ? "tx" : "connected", pressed ? "PTT down" : "Connected");
    }
}

void IaxNativeClient::release() {
    stopWorker();

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
#if FROSTY_HAVE_REAL_IAX
        if (iaxHandle_) {
            frosty_iax_set_ptt(iaxHandle_, 0);
            if (pttPressed_) {
                sendPhoneModeDtmf("#");
            }
            frosty_iax_destroy(iaxHandle_);
            iaxHandle_ = nullptr;
        }
#endif
        pttPressed_ = false;
        connected_ = false;
        initialized_ = false;
        txAudioAllowedAtMs_ = 0;
    }

    if (vm_ && bridgeGlobalRef_) {
        JNIEnv* env = nullptr;
        if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) {
            env->DeleteGlobalRef(bridgeGlobalRef_);
        }
        bridgeGlobalRef_ = nullptr;
    }
}

int IaxNativeClient::readRxAudio(uint8_t* outBuffer, int maxBytes) {
    if (!outBuffer || maxBytes <= 0) {
        return 0;
    }

#if FROSTY_HAVE_REAL_IAX
    if (!iaxHandle_) {
        return 0;
    }
    return frosty_iax_read_rx_audio(iaxHandle_, outBuffer, maxBytes);
#else
    return 0;
#endif
}

int IaxNativeClient::getRxSampleRate() const {
    return rxSampleRate_;
}

int IaxNativeClient::getRxChannelCount() const {
    return rxChannelCount_;
}

int IaxNativeClient::writeTxAudioPcm16(const uint8_t* pcmBytes, int numBytes) {
    if (!pcmBytes || numBytes <= 0) {
        return 0;
    }

#if FROSTY_HAVE_REAL_IAX
    if (!iaxHandle_) {
        return 0;
    }

    if (!connected_) {
        return 0;
    }

    if (!pttPressed_) {
        return 0;
    }

    const long long allowAt = txAudioAllowedAtMs_.load();
    if (allowAt > 0 && nowMs() < allowAt) {
        return 0;
    }

    return frosty_iax_write_tx_audio_pcm16(iaxHandle_, pcmBytes, numBytes);
#else
    return 0;
#endif
}

bool IaxNativeClient::sendPhoneModeDtmf(const char* digits) {
#if FROSTY_HAVE_REAL_IAX
    if (!iaxHandle_ || !digits || digits[0] == '\0') {
        return false;
    }
    return frosty_iax_send_dtmf(iaxHandle_, digits) != 0;
#else
    (void)digits;
    return false;
#endif
}

void IaxNativeClient::startWorker() {
    if (running_.exchange(true)) {
        return;
    }

    worker_ = std::thread(&IaxNativeClient::workerLoop, this);
}

void IaxNativeClient::stopWorker() {
    if (!running_.exchange(false)) {
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

void IaxNativeClient::workerLoop() {
    notifyLog("IAX worker loop started");

    bool lastConnected = false;

    while (running_) {
#if FROSTY_HAVE_REAL_IAX
        if (iaxHandle_) {
            frosty_iax_pump(iaxHandle_);

            const bool nowConnected = frosty_iax_is_connected(iaxHandle_) != 0;
            if (nowConnected != lastConnected) {
                connected_ = nowConnected;

                if (nowConnected) {
                    notifyState("connected", "Call connected");
                } else {
                    pttPressed_ = false;
                    txAudioAllowedAtMs_ = 0;
                    notifyState("disconnected", "Call disconnected");
                    notifyLog("Native session reported disconnected");
                }

                lastConnected = nowConnected;
            }
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    notifyLog("IAX worker loop stopped");
}

void IaxNativeClient::notifyState(const char* state, const char* detail) {
    if (!vm_ || !bridgeGlobalRef_ || !onNativeStateChanged_) {
        return;
    }

    JNIEnv* env = nullptr;
    bool attached = false;

    if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }

    jstring jState = env->NewStringUTF(state ? state : "");
    jstring jDetail = env->NewStringUTF(detail ? detail : "");
    env->CallVoidMethod(bridgeGlobalRef_, onNativeStateChanged_, jState, jDetail);
    env->DeleteLocalRef(jState);
    env->DeleteLocalRef(jDetail);

    if (attached) {
        vm_->DetachCurrentThread();
    }
}

void IaxNativeClient::notifyLog(const char* message) {
    if (!vm_ || !bridgeGlobalRef_ || !onNativeLog_) {
        return;
    }

    JNIEnv* env = nullptr;
    bool attached = false;

    if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }

    jstring jMessage = env->NewStringUTF(message ? message : "");
    env->CallVoidMethod(bridgeGlobalRef_, onNativeLog_, jMessage);
    env->DeleteLocalRef(jMessage);

    if (attached) {
        vm_->DetachCurrentThread();
    }
}