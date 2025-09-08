#include "meter_widget.h"
#include <QPainter>
#include <QDateTime>
#include <QStyleOption>
#include <QLinearGradient>
#include <QSettings>
#include <QCloseEvent>
#include <cmath>
#include <QPushButton>
#include <QButtonGroup>
#include <QLabel>

MeterWidget::MeterWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(minimumSizeHint());
    setAutoFillBackground(true);

    // 上部: Track1..Track6 ボタン（Mix 0..5）
    btnGroup_ = new QButtonGroup(this);
    btnGroup_->setExclusive(true);

    for (int i = 0; i < kButtonCount; ++i) {
        auto *btn = new QPushButton(QString("Track%1").arg(i + 1), this);
        btn->setCheckable(true);
        // 暗転スタイル（選択時）
        btn->setStyleSheet(
            "QPushButton { background-color: #3a3a3a; color: #f0f0f0; border: 1px solid #5a5a5a; padding: 4px 10px; }"
            "QPushButton:hover { background-color: #474747; }"
            "QPushButton:checked { background-color: #222; color: #dcdcdc; border-color: #777; }"
        );
        trackBtns_[i] = btn;
        // ボタンID = 対応する Mix インデックス（0..5）
        btnGroup_->addButton(btn, i);
    }

    // クリックでミックスインデックス（0..5）を通知
    connect(btnGroup_, &QButtonGroup::idClicked, this, [this](int id){
        emit mixIndexChanged(id);
    });

    // 配信使用トラック表示ラベル
    streamingInfoLabel_ = new QLabel(this);
    streamingInfoLabel_->setText("Streaming uses: —");
    streamingInfoLabel_->setStyleSheet("color: #ddd; background: transparent;");

    // 初期のトップバー高さ（ボタン＋ラベル）
    QFontMetrics fm(font());
    int btnH = fm.height() + 12;
    int infoH = fm.height();
    topBarHeightPx_ = btnH + 4 + infoH;
}

QSize MeterWidget::minimumSizeHint() const {
    const int margin = 10;
    const int spacing = 14; // 行間

    QFontMetrics fm(this->font());
    int rowTitleH = fm.height();
    int scaleH = fm.height() + 6; // 数字＋余白

    // バー領域の安全最小高さ
    int barMinH = 16;
    int rowMinH = rowTitleH + 2 + barMinH + 1 + scaleH;
    int areaH = 3 * rowMinH + 2 * spacing;

    // トップバー（ボタン＋情報）高さ・幅
    int btnH = fm.height() + 12;
    int infoH = fm.height();
    int btnW = fm.horizontalAdvance("Track10") + 22; // 余裕込み
    int gap = 8;
    int topBarW = kButtonCount * btnW + (kButtonCount - 1) * gap;

    int totalH = (btnH + 4 + infoH) + 6 + areaH + 2 * margin;

    // 追加の最小高さマージン（約30px）
    totalH += 30;

    // 横幅: ボタン列とスケール幅のどちらか広い方
    int labelW = fm.horizontalAdvance("-60");
    int ticks = 13;
    int totalLabelsW = ticks * (labelW + 6);
    int chLabelColW = fm.horizontalAdvance("R") + 10;
    int baseW = std::max(chLabelColW + totalLabelsW, topBarW);
    int totalW = std::max(420, baseW + 2 * margin);

    return QSize(totalW, totalH);
}

float MeterWidget::linToDb(float x) const {
    const float eps = 1e-9f;
    float v = x < eps ? eps : x;
    return 20.0f * std::log10(v);
}

float MeterWidget::clampDb(float db) const {
    if (db < dbFloor_) return dbFloor_;
    if (db > dbCeil_) return dbCeil_;
    return db;
}

float MeterWidget::clampDbToRange(float db, float floor, float ceil) const {
    if (db < floor) return floor;
    if (db > ceil) return ceil;
    return db;
}

int MeterWidget::dbToPx(float db, int widthPx) const {
    float clamped = clampDb(db);
    float t = (clamped - dbFloor_) / (dbCeil_ - dbFloor_);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return static_cast<int>(std::round(t * widthPx));
}

int MeterWidget::lufsToPx(float lufs, int widthPx) const {
    float clamped = clampDbToRange(lufs, lufsFloor_, lufsCeil_);
    float t = (clamped - lufsFloor_) / (lufsCeil_ - lufsFloor_);
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    return static_cast<int>(std::round(t * widthPx));
}

void MeterWidget::updateLevels(float rms, float peak, float lufs) {
    updateLevelsLR(rms, rms, peak, peak, lufs, lufs);
}

void MeterWidget::updateLevelsLR(float rmsL, float rmsR, float peakL, float peakR, float lufsL, float lufsR) {
    // 入力をdBに
    float newRmsDbL = clampDb(linToDb(rmsL));
    float newRmsDbR = clampDb(linToDb(rmsR));
    float newPeakDbL = clampDb(linToDb(peakL));
    float newPeakDbR = clampDb(linToDb(peakR));
    // LUFSは専用スケールでクランプ（-45..0 LUFS）
    lufsDbL_ = clampDbToRange(lufsL, lufsFloor_, lufsCeil_);
    lufsDbR_ = clampDbToRange(lufsR, lufsFloor_, lufsCeil_);

    // 時間差分q
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    float dt = 0.0f;
    if (lastUpdateMs_ != 0) dt = static_cast<float>(now - lastUpdateMs_) / 1000.0f;
    lastUpdateMs_ = now;
    if (dt <= 0.0f) dt = 0.05f; // フォールバック

    // RMSスムージング（L/R、攻撃/復帰時定数の一次IIR）
    if (!std::isfinite(rmsSmoothDbL_)) rmsSmoothDbL_ = -120.0f;
    if (!std::isfinite(rmsSmoothDbR_)) rmsSmoothDbR_ = -120.0f;
    if (!std::isfinite(newRmsDbL)) newRmsDbL = -120.0f;
    if (!std::isfinite(newRmsDbR)) newRmsDbR = -120.0f;

    float tauL = (newRmsDbL > rmsSmoothDbL_) ? rmsAttackSec_ : rmsReleaseSec_;
    float alphaL = 1.0f - std::exp(-dt / std::max(0.001f, tauL));
    rmsSmoothDbL_ = rmsSmoothDbL_ + alphaL * (newRmsDbL - rmsSmoothDbL_);
    rmsDbL_ = newRmsDbL; // 瞬時RMSも保持

    float tauR = (newRmsDbR > rmsSmoothDbR_) ? rmsAttackSec_ : rmsReleaseSec_;
    float alphaR = 1.0f - std::exp(-dt / std::max(0.001f, tauR));
    rmsSmoothDbR_ = rmsSmoothDbR_ + alphaR * (newRmsDbR - rmsSmoothDbR_);
    rmsDbR_ = newRmsDbR;

    // Peakスムージング（L/R）+ 瞬時値保持
    if (!std::isfinite(peakSmoothDbL_)) peakSmoothDbL_ = -120.0f;
    if (!std::isfinite(peakSmoothDbR_)) peakSmoothDbR_ = -120.0f;

    float tauPL = (newPeakDbL > peakSmoothDbL_) ? peakAttackSec_ : peakReleaseSec_;
    float aPL = 1.0f - std::exp(-dt / std::max(0.001f, tauPL));
    peakSmoothDbL_ = peakSmoothDbL_ + aPL * (newPeakDbL - peakSmoothDbL_);
    peakDbL_ = newPeakDbL;

    float tauPR = (newPeakDbR > peakSmoothDbR_) ? peakAttackSec_ : peakReleaseSec_;
    float aPR = 1.0f - std::exp(-dt / std::max(0.001f, tauPR));
    peakSmoothDbR_ = peakSmoothDbR_ + aPR * (newPeakDbR - peakSmoothDbR_);
    peakDbR_ = newPeakDbR;

    // ピークホールド: L/R（瞬時値ベース）
    if (peakDbL_ > peakHoldDbL_ + 0.1f) {
        peakHoldDbL_ = peakDbL_;
        peakHoldLastRiseMsL_ = now;
    } else if (peakHoldLastRiseMsL_ != 0) {
        float heldSec = static_cast<float>(now - peakHoldLastRiseMsL_) / 1000.0f;
        if (heldSec > peakHoldTimeSec_) {
            peakHoldDbL_ -= peakFallDbPerSec_ * dt;
            if (peakHoldDbL_ < dbFloor_) peakHoldDbL_ = dbFloor_;
        }
    }

    if (peakDbR_ > peakHoldDbR_ + 0.1f) {
        peakHoldDbR_ = peakDbR_;
        peakHoldLastRiseMsR_ = now;
    } else if (peakHoldLastRiseMsR_ != 0) {
        float heldSec = static_cast<float>(now - peakHoldLastRiseMsR_) / 1000.0f;
        if (heldSec > peakHoldTimeSec_) {
            peakHoldDbR_ -= peakFallDbPerSec_ * dt;
            if (peakHoldDbR_ < dbFloor_) peakHoldDbR_ = dbFloor_;
        }
    }

    update();
}

static QColor zoneColorLow() { return QColor(60, 200, 80); }   // green
static QColor zoneColorMid() { return QColor(230, 200, 60); }  // yellow
static QColor zoneColorHigh(){ return QColor(230, 40, 50); }   // red

void MeterWidget::drawDbScale(QPainter &p, const QRect &r) const {
    p.save();
    Q_UNUSED(r);
    // 背景の縦スケールは非表示
    p.restore();
}

// 下端目盛り（dBFS、5 dB刻み、数字あり）
void MeterWidget::drawBottomTicksDb(QPainter &p, const QRect &r) const {
    p.save();
    int yBase = r.top();
    QColor minor(120,120,120,140);
    QColor major(60,60,60,200);
    // 目盛り数字用にフォントを100%へ縮小
    QFont tickFont = p.font();
    if (tickFont.pointSizeF() > 0) {
        tickFont.setPointSizeF(tickFont.pointSizeF() * 1);
    } else if (tickFont.pixelSize() > 0) {
        tickFont.setPixelSize(static_cast<int>(std::round(tickFont.pixelSize() * 0.8)));
    } else {
        tickFont.setPointSize(10); // フォールバック（環境依存のデフォルトを軽く小さめに）
    }
    p.setFont(tickFont);

    QFontMetrics fm(p.fontMetrics());
    int th = fm.height();

    for (int d = -60; d <= 0; d += 5) {
        int x = r.left() + dbToPx(static_cast<float>(d), r.width());
        bool isMajor = (d % 10 == 0);
        // 目盛り線色は従来どおり
        p.setPen(isMajor ? major : minor);
        int tickH = isMajor ? 8 : 5;
        p.drawLine(QPoint(x, yBase), QPoint(x, yBase + tickH));
        // 数字は白（縮小フォント）
        QString label = QString::number(d);
        int w = fm.horizontalAdvance(label);
        QRect tr(x - w/2 - 2, r.bottom() - th + 1, w + 4, th);
        p.setPen(Qt::white);
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
    p.restore();
}

// 下端目盛り（LUFS、5 LU刻み、数字あり、-23/-18LUFSは強調）
void MeterWidget::drawBottomTicksLUFS(QPainter &p, const QRect &r) const {
    p.save();
    int yBase = r.top();
    int start = static_cast<int>(std::ceil(lufsFloor_ / 5.0f) * 5); // -45 → -45
    int end = static_cast<int>(std::floor(lufsCeil_ / 5.0f) * 5);   // 0 → 0
    QColor minor(120,120,120,140);
    QColor major(60,60,60,200);
    QColor target(230, 80, 80, 220); // -23 LUFS 強調（線）

    // 目盛り数字用にフォントを80%へ縮小
    QFont tickFont = p.font();
    if (tickFont.pointSizeF() > 0) {
        tickFont.setPointSizeF(tickFont.pointSizeF() * 0.8);
    } else if (tickFont.pixelSize() > 0) {
        tickFont.setPixelSize(static_cast<int>(std::round(tickFont.pixelSize() * 0.8)));
    } else {
        tickFont.setPointSize(10);
    }
    p.setFont(tickFont);

    QFontMetrics fm(p.fontMetrics());
    int th = fm.height();
    const int minorTickH = 5; // 5刻みの目盛り長さ

    for (int v = start; v <= end; v += 5) {
        int x = r.left() + lufsToPx(static_cast<float>(v), r.width());
        bool isMajor = (v % 10 == 0);
        p.setPen(isMajor ? major : minor);
        int tickH = isMajor ? 8 : minorTickH;
        p.drawLine(QPoint(x, yBase), QPoint(x, yBase + tickH));
        QString label = QString::number(v);
        int w = fm.horizontalAdvance(label);
        QRect tr(x - w/2 - 2, r.bottom() - th + 1, w + 4, th);
        p.setPen(Qt::white);
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
    // -23 LUFS の強調（スケール内にある場合）：線長さは5刻みの目盛りと同じ、ラベルは赤
    if (lufsFloor_ <= -23.0f && -23.0f <= lufsCeil_) {
        int x = r.left() + lufsToPx(-23.0f, r.width());
        p.setPen(QPen(target, 2));
        p.drawLine(QPoint(x, yBase), QPoint(x, yBase + minorTickH));
        // ラベルを赤で上書き
        QString label = QString::number(-23);
        int w = fm.horizontalAdvance(label);
        QRect tr(x - w/2 - 2, r.bottom() - th + 1, w + 4, th);
        QColor red = zoneColorHigh();
        p.setPen(red);
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
    // -18 LUFS の強調（スケール内にある場合）：線長さは5刻みの目盛りと同じ、ラベルはオレンジ
    if (lufsFloor_ <= -18.0f && -18.0f <= lufsCeil_) {
        int x = r.left() + lufsToPx(-18.0f, r.width());
        QColor target18(255, 160, 40, 220); // orange line
        p.setPen(QPen(target18, 2));
        p.drawLine(QPoint(x, yBase), QPoint(x, yBase + minorTickH));
        // ラベルをオレンジで上書き
        QString label = QString::number(-18);
        int w = fm.horizontalAdvance(label);
        QRect tr(x - w/2 - 2, r.bottom() - th + 1, w + 4, th);
        QColor orange(255, 160, 40);
        p.setPen(orange);
        p.drawText(tr, Qt::AlignHCenter | Qt::AlignVCenter, label);
    }

    p.restore();
}

void MeterWidget::drawBgZones(QPainter &p, const QRect &r) const {
    // BGの薄い色帯（緑 | 黄(-20dBまで) | 赤(-8dBまで)）
    p.save();
    // 枠背景
    p.setPen(QColor(60, 60, 60));
    p.setBrush(QColor(35, 35, 35));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    int x20 = r.left() + dbToPx(-20.f, r.width());
    int x8  = r.left() + dbToPx(-8.f,  r.width());

    QRect greenRect(r.left(), r.top(), std::max(0, x20 - r.left()), r.height());
    QRect yellowRect(x20, r.top(), std::max(0, x8 - x20), r.height());
    QRect redRect(x8, r.top(), std::max(0, r.right() - x8 + 1), r.height());

    QColor g = zoneColorLow(); g.setAlpha(60);
    QColor y = zoneColorMid(); y.setAlpha(60);
    QColor rcol = zoneColorHigh(); rcol.setAlpha(60);

    p.fillRect(greenRect, g);
    p.fillRect(yellowRect, y);
    p.fillRect(redRect, rcol);
    p.restore();
}

void MeterWidget::drawLevelBar(QPainter &p, const QRect &r, float dbValue) const {
    p.save();

    // 背景 + 色帯（薄色）
    drawBgZones(p, r);

    // しきい値（-20dB, -8dB）位置
    int x20 = r.left() + dbToPx(-20.f, r.width());
    int x8  = r.left() + dbToPx(-8.f,  r.width());

    // 現在値の描画終端
    int xVal = r.left() + dbToPx(dbValue, r.width());

    // 緑帯の実塗り: [left, min(x20, xVal))
    if (xVal > r.left()) {
        int gRight = std::min(xVal, x20);
        if (gRight > r.left()) {
            QRect gRect(r.left(), r.top(), gRight - r.left(), r.height());
            p.fillRect(gRect, zoneColorLow());
        }
        // 黄帯の実塗り: [x20, min(x8, xVal))
        if (xVal > x20) {
            int yRight = std::min(xVal, x8);
            if (yRight > x20) {
                QRect yRect(x20, r.top(), yRight - x20, r.height());
                p.fillRect(yRect, zoneColorMid());
            }
        }
        // 赤帯の実塗り: [x8, xVal)
        if (xVal > x8) {
            QRect rRect(x8, r.top(), xVal - x8, r.height());
            p.fillRect(rRect, zoneColorHigh());
        }
    }

    p.restore();
}

void MeterWidget::drawPeakMarker(QPainter &p, const QRect &r, float dbValue) const {
    p.save();
    int x = r.left() + dbToPx(dbValue, r.width());
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(x, r.top()), QPoint(x, r.bottom()));
    p.restore();
}

// LUFS バー（-45..0 LUFS スケールで描画）
void MeterWidget::drawLufsBar(QPainter &p, const QRect &r, float lufsDb) const {
    p.save();

    // 枠背景
    p.setPen(QColor(60, 60, 60));
    p.setBrush(QColor(35, 35, 35));
    p.drawRect(r.adjusted(0, 0, -1, -1));

    // 背景ゾーン（緑:-inf..-18, 黄:-18..-14, 赤:-14..0）を LUFS スケールで配置
    int x18 = r.left() + lufsToPx(-18.f, r.width());
    int x14 = r.left() + lufsToPx(-14.f, r.width());

    QRect greenRect(r.left(), r.top(), std::max(0, x18 - r.left()), r.height());
    QRect yellowRect(x18, r.top(), std::max(0, x14 - x18), r.height());
    QRect redRect(x14, r.top(), std::max(0, r.right() - x14 + 1), r.height());

    QColor g = zoneColorLow(); g.setAlpha(60);
    QColor y = zoneColorMid(); y.setAlpha(60);
    QColor rcol = zoneColorHigh(); rcol.setAlpha(60);

    p.fillRect(greenRect, g);
    p.fillRect(yellowRect, y);
    p.fillRect(redRect, rcol);

    // 現在値の描画終端（LUFSスケール）
    int xVal = r.left() + lufsToPx(lufsDb, r.width());

    if (xVal > r.left()) {
        int gRight = std::min(xVal, x18);
        if (gRight > r.left()) {
            QRect gRect(r.left(), r.top(), gRight - r.left(), r.height());
            p.fillRect(gRect, zoneColorLow());
        }
        if (xVal > x18) {
            int yRight = std::min(xVal, x14);
            if (yRight > x18) {
                QRect yRect(x18, r.top(), yRight - x18, r.height());
                p.fillRect(yRect, zoneColorMid());
            }
        }
        if (xVal > x14) {
            QRect rr(x14, r.top(), xVal - x14, r.height());
            p.fillRect(rr, zoneColorHigh());
        }
    }

    p.restore();
}

void MeterWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    p.setRenderHint(QPainter::Antialiasing, false);

    // レイアウト寸法
    const int margin = 10;
    const int spacing = 14; // 行間
    const int laneSpacing = 2; // L/Rの間隔

    // トップバー高さ（ボタン＋情報ラベル）を計算
    QFontMetrics fmTop(font());
    int btnH = fmTop.height() + 12;
    int infoH = fmTop.height();
    int calcTopBarH = btnH + 4 + infoH;
    if (topBarHeightPx_ != calcTopBarH) topBarHeightPx_ = calcTopBarH;

    QRect area = rect().adjusted(margin, margin + topBarHeightPx_ + 6, -margin, -margin);

    // 左側に L/R ラベル用の列を確保
    QFontMetrics fm(p.font());
    int chLabelColW = fm.horizontalAdvance("R") + 10;
    int rowTitleH = fm.height();
    int scaleH = fm.height() + 6; // 下部スケール高さ（数字込み）

    int rowH = (area.height() - 2 * spacing);
    rowH = rowH / 3; // 3行
    if (rowH < (rowTitleH + scaleH + 22)) rowH = (rowTitleH + scaleH + 22);

    QRect row1(area.left(), area.top(), area.width(), rowH);
    QRect row2(area.left(), row1.bottom() + spacing, area.width(), rowH);
    QRect row3(area.left(), row2.bottom() + spacing, area.width(), rowH);

    auto makeRowRects = [&](const QRect &row) {
        // タイトル行
        QRect titleRect(row.left(), row.top(), row.width(), rowTitleH);
        // バーの全体枠（行全体）: 左にL/R列、下にスケールを除く
        int barTop = titleRect.bottom() + 2;
        int barHeight = row.bottom() - scaleH - barTop; // 下部スケールの分だけ余白を確保
        if (barHeight < 16) barHeight = 16;
        QRect barFrame(row.left() + chLabelColW, barTop, row.width() - chLabelColW, barHeight);
        // L/R の各バー領域（枠内を上下二分）
        int halfH = (barFrame.height() - laneSpacing) / 2;
        if (halfH < 8) halfH = 8;
        QRect lBar(barFrame.left()+1, barFrame.top()+1, barFrame.width()-2, halfH-1);
        int rTop = lBar.bottom() + 1 + laneSpacing;
        if (rTop + halfH > barFrame.bottom()-1) rTop = barFrame.bottom()-1 - halfH;
        QRect rBar(barFrame.left()+1, rTop, barFrame.width()-2, halfH-1);
        // L/R ラベル列
        QRect lLbl(row.left(), lBar.top(), chLabelColW-4, lBar.height());
        QRect rLbl(row.left(), rBar.top(), chLabelColW-4, rBar.height());
        return std::tuple<QRect,QRect,QRect,QRect,QRect,QRect>(titleRect, barFrame, lBar, rBar, lLbl, rLbl);
    };

    auto [r1Title, r1Frame, r1LBar, r1RBar, r1LLbl, r1RLbl] = makeRowRects(row1);
    auto [r2Title, r2Frame, r2LBar, r2RBar, r2LLbl, r2RLbl] = makeRowRects(row2);
    auto [r3Title, r3Frame, r3LBar, r3RBar, r3LLbl, r3RLbl] = makeRowRects(row3);

    // バー塗りの関数（背景は行単位で描画）
    auto drawLevelFill = [&](QPainter &pp, const QRect &rr, float dbValue) {
        int x20 = rr.left() + dbToPx(-20.f, rr.width());
        int x8  = rr.left() + dbToPx(-8.f,  rr.width());
        int xVal = rr.left() + dbToPx(dbValue, rr.width());
        if (xVal > rr.left()) {
            int gRight = std::min(xVal, x20);
            if (gRight > rr.left()) pp.fillRect(QRect(rr.left(), rr.top(), gRight - rr.left(), rr.height()), zoneColorLow());
            if (xVal > x20) {
                int yRight = std::min(xVal, x8);
                if (yRight > x20) pp.fillRect(QRect(x20, rr.top(), yRight - x20, rr.height()), zoneColorMid());
            }
            if (xVal > x8) {
                pp.fillRect(QRect(x8, rr.top(), xVal - x8, rr.height()), zoneColorHigh());
            }
        }
    };

    // 行ごとに背景+塗り+ホールド線+目盛り+ラベル
    // 1) RMS
    drawBgZones(p, r1Frame);              // 行枠+背景ゾーン
    drawLevelFill(p, r1LBar, rmsSmoothDbL_);
    drawLevelFill(p, r1RBar, rmsSmoothDbR_);
    // 中央の仕切り線
    p.save(); p.setPen(QColor(60,60,60));
    p.drawLine(QPoint(r1Frame.left()+1, r1LBar.bottom()+1), QPoint(r1Frame.right()-1, r1LBar.bottom()+1));
    p.restore();
    // 下端スケール（dB）
    drawBottomTicksDb(p, QRect(r1Frame.left(), r1Frame.bottom()+1, r1Frame.width(), (row1.bottom()-r1Frame.bottom())));

    // 2) Peak
    drawBgZones(p, r2Frame);
    drawLevelFill(p, r2LBar, peakSmoothDbL_);
    drawLevelFill(p, r2RBar, peakSmoothDbR_);
    // ホールド線
    p.save();
    p.setPen(QPen(QColor(255,255,255,160), 2));
    int xHoldL2 = r2LBar.left() + dbToPx(peakHoldDbL_, r2LBar.width());
    int xHoldR2 = r2RBar.left() + dbToPx(peakHoldDbR_, r2RBar.width());
    p.drawLine(QPoint(xHoldL2, r2LBar.top()), QPoint(xHoldL2, r2LBar.bottom()));
    p.drawLine(QPoint(xHoldR2, r2RBar.top()), QPoint(xHoldR2, r2RBar.bottom()));
    p.restore();
    // 仕切り線
    p.save(); p.setPen(QColor(60,60,60));
    p.drawLine(QPoint(r2Frame.left()+1, r2LBar.bottom()+1), QPoint(r2Frame.right()-1, r2LBar.bottom()+1));
    p.restore();
    // 下端スケール（dB）
    drawBottomTicksDb(p, QRect(r2Frame.left(), r2Frame.bottom()+1, r2Frame.width(), (row2.bottom()-r2Frame.bottom())));

    // 3) LUFS
    // LUFSは専用スケールで背景と塗りを行う
    drawLufsBar(p, r3LBar, lufsDbL_);
    drawLufsBar(p, r3RBar, lufsDbR_);
    // 中央の仕切り線（従来どおり）
    p.save(); p.setPen(QColor(60,60,60));
    p.drawLine(QPoint(r3Frame.left()+1, r3LBar.bottom()+1), QPoint(r3Frame.right()-1, r3LBar.bottom()+1));
    p.restore();
    // 下端スケール（LUFS）
    drawBottomTicksLUFS(p, QRect(r3Frame.left(), r3Frame.bottom()+1, r3Frame.width(), (row3.bottom()-r3Frame.bottom())));

    // テキスト類（色は白）
    p.setPen(Qt::white);
    p.drawText(r1Title.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, "RMS");
    p.drawText(r2Title.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, "Peak");
    p.drawText(r3Title.adjusted(2, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, "LUFS");

    // L/R ラベル（白）: フォントを80%に縮小して描画
    p.save();
    QFont lrFont = p.font();
    if (lrFont.pointSizeF() > 0) {
        lrFont.setPointSizeF(lrFont.pointSizeF() * 0.8);
    } else if (lrFont.pixelSize() > 0) {
        lrFont.setPixelSize(static_cast<int>(std::round(lrFont.pixelSize() * 0.8)));
    } else {
        lrFont.setPointSize(10);
    }
    p.setFont(lrFont);
    p.drawText(r1LLbl, Qt::AlignLeft | Qt::AlignVCenter, "L");
    p.drawText(r1RLbl, Qt::AlignLeft | Qt::AlignVCenter, "R");
    p.drawText(r2LLbl, Qt::AlignLeft | Qt::AlignVCenter, "L");
    p.drawText(r2RLbl, Qt::AlignLeft | Qt::AlignVCenter, "R");
    p.drawText(r3LLbl, Qt::AlignLeft | Qt::AlignVCenter, "L");
    p.drawText(r3RLbl, Qt::AlignLeft | Qt::AlignVCenter, "R");
    p.restore();
}

void MeterWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    // トップバーのボタン配置（横一列）と配信情報ラベル
    const int margin = 10;
    QFontMetrics fm(font());
    int btnH = fm.height() + 12;
    int btnW = fm.horizontalAdvance("Track10") + 22;
    int gap = 8;

    int y = margin;
    int x = margin;
    for (int i = 0; i < kButtonCount; ++i) {
        if (trackBtns_[i]) trackBtns_[i]->setGeometry(x, y, btnW, btnH);
        x += btnW + gap;
    }

    int infoH = fm.height();
    if (streamingInfoLabel_) streamingInfoLabel_->setGeometry(margin, y + btnH + 4, width() - 2*margin, infoH);

    topBarHeightPx_ = btnH + 4 + infoH;

    // 最小サイズを再適用
    setMinimumSize(minimumSizeHint());

    QSettings settings("ha_kondo", "level_meter_plugin");
    settings.setValue("window/geometry", saveGeometry());
}

void MeterWidget::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    QSettings settings("ha_kondo", "level_meter_plugin");
    settings.setValue("window/geometry", saveGeometry());
}

void MeterWidget::closeEvent(QCloseEvent *event) {
    QSettings settings("ha_kondo", "level_meter_plugin");
    settings.setValue("window/geometry", saveGeometry());
    event->ignore();
    this->hide();
}

void MeterWidget::setMixIndex(int index) {
    if (index < 0) index = 0; if (index > 5) index = 5;
    int btnIdx = index;
    if (!btnGroup_) return;
    bool blocked = btnGroup_->blockSignals(true);
    if (btnIdx < kButtonCount && trackBtns_[btnIdx]) trackBtns_[btnIdx]->setChecked(true);
    btnGroup_->blockSignals(blocked);
}

void MeterWidget::setStreamingTracksMask(uint32_t mask) {
    if (!streamingInfoLabel_) return;
    QStringList parts;
    for (int i = 0; i < kButtonCount; ++i) {
        if (mask & (1u << i)) parts << QString("Track%1").arg(i + 1);
    }
    QString text;
    if (parts.isEmpty()) text = "Streaming uses: none";
    else text = QString("Streaming uses: %1").arg(parts.join(", "));
    streamingInfoLabel_->setText(text);
}
