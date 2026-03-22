#pragma once

#include <jni.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class IaxNativeClient {
public:
    IaxNativeClient(JavaVM* vm, jobject bridgeObj);
    ~IaxNativeClient();

    void initialize();
    bool connect(
            const std::string& username,
            const std::string& password,
            const std::string& host,
            int port,
            const std::string& remoteNode);
    void disconnect();
    void setPttPressed(bool pressed);
    void release();

    int readRxAudio(uint8_t* outBuffer, int maxBytes);
    int getRxSampleRate() const;
    int getRxChannelCount() const;
    int writeTxAudioPcm16(const uint8_t* pcmBytes, int numBytes);

private:
    void startWorker();
    void stopWorker();
    void workerLoop();

    void notifyState(const char* state, const char* detail);
    void notifyLog(const char* message);

    bool sendPhoneModeDtmf(const char* digits);

private:
    JavaVM* vm_;
    jobject bridgeGlobalRef_;
    jmethodID onNativeStateChanged_;
    jmethodID onNativeLog_;

    std::mutex stateMutex_;
    std::atomic<bool> initialized_;
    std::atomic<bool> running_;
    std::atomic<bool> connected_;
    std::atomic<bool> pttPressed_;

    std::thread worker_;

    std::string username_;
    std::string password_;
    std::string host_;
    std::string remoteNode_;
    int port_;

    void* iaxHandle_;

    int rxSampleRate_;
    int rxChannelCount_;

    std::atomic<long long> txAudioAllowedAtMs_;
};