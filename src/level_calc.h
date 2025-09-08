#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <vector>
#include <deque>
#include <array>

/*
 * LevelCalc
 *  - Momentary (400ms) LUFS と各種瞬時指標 (RMS / Peak) のみ保持
 *  - Integrated LUFS は削除（以前の energies_ 蓄積とゲーティング処理無し）
 *  - getLUFS() は互換目的で momentary LUFS を返す
 */
class LevelCalc {
public:
    LevelCalc();
    ~LevelCalc();

    void setSampleRate(uint32_t sampleRate);
    void setChannels(size_t channels);

    void process(float **data, uint32_t frames, size_t channels);

    float getRMS() const;
    float getPeak() const;

    float getRMSCh(size_t ch) const;
    float getPeakCh(size_t ch) const;
    size_t getChannels() const { return channels_; }

    // (互換) Integrated 代替: momentary を返す
    float getLUFS() const;
    // Momentary (400ms)
    float getLUFSMomentary() const;
    float getLUFSMomentaryCh(size_t ch) const;
    // Short (3sec) LUFS
    float getLUFSShort() const;
    float getSmoothedLUFSShort() const;
    float getLUFSShortCh(size_t ch) const;

private:
    static constexpr size_t kMaxChannels = 8;

    void initFiltersIfNeeded(size_t channels);
    void resetBlockAccumulators();
    void updateFilterCoeffs();

    // 原始メトリクス
    std::atomic<float> rms_;
    std::atomic<float> peak_;
    std::array<std::atomic<float>, kMaxChannels> rms_ch_{};
    std::array<std::atomic<float>, kMaxChannels> peak_ch_{};

    // Momentary (400ms) LUFS（全体 & ch別）
    std::atomic<float> lufs_m_;
    std::array<std::atomic<float>, kMaxChannels> lufs_m_ch_{};

    // Short (3秒) LUFS
    std::atomic<float> lufs_short_;
    std::array<std::atomic<float>, kMaxChannels> lufs_short_ch_{};
    float smoothed_lufs_short_ = -120.0f;
    std::vector<std::deque<double>> recentSubblocksShort_;
    std::vector<double> rollingSubSumShort_;

    std::atomic<uint32_t> sampleRate_;
    size_t channels_ = 0;

    // K-weighting フィルタ
    struct FirstOrderHP {
        float b0 = 0.f, b1 = 0.f, a1 = 0.f;
        float x1 = 0.f, y1 = 0.f;
        void setCoeffs(float b0_, float b1_, float a1_) { b0 = b0_; b1 = b1_; a1 = a1_; }
        inline float process(float x) {
            float y = b0 * x + b1 * x1 - a1 * y1;
            x1 = x; y1 = y;
            return y;
        }
        void reset() { x1 = 0.f; y1 = 0.f; }
    };
    struct BiquadHS {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;
        inline float process(float x) {
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void reset() { x1 = x2 = y1 = y2 = 0.f; }
    };
    std::vector<BiquadHS> shelfFilters_;
    std::vector<FirstOrderHP> hp1Filters_;
    std::vector<FirstOrderHP> hp2Filters_;

    // 100ms hop / 400ms window
    uint32_t hopSamples_ = 0;
    uint32_t hopSampleCount_ = 0;
    uint32_t blockWindowSamples_ = 0;

    std::vector<double> sumSquaresHop_;                 // per-ch 100ms二乗和
    std::vector<std::deque<double>> recentSubblocks_;   // per-ch 直近4つ(100ms)平均MS
    std::vector<double> rollingSubSum_;                 // per-ch 合計
};
