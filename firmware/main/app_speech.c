/*
 * Spoken recipes: speech/<id>.opk clips read in place from the mapped recipe pack,
 * decoded with esp_audio_codec's Opus decoder (16 kHz mono) and streamed to the ES8311
 * by a worker task, one 60 ms packet at a time. Every play or stop bumps a generation
 * counter; the worker drops a clip as soon as its generation is stale, so playback ends
 * within about one packet plus the DMA buffers, and the UI never waits for it.
 *
 * Volume is the codec's output volume (Settings > Volume). With Sound off nothing
 * plays, and turning Sound off stops a clip that is playing.
 */
#include <string.h>
#include "app.h"
#include "adb_speech.h"
#include "bsp.h"
#include "decoder/impl/esp_opus_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "speech";

#define MAX_FRAME   (ADB_SPEECH_RATE * 60 / 1000)    // one 60 ms Opus frame
#define STACK_BYTES (16 * 1024)                      // the Opus decoder is stack-hungry

static const adb_pack_t *s_pack;
typedef struct {
    adb_speech_t clip;
    uint32_t gen;
} request_t;

static QueueHandle_t s_requests;          // request_t
static volatile uint32_t s_gen;           // bumped by every play and stop
static volatile bool s_playing;           // what the UI shows: set by play, cleared by stop / clip end
static volatile bool s_busy;              // the worker holds the audio output
static volatile bool s_enabled = true;    // Settings > Sound
static int16_t *s_pcm;

static void play_clip(adb_speech_t *clip, uint32_t gen)
{
    esp_opus_dec_cfg_t cfg = {
        .sample_rate = ADB_SPEECH_RATE,
        .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,     // opusenc --framesize 60 (adb/audio.py)
        .self_delimited = false,
    };
    void *dec = NULL;
    if (esp_opus_dec_open(&cfg, sizeof cfg, &dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "opus decoder open failed");
        return;
    }
    if (bsp_audio_stream_begin(500) != ESP_OK) {
        ESP_LOGW(TAG, "audio busy");
        esp_opus_dec_close(dec);
        return;
    }
    int64_t t0 = esp_timer_get_time();
    uint32_t skip = clip->pre_skip, left = clip->samples, packets = 0;
    int64_t dec_us = 0;
    const uint8_t *pkt;
    int n;
    while (gen == s_gen && left && (n = adb_speech_next(clip, &pkt)) > 0) {
        esp_audio_dec_in_raw_t raw = { .buffer = (uint8_t *)pkt, .len = (uint32_t)n };
        esp_audio_dec_out_frame_t out = { .buffer = (uint8_t *)s_pcm, .len = MAX_FRAME * sizeof(int16_t) };
        esp_audio_dec_info_t info;
        int64_t td = esp_timer_get_time();
        if (esp_opus_dec_decode(dec, &raw, &out, &info) != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "decode failed at packet %u", (unsigned)packets);
            break;
        }
        dec_us += esp_timer_get_time() - td;
        packets++;
        uint32_t got = out.decoded_size / sizeof(int16_t), off = 0;
        if (skip) {
            off = skip < got ? skip : got;
            skip -= off;
        }
        uint32_t use = got - off < left ? got - off : left;
        if (use && bsp_audio_stream_write(s_pcm + off, use) != ESP_OK) break;
        left -= use;
    }
    bsp_audio_stream_end();
    esp_opus_dec_close(dec);
    uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "%s after %u.%u s (%u packets, decode %u%% of real time)",
             gen != s_gen ? "stopped" : "done", (unsigned)(ms / 1000), (unsigned)(ms % 1000 / 100),
             (unsigned)packets, ms ? (unsigned)(dec_us / 10 / ms) : 0);
}

static void speech_task(void *arg)
{
    for (;;) {
        request_t rq;
        xQueueReceive(s_requests, &rq, portMAX_DELAY);
        if (rq.gen != s_gen) continue;
        s_busy = true;
        play_clip(&rq.clip, rq.gen);
        s_busy = false;
        if (rq.gen == s_gen) s_playing = false;   // ended by itself
    }
}

void app_speech_init(const adb_pack_t *pack)
{
    s_pack = pack;
    s_pcm = heap_caps_malloc(MAX_FRAME * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    s_requests = xQueueCreate(1, sizeof(request_t));
    if (!s_pcm || !s_requests || xTaskCreate(speech_task, "speech", STACK_BYTES, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the speech task");
        s_pack = NULL;
        return;
    }
    int n = 0;
    for (uint32_t i = 0; i < pack->n; i++) n += !strncmp(pack->toc[i].name, "speech/", 7);
    ESP_LOGI(TAG, "%d spoken recipes in the pack", n);
}

bool app_speech_available(const char *drink_id)
{
    return s_pack && adb_speech_find(NULL, s_pack, drink_id) == 0;
}

bool app_speech_play(const char *drink_id)
{
    request_t rq;
    if (!s_pack || !s_enabled || adb_speech_find(&rq.clip, s_pack, drink_id) != 0) return false;
    adb_speech_t clip = rq.clip;
    rq.gen = ++s_gen;
    s_playing = true;
    ESP_LOGI(TAG, "playing %s (%u.%u s)", drink_id, (unsigned)(adb_speech_ms(&clip) / 1000),
             (unsigned)(adb_speech_ms(&clip) % 1000 / 100));
    xQueueOverwrite(s_requests, &rq);
    return true;
}

void app_speech_stop(bool wait)
{
    s_gen++;
    s_playing = false;
    xQueueReset(s_requests);
    // The worker notices within a packet. Waiting is for callers about to sleep.
    for (int i = 0; wait && i < 50 && s_busy; i++) vTaskDelay(pdMS_TO_TICKS(10));
    if (wait && s_busy) ESP_LOGW(TAG, "stop: worker still busy after 500 ms");
}

bool app_speech_playing(void) { return s_playing; }

void app_speech_set_enabled(bool on)
{
    s_enabled = on;
    if (!on) app_speech_stop(false);
}
