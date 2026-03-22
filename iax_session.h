#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "iax_frame.h"
#include "iax_udp_transport.h"

namespace frosty {

    class IaxSession {
    public:
        IaxSession();
        ~IaxSession();

        bool initialize();
        bool connect(const ConnectParams& params);
        void disconnect();
        void setPtt(bool pressed);
        void pump();

        SessionState state() const;
        bool isConnected() const;

        int readRxAudio(uint8_t* outBuffer, int maxBytes);
        int getRxSampleRate() const;
        int getRxChannelCount() const;

        int writeTxAudioPcm16(const uint8_t* pcmBytes, int numBytes);
        bool sendDtmfDigits(const char* digits);

    private:
        static uint16_t generateSrcCallNo();
        static int64_t nowMs();

        bool openTransportLocked();
        void closeTransportLocked();
        void setStateLocked(SessionState newState);
        void markInboundActivityLocked();
        bool checkLinkTimeoutLocked();
        void maybeSendKeepAliveLocked();

        bool sendInitialNewLocked();
        bool resendNewWithTokenLocked();
        bool sendAckLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq);
        bool sendAuthRepMd5Locked(
                uint16_t dstCallNo,
                uint32_t timestamp,
                uint8_t iseq,
                const std::string& challenge);
        bool sendLagRpLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq);
        bool sendPongLocked(uint16_t dstCallNo, uint32_t timestamp, uint8_t iseq);
        bool sendPingLocked();

        bool sendVoiceFullFrameLocked(const uint8_t* ulawBytes, int numBytes);
        bool sendVoiceMiniFrameLocked(const uint8_t* ulawBytes, int numBytes);
        bool sendDtmfFrameLocked(char digit);

        bool extractCallTokenIeLocked(const uint8_t* data, int size, std::vector<uint8_t>& outToken);
        bool extractAuthReqLocked(
                const uint8_t* data,
                int size,
                uint16_t& authMethods,
                std::string& challenge,
                std::string& username);

        void pushRxAudioLocked(const uint8_t* data, int size);
        void logPacketSummaryLocked(const uint8_t* data, int size);

    private:
        mutable std::mutex mutex_;
        IaxUdpTransport transport_;
        SessionState state_;
        bool pttPressed_;

        std::string username_;
        std::string password_;
        std::string host_;
        std::string remoteNode_;
        int port_;

        uint16_t srcCallNo_;
        uint16_t peerCallNo_;
        uint32_t outboundTimestamp_;
        uint16_t txMiniTimestamp_;

        std::vector<uint8_t> callToken_;
        bool tokenReceived_;
        bool tokenSentBack_;
        bool authRepSent_;
        bool txCodecAnnounced_;

        std::vector<uint8_t> rxAudioBuffer_;
        int rxSampleRate_;
        int rxChannelCount_;

        uint8_t localOSeq_;
        uint8_t remoteISeq_;

        uint32_t lastRxVoiceTimestamp_;
        bool haveRxVoiceTimestamp_;

        int64_t lastInboundPacketAtMs_;
        int64_t lastPingSentAtMs_;
    };

}  // namespace frosty