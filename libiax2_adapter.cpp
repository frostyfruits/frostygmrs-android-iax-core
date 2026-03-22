#include "libiax2_adapter.h"

#include <android/log.h>
#include <stdlib.h>

#include "iax_frame.h"
#include "iax_session.h"

#define FROSTY_LOG_TAG "FrostyIaxNative"
#define FROSTY_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, FROSTY_LOG_TAG, __VA_ARGS__)
#define FROSTY_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FROSTY_LOG_TAG, __VA_ARGS__)

struct FrostyIaxHandle {
    frosty::IaxSession* session;
};

void* frosty_iax_create() {
    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(calloc(1, sizeof(FrostyIaxHandle)));
    if (!h) {
        FROSTY_LOGE("frosty_iax_create: calloc failed");
        return nullptr;
    }

    h->session = new frosty::IaxSession();
    if (!h->session->initialize()) {
        delete h->session;
        free(h);
        FROSTY_LOGE("frosty_iax_create: session initialize failed");
        return nullptr;
    }

    FROSTY_LOGD("frosty_iax_create: handle created");
    return h;
}

int frosty_iax_connect(
        void* handle,
        const char* username,
        const char* password,
        const char* host,
        int port,
        const char* remoteNode) {
    if (!handle) {
        FROSTY_LOGE("frosty_iax_connect: null handle");
        return 0;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        FROSTY_LOGE("frosty_iax_connect: null session");
        return 0;
    }

    frosty::ConnectParams params;
    params.username = username;
    params.password = password;
    params.host = host;
    params.port = port;
    params.remoteNode = remoteNode;

    FROSTY_LOGD("frosty_iax_connect: requested");

    const bool ok = h->session->connect(params);
    FROSTY_LOGD("frosty_iax_connect: session connect returned %d", ok ? 1 : 0);
    return ok ? 1 : 0;
}

void frosty_iax_disconnect(void* handle) {
    if (!handle) {
        FROSTY_LOGE("frosty_iax_disconnect: null handle");
        return;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (h->session) {
        h->session->disconnect();
    }

    FROSTY_LOGD("frosty_iax_disconnect: disconnected");
}

void frosty_iax_set_ptt(void* handle, int pressed) {
    if (!handle) {
        FROSTY_LOGE("frosty_iax_set_ptt: null handle");
        return;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (h->session) {
        h->session->setPtt(pressed ? true : false);
    }

    FROSTY_LOGD("frosty_iax_set_ptt: ptt=%d", pressed ? 1 : 0);
}

void frosty_iax_pump(void* handle) {
    if (!handle) {
        return;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (h->session) {
        h->session->pump();
    }
}

int frosty_iax_read_rx_audio(void* handle, unsigned char* outBuffer, int maxBytes) {
    if (!handle) {
        return 0;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 0;
    }

    return h->session->readRxAudio(reinterpret_cast<uint8_t*>(outBuffer), maxBytes);
}

int frosty_iax_get_rx_sample_rate(void* handle) {
    if (!handle) {
        return 8000;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 8000;
    }

    return h->session->getRxSampleRate();
}

int frosty_iax_get_rx_channel_count(void* handle) {
    if (!handle) {
        return 1;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 1;
    }

    return h->session->getRxChannelCount();
}

int frosty_iax_write_tx_audio_pcm16(void* handle, const unsigned char* pcmBytes, int numBytes) {
    if (!handle || !pcmBytes || numBytes <= 0) {
        return 0;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 0;
    }

    return h->session->writeTxAudioPcm16(reinterpret_cast<const uint8_t*>(pcmBytes), numBytes);
}

int frosty_iax_send_dtmf(void* handle, const char* digits) {
    if (!handle || !digits || digits[0] == '\0') {
        return 0;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 0;
    }

    return h->session->sendDtmfDigits(digits) ? 1 : 0;
}

int frosty_iax_is_connected(void* handle) {
    if (!handle) {
        return 0;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);
    if (!h->session) {
        return 0;
    }

    return h->session->isConnected() ? 1 : 0;
}

void frosty_iax_destroy(void* handle) {
    if (!handle) {
        return;
    }

    FrostyIaxHandle* h = static_cast<FrostyIaxHandle*>(handle);

    if (h->session) {
        h->session->disconnect();
        delete h->session;
        h->session = nullptr;
    }

    FROSTY_LOGD("frosty_iax_destroy: freeing handle");
    free(h);
}