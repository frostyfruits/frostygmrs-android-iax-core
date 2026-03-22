#include <jni.h>
#include <android/log.h>
#include <memory>
#include <mutex>
#include <string>

#include "IaxNativeClient.h"

#define FROSTY_BRIDGE_TAG "NativeIaxBridgeJNI"
#define FROSTY_BRIDGE_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, FROSTY_BRIDGE_TAG, __VA_ARGS__)
#define FROSTY_BRIDGE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, FROSTY_BRIDGE_TAG, __VA_ARGS__)

static JavaVM* g_vm = nullptr;
static std::mutex g_bridgeMutex;
static std::unique_ptr<IaxNativeClient> g_client;

static IaxNativeClient* getOrCreateClient(JNIEnv* env, jobject thiz) {
    if (!g_vm || !env || !thiz) {
        FROSTY_BRIDGE_LOGE(
                "getOrCreateClient: invalid inputs");
        return nullptr;
    }

    if (!g_client) {
        FROSTY_BRIDGE_LOGD("getOrCreateClient: creating native client");
        g_client = std::make_unique<IaxNativeClient>(g_vm, thiz);
        g_client->initialize();
    }

    return g_client.get();
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    g_vm = vm;
    FROSTY_BRIDGE_LOGD("JNI_OnLoad");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_frostygmrs_mobile_NativeIaxBridge_initialize(JNIEnv* env, jobject thiz) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);
FROSTY_BRIDGE_LOGD("initialize()");
(void)getOrCreateClient(env, thiz);
}

extern "C" JNIEXPORT jboolean JNICALL
        Java_com_frostygmrs_mobile_NativeIaxBridge_connect(
        JNIEnv* env,
        jobject thiz,
jstring username,
        jstring password,
jstring host,
        jint port,
jstring remoteNode) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
if (!client || !env) {
FROSTY_BRIDGE_LOGE("connect: no client or env");
return JNI_FALSE;
}

const char* c_username = username ? env->GetStringUTFChars(username, nullptr) : nullptr;
const char* c_password = password ? env->GetStringUTFChars(password, nullptr) : nullptr;
const char* c_host = host ? env->GetStringUTFChars(host, nullptr) : nullptr;
const char* c_remoteNode = remoteNode ? env->GetStringUTFChars(remoteNode, nullptr) : nullptr;

FROSTY_BRIDGE_LOGD(
        "connect requested");

const bool ok = client->connect(
        c_username ? c_username : "",
        c_password ? c_password : "",
        c_host ? c_host : "",
        static_cast<int>(port),
        c_remoteNode ? c_remoteNode : "");

if (c_username) {
env->ReleaseStringUTFChars(username, c_username);
}
if (c_password) {
env->ReleaseStringUTFChars(password, c_password);
}
if (c_host) {
env->ReleaseStringUTFChars(host, c_host);
}
if (c_remoteNode) {
env->ReleaseStringUTFChars(remoteNode, c_remoteNode);
}

FROSTY_BRIDGE_LOGD("connect: result=%d", ok ? 1 : 0);
return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_frostygmrs_mobile_NativeIaxBridge_disconnect(JNIEnv* env, jobject thiz) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
FROSTY_BRIDGE_LOGD("disconnect requested");

if (client) {
client->disconnect();
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_frostygmrs_mobile_NativeIaxBridge_setPttPressed(
        JNIEnv* env,
jobject thiz,
        jboolean pressed) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
FROSTY_BRIDGE_LOGD(
        "setPttPressed: pressed=%d",
        pressed == JNI_TRUE ? 1 : 0);

if (client) {
client->setPttPressed(pressed == JNI_TRUE);
} else {
FROSTY_BRIDGE_LOGE("setPttPressed: no client");
}
}

extern "C" JNIEXPORT jint JNICALL
        Java_com_frostygmrs_mobile_NativeIaxBridge_readRxAudio(
        JNIEnv* env,
        jobject thiz,
jbyteArray buffer,
        jint maxBytes) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
if (!client || !env || !buffer || maxBytes <= 0) {
return 0;
}

const jsize arrayLen = env->GetArrayLength(buffer);
const jint toRead = maxBytes < arrayLen ? maxBytes : arrayLen;
if (toRead <= 0) {
return 0;
}

jbyte* bytes = env->GetByteArrayElements(buffer, nullptr);
if (!bytes) {
FROSTY_BRIDGE_LOGE("readRxAudio: GetByteArrayElements failed");
return 0;
}

const int read = client->readRxAudio(
        reinterpret_cast<uint8_t*>(bytes),
        static_cast<int>(toRead));

env->ReleaseByteArrayElements(buffer, bytes, 0);
return static_cast<jint>(read);
}

extern "C" JNIEXPORT jint JNICALL
        Java_com_frostygmrs_mobile_NativeIaxBridge_getRxSampleRate(JNIEnv* env, jobject thiz) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
if (!client) {
return 8000;
}

return static_cast<jint>(client->getRxSampleRate());
}

extern "C" JNIEXPORT jint JNICALL
        Java_com_frostygmrs_mobile_NativeIaxBridge_getRxChannelCount(JNIEnv* env, jobject thiz) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
if (!client) {
return 1;
}

return static_cast<jint>(client->getRxChannelCount());
}

extern "C" JNIEXPORT jint JNICALL
        Java_com_frostygmrs_mobile_NativeIaxBridge_writeTxAudioPcm16(
        JNIEnv* env,
        jobject thiz,
jbyteArray buffer,
        jint length) {
std::lock_guard<std::mutex> lock(g_bridgeMutex);

IaxNativeClient* client = getOrCreateClient(env, thiz);
if (!client || !env || !buffer || length <= 0) {
return 0;
}

const jsize arrayLen = env->GetArrayLength(buffer);
const jint toWrite = length < arrayLen ? length : arrayLen;
if (toWrite <= 0) {
return 0;
}

jbyte* bytes = env->GetByteArrayElements(buffer, nullptr);
if (!bytes) {
FROSTY_BRIDGE_LOGE("writeTxAudioPcm16: GetByteArrayElements failed");
return 0;
}

const int written = client->writeTxAudioPcm16(
        reinterpret_cast<const uint8_t*>(bytes),
        static_cast<int>(toWrite));

env->ReleaseByteArrayElements(buffer, bytes, JNI_ABORT);

if (written <= 0) {
FROSTY_BRIDGE_LOGD("writeTxAudioPcm16: wrote=%d requested=%d", written, static_cast<int>(toWrite));
}

return static_cast<jint>(written);
}

extern "C" JNIEXPORT void JNICALL
Java_com_frostygmrs_mobile_NativeIaxBridge_release(JNIEnv* env, jobject thiz) {
(void)env;
(void)thiz;

std::lock_guard<std::mutex> lock(g_bridgeMutex);
FROSTY_BRIDGE_LOGD("release");

if (g_client) {
g_client->release();
g_client.reset();
}
}