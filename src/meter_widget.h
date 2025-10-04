#pragma once
#include <QWidget>
#include <QtGlobal>
#include <array>
#include <cstdint>

class QPainter;
class QRect;
class QResizeEvent;
class QMoveEvent;
class QCloseEvent;
class QPushButton;
class QButtonGroup;
class QLabel;
class QTimer;

class MeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit MeterWidget(QWidget *parent = nullptr);

public slots:
    void updateLevels(float rms, float peak, float lufs);
    void updateLevelsLR(float rmsL, float rmsR, float peakL, float peakR, float lufsL, float lufsR);
    void setMixIndex(int index);
    void setStreamingTracksMask(uint32_t mask);
    // LUFS目盛りのオフセットを動的に設定（ピクセル）
    void setLufsTickOffsets(int offset23Px, int offset18Px);
    void setLufsTickOffset23(int offset23Px);
    void setLufsTickOffset18(int offset18Px);
    // Display smoothing / throttling for numeric labels
    void setDisplaySmoothingAlpha(double alpha); // 0..1
    void setUiUpdateIntervalMs(int ms);
    void setDisplayThresholdDb(double db);
    void onUiUpdateTimer();

signals:
    void mixIndexChanged(int index);

public:
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // UI: 上部のTrackボタン（Track1..Track6 → Mix0..5）+ その下に配信使用トラックの情報ラベル
    static constexpr int kButtonCount = 6;
    std::array<QPushButton*, kButtonCount> trackBtns_{};
    QButtonGroup *btnGroup_ = nullptr;
    QLabel *streamingInfoLabel_ = nullptr;
    int topBarHeightPx_ = 0; // ボタン列 + 情報ラベルを含む高さ

    // グラフ用の内部状態（dBFSに換算して保持）
    float rmsDbL_ = -120.0f;
    float rmsDbR_ = -120.0f;
    float peakDbL_ = -120.0f;
    float peakDbR_ = -120.0f;
    float lufsDbL_ = -120.0f;
    float lufsDbR_ = -120.0f;
    // 追加: チャネルの和（合計）で表示するLUFS
    float lufsDbCombined_ = -120.0f;
    // LUFS 目盛りオフセット（ピクセル）: -23 と -18 の目盛り線を下にずらすための微調整値
    int lufsTickOffset23Px_ = 3; // デフォルト: 3px 下にオフセット
    int lufsTickOffset18Px_ = 4; // デフォルト: 4px 下にオフセット

    // UI 数値表示の平滑化／更新制御
    QTimer *uiUpdateTimer_ = nullptr;
    double displayRmsL_ = -120.0;
    double displayRmsR_ = -120.0;
    double displayPeakL_ = -120.0;
    double displayPeakR_ = -120.0;
    double displayLufs_ = -120.0;
    double displaySmoothingAlpha_ = 0.25; // EMA alpha
    double displayThresholdDb_ = 0.05;   // minimal dB change to trigger update
    int uiUpdateIntervalMs_ = 120;       // ms (~8Hz)

    // 表示用スムージング
    float rmsSmoothDbL_ = -120.0f;
    float rmsSmoothDbR_ = -120.0f;
    float peakSmoothDbL_ = -120.0f;
    float peakSmoothDbR_ = -120.0f;
    qint64 lastUpdateMs_ = 0;
    const float rmsAttackSec_ = 0.06f;   // 60ms
    const float rmsReleaseSec_ = 0.30f;  // 300ms
    const float peakAttackSec_ = 0.04f;  // 40ms（少し遅く）
    const float peakReleaseSec_ = 0.25f; // 250ms

    // ピークホールド（L/R）
    float peakHoldDbL_ = -120.0f;
    float peakHoldDbR_ = -120.0f;
    qint64 peakHoldLastRiseMsL_ = 0;
    qint64 peakHoldLastRiseMsR_ = 0;
    float peakHoldTimeSec_ = 1.0f;      // 1秒保持
    float peakFallDbPerSec_ = 8.0f;     // 少し遅く

    // スケール範囲（dBFS 共通）
    float dbFloor_ = -60.0f;
    float dbCeil_  = 0.0f;
    // LUFS 専用スケール（最適化: -45 .. 0 LUFS）
    float lufsFloor_ = -45.0f;
    float lufsCeil_  = 0.0f;

    // ユーティリティ
    float linToDb(float x) const;
    float clampDb(float db) const;
    float clampDbToRange(float db, float floor, float ceil) const;
    int dbToPx(float db, int widthPx) const;
    int lufsToPx(float lufs, int widthPx) const;
    void drawDbScale(QPainter &p, const QRect &r) const;
    void drawLevelBar(QPainter &p, const QRect &r, float dbValue) const;
    void drawPeakMarker(QPainter &p, const QRect &r, float dbValue) const;
    void drawLufsBar(QPainter &p, const QRect &r, float lufsDb) const;
    void drawBgZones(QPainter &p, const QRect &r) const;
    // 下端の控えめ目盛りのみ（数字なし）：RMS/Peak=5dB刻み、LUFS=5LU刻み
    void drawBottomTicksDb(QPainter &p, const QRect &r) const;
    void drawBottomTicksLUFS(QPainter &p, const QRect &r) const;
};
