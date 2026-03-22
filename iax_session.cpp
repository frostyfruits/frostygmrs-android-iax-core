#include "iax_session.h"

#include <algorithm>
#include <android/log.h>
#include <chrono>
#include <random>
#include <string.h>
#include <vector>

#include "iax_auth.h"
#include "iax_builder.h"

#define FROSTY_SESSION_LOG_TAG "FrostyIaxSession"
#define FROSTY_SESSION_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, FROSTY_SESSION_LOG_TAG, __VA_ARGS__)
#define FROSTY_SESSION_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FROSTY_SESSION_LOG_TAG, __VA_ARGS__)

namespace frosty {

    static const uint8_t kFrameTypeDtmf = 1;
    static const uint8_t kFrameTypeVoice = 2;
    static const uint8_t kFrameTypeIax = 6;
    static const uint8_t kMediaFormatUlawSubclass = 0x04;
    static const uint32_t kDtmfStepMs = 240;
    static const int64_t kLinkTimeoutMs = 10000;
    static const int64_t kKeepAliveIntervalMs = 1000;

    static uint16_t readU16(const uint8_t* p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) |
                                     static_cast<uint16_t>(p[1]));
    }

    static uint32_t readU32(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) |
               static_cast<uint32_t>(p[3]);
    }

    static int16_t ulawToPcm16(uint8_t uVal) {
        uVal = static_cast<uint8_t>(~uVal);

        const int sign = uVal & 0x80;
        const int exponent = (uVal >> 4) & 0x07;
        const int mantissa = uVal & 0x0f;

        int sample = ((mantissa << 3) + 0x84) << exponent;
        sample -= 0x84;

        return static_cast<int16_t>(sign ? -sample : sample);
    }

    static uint8_t pcm16ToUlaw(int16_t sample) {
        static const int16_t kBias = 0x84;
        static const int16_t kClip = 32635;

        int16_t pcm = sample;
        int16_t mask;
        int16_t seg;
        uint8_t uval;

        if (pcm < 0) {
            pcm = static_cast<int16_t>(-pcm);
            mask = 0x7f;
        } else {
            mask = 0xff;
        }

        if (pcm > kClip) {
            pcm = kClip;
        }

        pcm = static_cast<int16_t>(pcm + kBias);

        if (pcm <= 0xFF) {
            seg = 0;
        } else if (pcm <= 0x1FF) {
            seg = 1;
        } else if (pcm <= 0x3FF) {
            seg = 2;
        } else if (pcm <= 0x7FF) {
            seg = 3;
        } else if (pcm <= 0xFFF) {
            seg = 4;
        } else if (pcm <= 0x1FFF) {
            seg = 5;
        } else if (pcm <= 0x3FFF) {
            seg = 6;
        } else {
            seg = 7;
        }

        uval = static_cast<uint8_t>((seg << 4) | ((pcm >> (seg + 3)) & 0x0f));
        return static_cast<uint8_t>(uval ^ mask);
    }

    static bool dtmfCharToSubclass(char c, uint8_t* outSubclass) {
        if (!outSubclass) {
            return false;
        }

        *outSubclass = static_cast<uint8_t>(c);
        return true;
    }

    int64_t IaxSession::nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint16_t IaxSession::generateSrcCallNo() {
        static std::mutex s_mutex;
        static std::mt19937 s_rng(
                static_cast<uint32_t>(
                        std::chrono::steady_clock::now().time_since_epoch().count()));
        static uint16_t s_last = 0;

        std::lock_guard<std::mutex> lock(s_mutex);

        std::uniform_int_distribution<uint16_t> dist(2, 32767);

        uint16_t value = dist(s_rng);
        if (value == s_last) {
            value = static_cast<uint16_t>((value % 32766) + 2);
        }

        s_last = value;
        return value;
    }

    IaxSession::IaxSession()
            : state_(SessionState::Idle),
              pttPressed_(false),
              port_(kIaxDefaultPort),
              srcCallNo_(generateSrcCallNo()),
              peerCallNo_(0),
              outboundTimestamp_(1),
              txMiniTimestamp_(1),
              tokenReceived_(false),
              tokenSentBack_(false),
              authRepSent_(false),
              txCodecAnnounced_(false),
              rxSampleRate_(8000),
              rxChannelCount_(1),
              localOSeq_(0),
              remoteISeq_(0),
              lastRxVoiceTimestamp_(0),
              haveRxVoiceTimestamp_(false),
              lastInboundPacketAtMs_(0),
              lastPingSentAtMs_(0) {
    }

    IaxSession::~IaxSession() {
        disconnect();
    }

    bool IaxSession::initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        setStateLocked(SessionState::Idle);
        rxAudioBuffer_.clear();
        localOSeq_ = 0;
        remoteISeq_ = 0;
        peerCallNo_ = 0;
        outboundTimestamp_ = 1;
        txMiniTimestamp_ = 1;
        txCodecAnnounced_ = false;
        lastRxVoiceTimestamp_ = 0;
        haveRxVoiceTimestamp_ = false;
        lastInboundPacketAtMs_ = 0;
        lastPingSentAtMs_ = nowMs() - kKeepAliveIntervalMs;
        FROSTY_SESSION_LOGD("initialize(): srcCallNo=%u", static_cast<unsigned int>(srcCallNo_));
        return true;
    }

    bool IaxSession::connect(const ConnectParams& params) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!params.username || !params.password || !params.host || !params.remoteNode) {
            FROSTY_SESSION_LOGE("connect(): missing params");
            setStateLocked(SessionState::Error);
            return false;
        }

        username_ = params.username;
        password_ = params.password;
        host_ = params.host;
        remoteNode_ = params.remoteNode;
        port_ = params.port > 0 ? params.port : kIaxDefaultPort;
        pttPressed_ = false;
        srcCallNo_ = generateSrcCallNo();
        peerCallNo_ = 0;
        outboundTimestamp_ = 1;
        txMiniTimestamp_ = 1;
        callToken_.clear();
        tokenReceived_ = false;
        tokenSentBack_ = false;
        authRepSent_ = false;
        txCodecAnnounced_ = false;
        rxAudioBuffer_.clear();
        rxSampleRate_ = 8000;
        rxChannelCount_ = 1;
        localOSeq_ = 0;
        remoteISeq_ = 0;
        lastRxVoiceTimestamp_ = 0;
        haveRxVoiceTimestamp_ = false;
        lastInboundPacketAtMs_ = 0;
        lastPingSentAtMs_ = 0;

        FROSTY_SESSION_LOGD(
                "connect(): request accepted srcCallNo=%u",
                static_cast<unsigned int>(srcCallNo_)
        );

        setStateLocked(SessionState::Connecting);

        if (!openTransportLocked()) {
            FROSTY_SESSION_LOGE("connect(): openTransportLocked failed");
            setStateLocked(SessionState::Error);
            return false;
        }

        if (!sendInitialNewLocked()) {
            FROSTY_SESSION_LOGE("connect(): sendInitialNewLocked failed");
            setStateLocked(SessionState::Error);
            return false;
        }

        FROSTY_SESSION_LOGD("connect(): NEW sent with empty CALLTOKEN IE, awaiting server response");
        setStateLocked(SessionState::Connecting);
        return true;
    }

    void IaxSession::disconnect() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ == SessionState::Disconnected || state_ == SessionState::Idle) {
            closeTransportLocked();
            callToken_.clear();
            tokenReceived_ = false;
            tokenSentBack_ = false;
            authRepSent_ = false;
            txCodecAnnounced_ = false;
            rxAudioBuffer_.clear();
            localOSeq_ = 0;
            remoteISeq_ = 0;
            peerCallNo_ = 0;
            outboundTimestamp_ = 1;
            txMiniTimestamp_ = 1;
            lastRxVoiceTimestamp_ = 0;
            haveRxVoiceTimestamp_ = false;
            lastInboundPacketAtMs_ = 0;
            lastPingSentAtMs_ = 0;
            setStateLocked(SessionState::Disconnected);
            return;
        }

        setStateLocked(SessionState::Disconnecting);
        closeTransportLocked();
        pttPressed_ = false;
        callToken_.clear();
        tokenReceived_ = false;
        tokenSentBack_ = false;
        authRepSent_ = false;
        txCodecAnnounced_ = false;
        rxAudioBuffer_.clear();
        localOSeq_ = 0;
        remoteISeq_ = 0;
        peerCallNo_ = 0;
        outboundTimestamp_ = 1;
        txMiniTimestamp_ = 1;
        lastRxVoiceTimestamp_ = 0;
        haveRxVoiceTimestamp_ = false;
        lastInboundPacketAtMs_ = 0;
        lastPingSentAtMs_ = 0;
        setStateLocked(SessionState::Disconnected);
        FROSTY_SESSION_LOGD("disconnect()");
    }

    void IaxSession::setPtt(bool pressed) {
        std::lock_guard<std::mutex> lock(mutex_);
        pttPressed_ = pressed;

        if (pressed) {
            txCodecAnnounced_ = false;

            if (haveRxVoiceTimestamp_) {
                const uint32_t seededTs = lastRxVoiceTimestamp_ + 20;
                if (seededTs > outboundTimestamp_) {
                    outboundTimestamp_ = seededTs;
                }
                txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);
                FROSTY_SESSION_LOGD(
                        "setPtt(1): seeded from full-frame rx voice ts=%u outbound=%u mini=%u",
                        static_cast<unsigned int>(lastRxVoiceTimestamp_),
                        static_cast<unsigned int>(outboundTimestamp_),
                        static_cast<unsigned int>(txMiniTimestamp_));
            } else {
                txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);
                FROSTY_SESSION_LOGD(
                        "setPtt(1): no full-frame rx voice ts yet, using outbound=%u mini=%u",
                        static_cast<unsigned int>(outboundTimestamp_),
                        static_cast<unsigned int>(txMiniTimestamp_));
            }
        } else {
            txCodecAnnounced_ = false;
        }

        FROSTY_SESSION_LOGD("setPtt(%d)", pressed ? 1 : 0);
    }

    void IaxSession::markInboundActivityLocked() {
        lastInboundPacketAtMs_ = nowMs();
    }

    bool IaxSession::checkLinkTimeoutLocked() {
        if (state_ != SessionState::Connected) {
            return false;
        }

        if (lastInboundPacketAtMs_ <= 0) {
            return false;
        }

        const int64_t idleMs = nowMs() - lastInboundPacketAtMs_;
        if (idleMs < kLinkTimeoutMs) {
            return false;
        }

        FROSTY_SESSION_LOGE("pump(): link timeout after %lld ms without inbound packets", static_cast<long long>(idleMs));

        pttPressed_ = false;
        txCodecAnnounced_ = false;
        peerCallNo_ = 0;
        closeTransportLocked();
        setStateLocked(SessionState::Disconnected);
        return true;
    }

    void IaxSession::maybeSendKeepAliveLocked() {
        if (state_ != SessionState::Connected) {
            return;
        }

        if (!transport_.isOpen()) {
            return;
        }

        if (peerCallNo_ == 0) {
            return;
        }

        const int64_t now = nowMs();
        if (lastPingSentAtMs_ > 0 && (now - lastPingSentAtMs_) < kKeepAliveIntervalMs) {
            return;
        }

        if (sendPingLocked()) {
            lastPingSentAtMs_ = now;
        }
    }

    void IaxSession::pump() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!transport_.isOpen()) {
            return;
        }

        uint8_t buffer[kIaxMaxPacketSize];
        const int rc = transport_.receivePacket(buffer, sizeof(buffer), 10);
        if (rc > 0) {
            markInboundActivityLocked();
            logPacketSummaryLocked(buffer, rc);
            return;
        }

        if (rc == 0) {
            maybeSendKeepAliveLocked();
            (void)checkLinkTimeoutLocked();
        }
    }

    SessionState IaxSession::state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    bool IaxSession::isConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_ == SessionState::Connected;
    }

    int IaxSession::readRxAudio(uint8_t* outBuffer, int maxBytes) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!outBuffer || maxBytes <= 0) {
            return 0;
        }

        const int available = static_cast<int>(rxAudioBuffer_.size());
        const int toCopy = std::min(available, maxBytes);
        if (toCopy <= 0) {
            return 0;
        }

        memcpy(outBuffer, rxAudioBuffer_.data(), static_cast<size_t>(toCopy));
        rxAudioBuffer_.erase(rxAudioBuffer_.begin(), rxAudioBuffer_.begin() + toCopy);
        return toCopy;
    }

    int IaxSession::getRxSampleRate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return rxSampleRate_;
    }

    int IaxSession::getRxChannelCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return rxChannelCount_;
    }

    int IaxSession::writeTxAudioPcm16(const uint8_t* pcmBytes, int numBytes) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!pcmBytes || numBytes <= 0) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): rejected empty buffer");
            return 0;
        }

        if (!transport_.isOpen()) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): transport not open");
            return 0;
        }

        if (state_ != SessionState::Connected) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): session not connected state=%d", static_cast<int>(state_));
            return 0;
        }

        if (!pttPressed_) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): ptt not pressed");
            return 0;
        }

        if (peerCallNo_ == 0) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): peerCallNo is 0");
            return 0;
        }

        const int usableBytes = numBytes - (numBytes % 2);
        if (usableBytes <= 0) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): usableBytes <= 0");
            return 0;
        }

        const int sampleCount = usableBytes / 2;
        std::vector<uint8_t> ulawBytes;
        ulawBytes.resize(static_cast<size_t>(sampleCount));

        for (int i = 0; i < sampleCount; ++i) {
            const int lo = pcmBytes[i * 2] & 0xff;
            const int hi = pcmBytes[i * 2 + 1] & 0xff;
            const int16_t sample = static_cast<int16_t>((hi << 8) | lo);
            ulawBytes[static_cast<size_t>(i)] = pcm16ToUlaw(sample);
        }

        if (!txCodecAnnounced_) {
            if (!sendVoiceFullFrameLocked(ulawBytes.data(), static_cast<int>(ulawBytes.size()))) {
                FROSTY_SESSION_LOGD("writeTxAudioPcm16(): first full voice frame failed");
                return 0;
            }
            txCodecAnnounced_ = true;
            return usableBytes;
        }

        if (!sendVoiceMiniFrameLocked(ulawBytes.data(), static_cast<int>(ulawBytes.size()))) {
            FROSTY_SESSION_LOGD("writeTxAudioPcm16(): mini voice frame failed");
            return 0;
        }

        return usableBytes;
    }

    bool IaxSession::sendDtmfDigits(const char* digits) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!digits || digits[0] == '\0') {
            return false;
        }

        if (!transport_.isOpen()) {
            return false;
        }

        if (state_ != SessionState::Connected) {
            return false;
        }

        bool anySent = false;
        for (const char* p = digits; *p != '\0'; ++p) {
            if (sendDtmfFrameLocked(*p)) {
                anySent = true;
            } else {
                return false;
            }
        }

        return anySent;
    }

    bool IaxSession::openTransportLocked() {
        if (transport_.isOpen()) {
            return true;
        }

        const bool ok = transport_.open(host_, port_);
        if (ok) {
            setStateLocked(SessionState::SocketReady);
        }
        return ok;
    }

    void IaxSession::closeTransportLocked() {
        transport_.close();
    }

    void IaxSession::setStateLocked(SessionState newState) {
        state_ = newState;
    }

    bool IaxSession::sendInitialNewLocked() {
        static const char* kCalledContext = "iax-client";

        const std::vector<uint8_t> packet = IaxBuilder::buildInitialNew(
                srcCallNo_,
                outboundTimestamp_,
                username_.c_str(),
                remoteNode_.c_str(),
                kCalledContext,
                username_.c_str(),
                username_.c_str(),
                nullptr,
                0
        );

        if (packet.empty()) {
            FROSTY_SESSION_LOGE("sendInitialNewLocked(): packet empty");
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendInitialNewLocked(): send failed");
            return false;
        }

        localOSeq_ = 1;
        outboundTimestamp_ += 20;
        FROSTY_SESSION_LOGD("sendInitialNewLocked(): sent NEW frame size=%zu srcCallNo=%u nextLocalOSeq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_));
        return true;
    }

    bool IaxSession::resendNewWithTokenLocked() {
        static const char* kCalledContext = "iax-client";

        if (callToken_.empty()) {
            FROSTY_SESSION_LOGE("resendNewWithTokenLocked(): no token available");
            return false;
        }

        const std::vector<uint8_t> packet = IaxBuilder::buildInitialNew(
                srcCallNo_,
                outboundTimestamp_,
                username_.c_str(),
                remoteNode_.c_str(),
                kCalledContext,
                username_.c_str(),
                username_.c_str(),
                callToken_.data(),
                callToken_.size()
        );

        if (packet.empty()) {
            FROSTY_SESSION_LOGE("resendNewWithTokenLocked(): packet empty");
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("resendNewWithTokenLocked(): send failed");
            return false;
        }

        tokenSentBack_ = true;
        FROSTY_SESSION_LOGD("resendNewWithTokenLocked(): resent NEW with CALLTOKEN size=%zu srcCallNo=%u localOSeq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_));
        return true;
    }

    bool IaxSession::sendAckLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq) {
        const std::vector<uint8_t> packet = IaxBuilder::buildAck(
                srcCallNo_,
                dstCallNo,
                timestamp,
                localOSeq_,
                iseq
        );

        if (packet.empty()) {
            FROSTY_SESSION_LOGE("sendAckLocked(): packet empty");
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendAckLocked(): send failed");
            return false;
        }

        FROSTY_SESSION_LOGD("sendAckLocked(): sent ACK size=%zu srcCallNo=%u oseq=%u iseq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(iseq));
        return true;
    }

    bool IaxSession::sendAuthRepMd5Locked(
            uint16_t dstCallNo,
            uint32_t timestamp,
            uint8_t iseq,
            const std::string& challenge) {
        if (challenge.empty()) {
            FROSTY_SESSION_LOGE("sendAuthRepMd5Locked(): empty challenge");
            return false;
        }

        const std::string md5Hex = IaxAuth::computeChallengeResponseMd5(challenge, password_);

        const std::vector<uint8_t> packet = IaxBuilder::buildAuthRepMd5(
                srcCallNo_,
                dstCallNo,
                timestamp,
                localOSeq_,
                iseq,
                username_.c_str(),
                md5Hex
        );

        if (packet.empty()) {
            FROSTY_SESSION_LOGE("sendAuthRepMd5Locked(): packet empty");
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendAuthRepMd5Locked(): send failed");
            return false;
        }

        authRepSent_ = true;
        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);
        outboundTimestamp_ += 20;

        FROSTY_SESSION_LOGD("sendAuthRepMd5Locked(): sent AUTHREP size=%zu srcCallNo=%u nextLocalOSeq=%u iseq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(iseq));
        return true;
    }

    bool IaxSession::sendLagRpLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq) {
        const std::vector<uint8_t> packet = IaxBuilder::buildLagRp(
                srcCallNo_,
                dstCallNo,
                timestamp,
                localOSeq_,
                iseq
        );

        if (packet.empty()) {
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            return false;
        }

        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);

        FROSTY_SESSION_LOGD("sendLagRpLocked(): sent LAGRP size=%zu srcCallNo=%u nextLocalOSeq=%u iseq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(iseq));
        return true;
    }

    bool IaxSession::sendPongLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq) {
        const std::vector<uint8_t> packet = IaxBuilder::buildPong(
                srcCallNo_,
                dstCallNo,
                timestamp,
                localOSeq_,
                iseq
        );

        if (packet.empty()) {
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            return false;
        }

        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);

        FROSTY_SESSION_LOGD("sendPongLocked(): sent PONG size=%zu srcCallNo=%u nextLocalOSeq=%u iseq=%u",
                            packet.size(),
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(iseq));
        return true;
    }

    bool IaxSession::sendPingLocked() {
        const std::vector<uint8_t> packet = IaxBuilder::buildPing(
                srcCallNo_,
                peerCallNo_,
                outboundTimestamp_,
                localOSeq_,
                remoteISeq_);

        if (packet.empty()) {
            FROSTY_SESSION_LOGE("sendPingLocked(): packet empty");
            return false;
        }

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendPingLocked(): send failed");
            return false;
        }

        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);
        outboundTimestamp_ += 20;

        FROSTY_SESSION_LOGD("sendPingLocked(): sent PING srcCallNo=%u peerCallNo=%u nextLocalOSeq=%u",
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(peerCallNo_),
                            static_cast<unsigned int>(localOSeq_));
        return true;
    }

    bool IaxSession::sendVoiceFullFrameLocked(const uint8_t* ulawBytes, int numBytes) {
        if (!ulawBytes || numBytes <= 0) {
            return false;
        }

        if (!transport_.isOpen()) {
            return false;
        }

        if (peerCallNo_ == 0) {
            FROSTY_SESSION_LOGE("sendVoiceFullFrameLocked(): peerCallNo is 0");
            return false;
        }

        const uint32_t ts = outboundTimestamp_;

        std::vector<uint8_t> packet;
        packet.resize(static_cast<size_t>(12 + numBytes));

        const uint16_t srcCall = static_cast<uint16_t>(0x8000 | (srcCallNo_ & 0x7fff));
        const uint16_t dstCall = static_cast<uint16_t>(peerCallNo_ & 0x7fff);

        packet[0] = static_cast<uint8_t>((srcCall >> 8) & 0xff);
        packet[1] = static_cast<uint8_t>(srcCall & 0xff);
        packet[2] = static_cast<uint8_t>((dstCall >> 8) & 0x7f);
        packet[3] = static_cast<uint8_t>(dstCall & 0xff);
        packet[4] = static_cast<uint8_t>((ts >> 24) & 0xff);
        packet[5] = static_cast<uint8_t>((ts >> 16) & 0xff);
        packet[6] = static_cast<uint8_t>((ts >> 8) & 0xff);
        packet[7] = static_cast<uint8_t>(ts & 0xff);
        packet[8] = localOSeq_;
        packet[9] = remoteISeq_;
        packet[10] = kFrameTypeVoice;
        packet[11] = kMediaFormatUlawSubclass;

        memcpy(packet.data() + 12, ulawBytes, static_cast<size_t>(numBytes));

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendVoiceFullFrameLocked(): send failed");
            return false;
        }

        FROSTY_SESSION_LOGD("sendVoiceFullFrameLocked(): sent full voice frame bytes=%d srcCallNo=%u ts=%u oseq=%u iseq=%u dstCall=%u subclass=0x%02x",
                            numBytes,
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(ts),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(remoteISeq_),
                            static_cast<unsigned int>(dstCall),
                            static_cast<unsigned int>(kMediaFormatUlawSubclass));

        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);
        outboundTimestamp_ = ts + 20;
        txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);

        return true;
    }

    bool IaxSession::sendVoiceMiniFrameLocked(const uint8_t* ulawBytes, int numBytes) {
        if (!ulawBytes || numBytes <= 0) {
            return false;
        }

        if (!transport_.isOpen()) {
            return false;
        }

        std::vector<uint8_t> packet;
        packet.resize(static_cast<size_t>(4 + numBytes));

        const uint16_t srcCall = static_cast<uint16_t>(srcCallNo_ & 0x7fff);
        const uint16_t ts = txMiniTimestamp_;

        packet[0] = static_cast<uint8_t>((srcCall >> 8) & 0x7f);
        packet[1] = static_cast<uint8_t>(srcCall & 0xff);
        packet[2] = static_cast<uint8_t>((ts >> 8) & 0xff);
        packet[3] = static_cast<uint8_t>(ts & 0xff);

        memcpy(packet.data() + 4, ulawBytes, static_cast<size_t>(numBytes));

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendVoiceMiniFrameLocked(): send failed");
            return false;
        }

        txMiniTimestamp_ = static_cast<uint16_t>(txMiniTimestamp_ + 20);
        outboundTimestamp_ += 20;

        FROSTY_SESSION_LOGD("sendVoiceMiniFrameLocked(): sent mini voice frame bytes=%d srcCallNo=%u ts=%u nextOutboundTs=%u",
                            numBytes,
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(ts),
                            static_cast<unsigned int>(outboundTimestamp_));
        return true;
    }

    bool IaxSession::sendDtmfFrameLocked(char digit) {
        uint8_t subclass = 0;
        if (!dtmfCharToSubclass(digit, &subclass)) {
            FROSTY_SESSION_LOGE("sendDtmfFrameLocked(): unsupported digit %c", digit);
            return false;
        }

        if (!transport_.isOpen()) {
            return false;
        }

        if (peerCallNo_ == 0) {
            FROSTY_SESSION_LOGE("sendDtmfFrameLocked(): peerCallNo is 0");
            return false;
        }

        std::vector<uint8_t> packet;
        packet.resize(12);

        const uint16_t srcCall = static_cast<uint16_t>(0x8000 | (srcCallNo_ & 0x7fff));
        const uint16_t dstCall = static_cast<uint16_t>(peerCallNo_ & 0x7fff);
        const uint32_t ts = outboundTimestamp_;

        packet[0] = static_cast<uint8_t>((srcCall >> 8) & 0xff);
        packet[1] = static_cast<uint8_t>(srcCall & 0xff);
        packet[2] = static_cast<uint8_t>((dstCall >> 8) & 0x7f);
        packet[3] = static_cast<uint8_t>(dstCall & 0xff);
        packet[4] = static_cast<uint8_t>((ts >> 24) & 0xff);
        packet[5] = static_cast<uint8_t>((ts >> 16) & 0xff);
        packet[6] = static_cast<uint8_t>((ts >> 8) & 0xff);
        packet[7] = static_cast<uint8_t>(ts & 0xff);
        packet[8] = localOSeq_;
        packet[9] = remoteISeq_;
        packet[10] = kFrameTypeDtmf;
        packet[11] = subclass;

        if (!transport_.sendPacket(packet.data(), packet.size())) {
            FROSTY_SESSION_LOGE("sendDtmfFrameLocked(): send failed for digit %c", digit);
            return false;
        }

        FROSTY_SESSION_LOGD("sendDtmfFrameLocked(): sent digit=%c srcCallNo=%u ascii=%u ts=%u oseq=%u iseq=%u",
                            digit,
                            static_cast<unsigned int>(srcCallNo_),
                            static_cast<unsigned int>(subclass),
                            static_cast<unsigned int>(ts),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(remoteISeq_));

        localOSeq_ = static_cast<uint8_t>(localOSeq_ + 1);
        outboundTimestamp_ += kDtmfStepMs;
        txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);

        return true;
    }

    bool IaxSession::extractCallTokenIeLocked(const uint8_t* data, int size, std::vector<uint8_t>& outToken) {
        outToken.clear();

        if (!data || size <= 12) {
            return false;
        }

        int pos = 12;
        while (pos + 2 <= size) {
            const uint8_t ieType = data[pos];
            const uint8_t ieLen = data[pos + 1];
            pos += 2;

            if (pos + ieLen > size) {
                FROSTY_SESSION_LOGE("extractCallTokenIeLocked(): malformed IE block");
                return false;
            }

            if (ieType == 0x36) {
                outToken.assign(data + pos, data + pos + ieLen);
                return true;
            }

            pos += ieLen;
        }

        return false;
    }

    bool IaxSession::extractAuthReqLocked(
            const uint8_t* data,
            int size,
            uint16_t& authMethods,
            std::string& challenge,
            std::string& username) {
        authMethods = 0;
        challenge.clear();
        username.clear();

        if (!data || size <= 12) {
            return false;
        }

        int pos = 12;
        while (pos + 2 <= size) {
            const uint8_t ieType = data[pos];
            const uint8_t ieLen = data[pos + 1];
            pos += 2;

            if (pos + ieLen > size) {
                FROSTY_SESSION_LOGE("extractAuthReqLocked(): malformed IE block");
                return false;
            }

            if (ieType == 0x0e && ieLen == 2) {
                authMethods = readU16(data + pos);
            } else if (ieType == 0x0f) {
                challenge.assign(reinterpret_cast<const char*>(data + pos), ieLen);
            } else if (ieType == 0x06) {
                username.assign(reinterpret_cast<const char*>(data + pos), ieLen);
            }

            pos += ieLen;
        }

        return true;
    }

    void IaxSession::pushRxAudioLocked(const uint8_t* data, int size) {
        if (!data || size <= 0) {
            return;
        }

        const size_t oldSize = rxAudioBuffer_.size();
        rxAudioBuffer_.resize(oldSize + static_cast<size_t>(size));
        memcpy(rxAudioBuffer_.data() + oldSize, data, static_cast<size_t>(size));

        static const size_t kMaxBufferedBytes = 64000;
        if (rxAudioBuffer_.size() > kMaxBufferedBytes) {
            const size_t trim = rxAudioBuffer_.size() - kMaxBufferedBytes;
            rxAudioBuffer_.erase(rxAudioBuffer_.begin(), rxAudioBuffer_.begin() + static_cast<long>(trim));
        }
    }

    void IaxSession::logPacketSummaryLocked(const uint8_t* data, int size) {
        if (!data || size <= 0) {
            return;
        }

        if (size < 4) {
            FROSTY_SESSION_LOGD("pump(): packet too short size=%d", size);
            return;
        }

        const uint16_t rawSrcCall = readU16(data + 0);
        const bool isFullFrame = (rawSrcCall & 0x8000) != 0;

        if (!isFullFrame) {
            const uint16_t miniSrcCall = static_cast<uint16_t>(rawSrcCall & 0x7fff);
            const uint16_t miniTs = readU16(data + 2);

            FROSTY_SESSION_LOGD("pump(): non-full frame size=%d srcCall=%u ts=%u first4=%02x %02x %02x %02x",
                                size,
                                static_cast<unsigned int>(miniSrcCall),
                                static_cast<unsigned int>(miniTs),
                                data[0] & 0xff,
                                data[1] & 0xff,
                                data[2] & 0xff,
                                data[3] & 0xff);

            if (size > 4) {
                const uint8_t* ulawData = data + 4;
                const int ulawSize = size - 4;

                std::vector<uint8_t> pcmBytes;
                pcmBytes.resize(static_cast<size_t>(ulawSize) * 2);

                for (int i = 0; i < ulawSize; ++i) {
                    const int16_t sample = ulawToPcm16(ulawData[i]);
                    pcmBytes[static_cast<size_t>(i) * 2] = static_cast<uint8_t>(sample & 0xff);
                    pcmBytes[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>((sample >> 8) & 0xff);
                }

                pushRxAudioLocked(pcmBytes.data(), static_cast<int>(pcmBytes.size()));
            }

            return;
        }

        if (size < 12) {
            FROSTY_SESSION_LOGD("pump(): short full frame size=%d", size);
            return;
        }

        const uint16_t srcCall = static_cast<uint16_t>(readU16(data + 0) & 0x7fff);
        const uint16_t dstCall = static_cast<uint16_t>(readU16(data + 2) & 0x7fff);
        const uint32_t ts = readU32(data + 4);
        const uint8_t oseq = data[8];
        const uint8_t iseq = data[9];
        const uint8_t frameType = data[10];
        const uint8_t subclass = data[11];

        remoteISeq_ = static_cast<uint8_t>(oseq + 1);

        FROSTY_SESSION_LOGD("pump(): full frame size=%d bytes=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                            size,
                            data[0] & 0xff,
                            data[1] & 0xff,
                            data[2] & 0xff,
                            data[3] & 0xff,
                            data[4] & 0xff,
                            data[5] & 0xff,
                            data[6] & 0xff,
                            data[7] & 0xff,
                            data[8] & 0xff,
                            data[9] & 0xff,
                            data[10] & 0xff,
                            data[11] & 0xff);

        FROSTY_SESSION_LOGD("pump(): header srcCall=%u dstCall=%u ts=%u oseq=%u iseq=%u frameType=%u subclass=0x%02x localOSeq=%u remoteISeq=%u localSrcCallNo=%u",
                            static_cast<unsigned int>(srcCall),
                            static_cast<unsigned int>(dstCall),
                            static_cast<unsigned int>(ts),
                            static_cast<unsigned int>(oseq),
                            static_cast<unsigned int>(iseq),
                            static_cast<unsigned int>(frameType),
                            static_cast<unsigned int>(subclass),
                            static_cast<unsigned int>(localOSeq_),
                            static_cast<unsigned int>(remoteISeq_),
                            static_cast<unsigned int>(srcCallNo_));

        if (frameType == kFrameTypeIax && subclass == 0x28) {
            std::vector<uint8_t> token;
            if (!extractCallTokenIeLocked(data, size, token)) {
                FROSTY_SESSION_LOGE("pump(): CALLTOKEN subclass received but token IE missing");
                setStateLocked(SessionState::Error);
                return;
            }

            callToken_ = token;
            tokenReceived_ = true;

            FROSTY_SESSION_LOGD("pump(): server sent CALLTOKEN len=%zu", callToken_.size());

            if (!tokenSentBack_) {
                if (!resendNewWithTokenLocked()) {
                    setStateLocked(SessionState::Error);
                    return;
                }
                FROSTY_SESSION_LOGD("pump(): resent NEW with returned CALLTOKEN");
                setStateLocked(SessionState::Connecting);
            }

            return;
        }

        if (frameType == kFrameTypeIax && subclass == 6) {
            FROSTY_SESSION_LOGD("pump(): server sent REJECT");
            sendAckLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            setStateLocked(SessionState::Error);
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 8) {
            uint16_t authMethods = 0;
            std::string challenge;
            std::string authUser;

            if (!extractAuthReqLocked(data, size, authMethods, challenge, authUser)) {
                FROSTY_SESSION_LOGE("pump(): failed to parse AUTHREQ");
                setStateLocked(SessionState::Error);
                return;
            }

            FROSTY_SESSION_LOGD("pump(): server sent AUTHREQ methods=0x%04x",
                                static_cast<unsigned int>(authMethods));

            if ((authMethods & 0x0002) == 0) {
                FROSTY_SESSION_LOGE("pump(): AUTHREQ does not offer MD5");
                setStateLocked(SessionState::Error);
                return;
            }

            if (!sendAuthRepMd5Locked(srcCall, ts, static_cast<uint8_t>(oseq + 1), challenge)) {
                setStateLocked(SessionState::Error);
                return;
            }

            setStateLocked(SessionState::Connecting);
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 7) {
            peerCallNo_ = srcCall;
            txCodecAnnounced_ = false;

            if (ts >= outboundTimestamp_) {
                outboundTimestamp_ = ts + 20;
            }
            txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);

            FROSTY_SESSION_LOGD(
                    "pump(): server sent ACCEPT peerCall=%u acceptTs=%u outbound=%u mini=%u localSrcCallNo=%u",
                    static_cast<unsigned int>(peerCallNo_),
                    static_cast<unsigned int>(ts),
                    static_cast<unsigned int>(outboundTimestamp_),
                    static_cast<unsigned int>(txMiniTimestamp_),
                    static_cast<unsigned int>(srcCallNo_));

            sendAckLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            setStateLocked(SessionState::Connected);
            markInboundActivityLocked();
            lastPingSentAtMs_ = 0;
            return;
        }

        if (frameType == kFrameTypeVoice) {
            lastRxVoiceTimestamp_ = ts;
            haveRxVoiceTimestamp_ = true;

            FROSTY_SESSION_LOGD("pump(): inbound full voice frame subclass=0x%02x bytes=%d",
                                static_cast<unsigned int>(subclass),
                                size - 12);

            if (size > 12) {
                const uint8_t* ulawData = data + 12;
                const int ulawSize = size - 12;

                std::vector<uint8_t> pcmBytes;
                pcmBytes.resize(static_cast<size_t>(ulawSize) * 2);

                for (int i = 0; i < ulawSize; ++i) {
                    const int16_t sample = ulawToPcm16(ulawData[i]);
                    pcmBytes[static_cast<size_t>(i) * 2] = static_cast<uint8_t>(sample & 0xff);
                    pcmBytes[static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>((sample >> 8) & 0xff);
                }

                pushRxAudioLocked(pcmBytes.data(), static_cast<int>(pcmBytes.size()));
            }

            sendAckLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 0x0b) {
            FROSTY_SESSION_LOGD("pump(): server sent LAGRQ");
            if (ts >= outboundTimestamp_) {
                outboundTimestamp_ = ts + 20;
                txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);
            }
            sendLagRpLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 0x02) {
            FROSTY_SESSION_LOGD("pump(): server sent PING");
            if (ts >= outboundTimestamp_) {
                outboundTimestamp_ = ts + 20;
                txMiniTimestamp_ = static_cast<uint16_t>(outboundTimestamp_ & 0xffff);
            }
            sendPongLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 0x03) {
            FROSTY_SESSION_LOGD("pump(): server sent PONG");
            sendAckLocked(srcCall, ts, static_cast<uint8_t>(oseq + 1));
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 0x05) {
            FROSTY_SESSION_LOGD("pump(): server sent HANGUP");
            pttPressed_ = false;
            txCodecAnnounced_ = false;
            peerCallNo_ = 0;
            setStateLocked(SessionState::Disconnected);
            closeTransportLocked();
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 4) {
            FROSTY_SESSION_LOGD("pump(): server sent ACK");
            return;
        }

        if (frameType == kFrameTypeIax && subclass == 0x12) {
            FROSTY_SESSION_LOGD("pump(): server sent VNAK");
            return;
        }
    }

}  // namespace frosty