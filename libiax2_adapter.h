#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void* frosty_iax_create();

int frosty_iax_connect(
        void* handle,
        const char* username,
        const char* password,
        const char* host,
        int port,
        const char* remoteNode);

void frosty_iax_disconnect(void* handle);
void frosty_iax_set_ptt(void* handle, int pressed);
void frosty_iax_pump(void* handle);

int frosty_iax_read_rx_audio(void* handle, unsigned char* outBuffer, int maxBytes);
int frosty_iax_get_rx_sample_rate(void* handle);
int frosty_iax_get_rx_channel_count(void* handle);
int frosty_iax_write_tx_audio_pcm16(void* handle, const unsigned char* pcmBytes, int numBytes);
int frosty_iax_send_dtmf(void* handle, const char* digits);
int frosty_iax_is_connected(void* handle);

void frosty_iax_destroy(void* handle);

#ifdef __cplusplus
}
#endif