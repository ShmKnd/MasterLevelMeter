#include <obs-module.h>
#include <obs.h>
#include "level_calc.h"
#include "meter_widget.h"
#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <obs-frontend-api.h>
#include "media-io/audio-io.h"
#include <cstdint>
#include "util/config-file.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("level-meter-plugin", "en-US")

// 追加: モジュール名/説明
const char *obs_module_name(void)
{
    return "Level Meter (dBFS)";
}

const char *obs_module_description(void)
{
    return "Floating window level meter for master bus (Qt).";
}

static LevelCalc g_levelCalc;
static MeterWidget *g_meterWidget = nullptr;
static QTimer *g_updateTimer = nullptr;
static QTimer *g_streamInfoTimer = nullptr; // streaming mix info updater

// OBS audio connection state
static audio_t *g_audio = nullptr;
static size_t g_mixIdx = 0; // master mix
static size_t g_channels = 0;
static bool g_connected = false;

static void audio_callback(void *param, size_t mix_idx, struct audio_data *data) {
    Q_UNUSED(param);
    // 選択中ミックス以外は無視
    if (mix_idx != g_mixIdx) return;

    if (!data || g_channels == 0)
        return;

    // Planar float expected; build channel pointers
    float *planes[MAX_AUDIO_CHANNELS] = {nullptr};
    size_t chs = g_channels;
    for (size_t ch = 0; ch < chs; ++ch) {
        planes[ch] = reinterpret_cast<float*>(data->data[ch]);
    }

    g_levelCalc.process(reinterpret_cast<float **>(planes), data->frames, chs);
}

static void update_window_title()
{
    if (g_meterWidget) {
        int trackNo = (int)g_mixIdx + 1; // 1..6 表示
        QString title = QString("Master Level Meter - Track%1").arg(trackNo);
        g_meterWidget->setWindowTitle(title);
    }
}

static bool reconnect_to_mix(size_t newMixIdx)
{
    if (newMixIdx > 5) newMixIdx = 5; // Track1..6 = mix 0..5
    if (g_audio == nullptr) {
        g_mixIdx = newMixIdx;
        update_window_title();
        return false;
    }

    if (g_connected) {
        audio_output_disconnect(g_audio, g_mixIdx, audio_callback, nullptr);
        g_connected = false;
    }

    // 現在のオーディオ情報から変換設定を作成
    struct obs_audio_info oai = {};
    if (!obs_get_audio_info(&oai)) {
        blog(LOG_WARNING, "Level Meter Plugin: obs_get_audio_info failed in reconnect");
        g_mixIdx = newMixIdx;
        update_window_title();
        return false;
    }

    struct audio_convert_info conv = {};
    conv.samples_per_sec = oai.samples_per_sec;
    conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
    conv.speakers = oai.speakers;
    conv.allow_clipping = false;

    bool ok = audio_output_connect(g_audio, newMixIdx, &conv, audio_callback, nullptr);
    if (!ok) {
        blog(LOG_WARNING, "Level Meter Plugin: audio_output_connect failed for mix %zu", newMixIdx);
    } else {
        blog(LOG_INFO, "Level Meter Plugin: connected to mix %zu", newMixIdx);
    }
    g_connected = ok;
    g_mixIdx = newMixIdx;

    // 保存
    QSettings settings("ha_kondo", "level_meter_plugin");
    settings.setValue("audio/mix_index", (int)g_mixIdx);

    update_window_title();
    return ok;
}

static void switch_mix_menu_cb(void *param)
{
    size_t idx = (size_t)(intptr_t)param;
    reconnect_to_mix(idx);
}

static void show_meter_menu_cb(void *param)
{
    Q_UNUSED(param);
    if (g_meterWidget) {
        g_meterWidget->show();
        g_meterWidget->raise();
        g_meterWidget->activateWindow();
    }
}

static uint32_t get_streaming_mixers_from_settings()
{
    uint32_t mask = 0;

    // 1) 設定から取得（プロファイル）
    config_t *cfg = obs_frontend_get_profile_config();
    if (cfg) {
        const char *mode = config_get_string(cfg, "Output", "Mode"); // "Simple" or "Advanced"
        int trackIndex = 1; // Simpleモード時はTrack1
        if (mode && strcmp(mode, "Advanced") == 0) {
            trackIndex = (int)config_get_int(cfg, "AdvOut", "TrackIndex");
        }
        if (trackIndex >= 1 && trackIndex <= 6) {
            mask = (1u << (trackIndex - 1));
        }
    }

    // 2) フォールバック: 出力オブジェクトから取得
    if (mask == 0) {
        obs_output_t *out = obs_frontend_get_streaming_output();
        if (out) {
            mask = static_cast<uint32_t>(obs_output_get_mixers(out));
            obs_output_release(out);
        }
    }

    return mask;
}

bool obs_module_load(void) {
    // UI
    g_meterWidget = new MeterWidget();
    g_meterWidget->setWindowTitle("Master Level Meter");
    // フローティング&前面
    g_meterWidget->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);

    // シグナル: UIドロップダウンからのミックス変更
    QObject::connect(g_meterWidget, &MeterWidget::mixIndexChanged, [](int idx){
        reconnect_to_mix((size_t)idx);
    });

    // 前回サイズ復元 or 既定サイズ（最小 + 高さ+48px）
    QSettings settings("ha_kondo", "level_meter_plugin");
    QByteArray geom = settings.value("window/geometry").toByteArray();
    if (!geom.isEmpty() && g_meterWidget->restoreGeometry(geom)) {
        // 復元成功
    } else {
        QSize init = g_meterWidget->minimumSizeHint();
        init.setHeight(init.height() + 48); // さらに余裕
        g_meterWidget->resize(init);
    }
    g_meterWidget->show();

    // Tools メニューに「Show Master Level Meter」を追加
    obs_frontend_add_tools_menu_item("Show Master Level Meter", show_meter_menu_cb, nullptr);

    // Audio connect
    struct obs_audio_info oai = {};
    if (obs_get_audio_info(&oai)) {
        g_audio = obs_get_audio();
        g_channels = get_audio_channels(oai.speakers);

        // 前回のMixインデックスを復元（0..5）
        QSettings settings("ha_kondo", "level_meter_plugin");
        int savedMix = settings.value("audio/mix_index", 0).toInt();
        if (savedMix < 0) savedMix = 0; if (savedMix > 5) savedMix = 5;
        g_mixIdx = (size_t)savedMix;
        // UIにも反映
        g_meterWidget->setMixIndex(savedMix);

        // LevelCalc にサンプルレート/チャンネル数を通知
        g_levelCalc.setSampleRate(oai.samples_per_sec);
        g_levelCalc.setChannels(g_channels);

        struct audio_convert_info conv = {};
        conv.samples_per_sec = oai.samples_per_sec;
        conv.format = AUDIO_FORMAT_FLOAT_PLANAR;
        conv.speakers = oai.speakers;
        conv.allow_clipping = false;

        if (g_audio && g_channels > 0) {
            g_connected = audio_output_connect(g_audio, g_mixIdx, &conv, audio_callback, nullptr);
            if (!g_connected) {
                blog(LOG_WARNING, "Level Meter Plugin: audio_output_connect failed");
            }
        } else {
            blog(LOG_WARNING, "Level Meter Plugin: audio not available or channels=0");
        }
    } else {
        blog(LOG_WARNING, "Level Meter Plugin: obs_get_audio_info failed");
    }

    // タイトル更新
    update_window_title();

    // 更新用タイマー（UIスレッド）: 約60fps
    g_updateTimer = new QTimer();
    QObject::connect(g_updateTimer, &QTimer::timeout, [=]() {
        size_t chs = g_levelCalc.getChannels();
        float rmsL = (chs >= 1) ? g_levelCalc.getRMSCh(0) : g_levelCalc.getRMS();
        float rmsR = (chs >= 2) ? g_levelCalc.getRMSCh(1) : rmsL;
        float peakL = (chs >= 1) ? g_levelCalc.getPeakCh(0) : g_levelCalc.getPeak();
        float peakR = (chs >= 2) ? g_levelCalc.getPeakCh(1) : peakL;
        float lufsL = (chs >= 1) ? g_levelCalc.getLUFSMomentaryCh(0) : g_levelCalc.getLUFSMomentary();
        float lufsR = (chs >= 2) ? g_levelCalc.getLUFSMomentaryCh(1) : lufsL;
        g_meterWidget->updateLevelsLR(rmsL, rmsR, peakL, peakR, lufsL, lufsR);
    });
    g_updateTimer->start(16); // ~60fps

    // 配信が使用しているトラック情報を1秒ごとに更新（設定ベース）
    g_streamInfoTimer = new QTimer();
    QObject::connect(g_streamInfoTimer, &QTimer::timeout, [](){
        uint32_t mask = get_streaming_mixers_from_settings();
        if (g_meterWidget) g_meterWidget->setStreamingTracksMask(mask);
    });
    g_streamInfoTimer->start(1000);

    blog(LOG_INFO, "Level Meter Plugin loaded");
    return true;
}

void obs_module_unload(void) {
    if (g_streamInfoTimer) {
        g_streamInfoTimer->stop();
        delete g_streamInfoTimer;
        g_streamInfoTimer = nullptr;
    }
    if (g_updateTimer) {
        g_updateTimer->stop();
        delete g_updateTimer;
        g_updateTimer = nullptr;
    }
    // audio_output_disconnectはg_audioが有効かつg_connectedの時のみ
    if (g_connected && g_audio) {
        // OBSのaudioサブシステムが既にシャットダウンしている場合は呼ばない
        if (obs_get_audio()) {
            audio_output_disconnect(g_audio, g_mixIdx, audio_callback, nullptr);
        }
        g_connected = false;
        g_audio = nullptr;
    }
    if (g_meterWidget) {
        // QWidgetのdeleteはUIスレッドでのみ行う
        if (g_meterWidget->isVisible())
            g_meterWidget->close();
        QSettings settings("ha_kondo", "level_meter_plugin");
        settings.setValue("window/geometry", g_meterWidget->saveGeometry());
        delete g_meterWidget;
        g_meterWidget = nullptr;
    }
}
