# Master Level Meter (OBS Plugin)

音声レベル表示 (RMS / Peak / Momentary LUFS) を行う OBS Studio 用フローティング・ウィンドウ・プラグインです。

Master の任意トラック (Track1..Track6) のメーター表示と、配信設定で使用される音声マスタートラックの音量が見れるようになります。

---
## 機能
- Master Track1..Track6 を流れる信号の表示
- Dual-channel (L/R) メータ:
- RMS
- Peak
- Momentary LUFS (400ms ITU-R BS.1770 K-weighted 処理)
- K-weighting フィルタ:
- 二段ハイパス (60Hz) + High-shelf (+4 dB @ ~1.7 kHz) 実装
- -23 / -18 LUFS 強調目盛り
- フローティングツールウィンドウ (常に前面)
- Streaming 設定から現在利用されるトラックの表示更新
  
---
### Audio Flow
1. OBS の `audio_output_connect` 経由で planar float オーディオフレーム取得
2. `audio_callback` で選択 Mix のバッファを `LevelCalc::process()` へ
3. K-weighting & サブブロック (100ms hop / 400ms window) 処理
4. Qt UI (約 60fps タイマ) が `updateLevelsLR()` を呼び内部状態を更新 → 再描画

  

### Loudness (Momentary / Integrated)
- Momentary: 400ms ウィンドウ平均エネルギー (ch 合算) → -0.691 オフセット
- Integrated: EBU R128 二段ゲート (絶対 -70 LUFS, 相対 -10 LU) の単純実装

---
## Qt 6 Usage
- モジュール: Core / Gui / Widgets
- CMake: `find_package(Qt6 COMPONENTS Core Widgets REQUIRED)`
- AUTOMOC 有効化
- 動的リンク前提（LGPLv3 への準拠）
静的リンクはライセンス義務が増すため非推奨。
> NOTE: obs-deps 付属の Qt (ヘッダ/Config が省略される場合) を直接利用するより、開発環境では公式 Qt SDK または Homebrew (macOS: `brew install qt`) を推奨。
---
## 【開発者向け】ビルド方法 
-Cmake関連ファイルは[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) をベースにしています。

### Typical macOS Build
```
mkdir -p build && cd build
cmake -G "Xcode" ..
```
生成された.xcodeprojを開き
schemeをReleaseに変更してGUIでビルドしてください.
※デフォルトで生成されるDebugフォルダの.pluginは**動きません**

  

### Windows (PowerShell)!!!調整中!!!
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DQt6_DIR="C:/Qt/6.6.2/msvc2019_64/lib/cmake/Qt6" -DCMAKE_PREFIX_PATH="C:/obs-studio/build/install"
cmake --build build --config Release
cmake --install build --config Release
```
> Ensure `OBS::libobs` is discoverable (either installed to a prefix in `CMAKE_PREFIX_PATH` or specify `-DLIBOBS_ROOT=/path/to/obs/install` if template supports it).
---
## インストール方法
On macOS:
```
~/Library/Application Support/obs-studio/plugins/MasterLevelMeter.plugin/
```

On Windows:
```
C:\Program Files\obs-studio\obs-plugins\64bit\MasterLevelMeter.dll
```
未確認ですが、Streamlabs OBS でも同様の場所に配置すれば動作すると思われますが動作保証しません。
**該当場所にdll,pluginを配置後、OBSを再起動してください。**

---
## 使い方

- OBS メニュー: ツール → Show Master Level Meter (初回ロード時は自動表示)
- Track ボタン: 対象トラック切替 (Track1..Track6)
- Streaming uses は 1 秒周期で設定から反映。設定 > 出力 >　配信タブ > 音声トラックで設定しているもの。出力モードが「基本」になっていると"Track1"になります
- ウィンドウは閉じても hide (非表示) 扱い、再度メニューから復帰
---
## 備考
- 音声コールバックは最小限計算 (K-weighting + accumulations)
- 400ms window / 100ms hop → モーメンタリ計算コストは制御可能
- Atomic 変数で UI スレッドとのロックレス共有（OBS main/Qt GUI thread）
---

## License
This plugin: GPL-2.0-or-later
Links dynamically to Qt6 (LGPLv3). See `LICENSE`.
OBS Studio (libobs) is GPLv2 (or later – confirm upstream license text).
If you bundle Qt frameworks, include LGPLv3 text and allow replacement.

---
## Third-Party Notices (Summary)
| Component | License | Notes |
|-----------|---------|-------|
| OBS (libobs) | GPLv2 | Core streaming / audio API |
| Qt 6 (Core/Gui/Widgets) | LGPLv3 | Dynamic link only |
| (Transitive: FFmpeg, x264, etc.) | Mixed (LGPL/GPL) | Via OBS, not directly redistributed unless you package them |
---
## セキュリティ / プライバシー
- このプラグインはネットワークにアクセスしません。
- ウィンドウサイズと表示トラックを、OBSを閉じても保存できるように`QSettings` (ローカルのOSに依存した保存領域)のみを使用します。
---
## ロードマップ (やりたいこと・アイディア)
- EBU R128 Short-Term (3s)
- トゥルーピーク対応
- スキン・カラーテーマの対応
- ドック対応（これが難しいんだ）
- ストリーミングではなく録画時のマスタートラック表示（録画はトラックが一つだけとは限らないからUIどうするか）
---
## Q&A
- OBSのバージョンは？
    - OBS 29.0 以降を想定しています (Qt6 / C++17 必須)。

- macOSのバージョンは？
    - macOS 10.15 (Catalina) 以降を想定しています (Qt6 / C++17 必須)。
    - Intel Macはサポートしません。多分動くと思うけど。

- windowsのバージョンは？
    - Windows 10 以降を想定しています (Qt6 / C++17 必須)。

- OBS Studio以外でも動作しますか？
    - Streamlabs OBS など Qt6 ベースの OBS fork でも動作すると思われますが、未確認です。

- 他のプラグインと競合しませんか？

    - OBSのオーディオコールバックは複数登録可能なので、基本的に競合しませんが、競合するものがあれば連絡ください。

- CPU負荷はどのくらいですか？
    - macOS (M1) で 0.5% 以下、Windows (i7-9700K) で 1% 以下です。OBS全体の負荷に対しては微小です。

- **マスターエフェクト対応する予定ありますか？**
    - audio_output_connectのAPI仕様上、そこで加工しても元のパイプラインへ戻す仕組みがなく対応予定はありません。OBSの将来的なAPI拡張次第です。

## 謝辞
  
- OBS Project & contributors
- Qt Project
- ITU-R BS.1770 / EBU R128 specification references
---
## 免責
本ドキュメントおよびソフトウェアは現状有姿 (“AS IS”) で提供され、いかなる保証も行いません。
バイナリを広範に配布する場合などの法的適合性 (GPL / LGPL 等) については、専門の弁護士に相談してください。
本ソフトウェアのインストールまたは使用に起因して発生した損害、データ消失、動作不良その他一切の不利益について、責任を負いません。

---

  

---

# -English README-

  

# Master Level Meter (OBS Plugin)

  

A floating window plugin for OBS Studio that displays audio levels: RMS / Peak / Momentary LUFS for any Master track (Track1..Track6).

It also visualizes which audio tracks are currently selected in the streaming (Output) settings.
  
---  

## Features
- Visualize Master Track1..Track6 signal levels
- Dual-channel (L/R) meters:
- RMS
- Peak
- Momentary LUFS (400 ms ITU-R BS.1770 K-weighted)
- K-weighting filter:
- Two-stage high-pass (60 Hz) + high-shelf (+4 dB @ ~1.7 kHz)
- Emphasis ticks at -23 / -18 LUFS
- Floating tool window (always on top)
- Detects and shows which tracks are used for streaming (updates every 1 s)
---
## Audio Flow
1. Obtain planar float audio frames via `audio_output_connect`
2. The selected mix buffer is pushed to `LevelCalc::process()` inside `audio_callback`
3. K-weighting & sub-block processing (100 ms hop / 400 ms window)
4. A ~60 fps Qt timer calls `updateLevelsLR()` → triggers repaint

### Loudness (Momentary / Integrated)
- Momentary: 400 ms sliding window energy (summed channels) with -0.691 offset
- Integrated: EBU R128 dual-gate (absolute -70 LUFS, relative -10 LU) planned / extendable
---
## Qt 6 Usage
- Modules: Core / Gui / Widgets
- CMake: `find_package(Qt6 COMPONENTS Core Widgets REQUIRED)`
- AUTOMOC enabled
- Dynamic linking (LGPLv3 compliance). Static linking discouraged due to extra obligations
- Prefer official Qt SDK (or Homebrew `brew install qt` on macOS) over ad‑hoc stripped deps
---
## Build (Developer Notes)
CMake logic based on [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate)

### macOS (Xcode) Example
```bash
mkdir -p build && cd build
cmake -G "Xcode" ..
```
open the generated `.xcodeproj` in Xcode,  
set the scheme to **Release**,  
and build from Xcode GUI.
  
### Windows (PowerShell) (Work in Progress)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
-DQt6_DIR="C:/Qt/6.6.2/msvc2019_64/lib/cmake/Qt6" `
-DCMAKE_PREFIX_PATH="C:/obs-studio/build/install"
cmake --build build --config Release
cmake --install build --config Release
```
> Ensure `OBS::libobs` is discoverable (add its install prefix to `CMAKE_PREFIX_PATH` or use `-DLIBOBS_ROOT=...` if supported).
---

## Installation
macOS:
```
~/Library/Application Support/obs-studio/plugins/MasterLevelMeter.plugin/
```

Windows:
```
C:\Program Files\obs-studio\obs-plugins\64bit\MasterLevelMeter.dll
```
Restart OBS after placing the plugin.
(Other Qt6-based forks such as Streamlabs OBS may work, unverified.)

---
## Usage
- OBS menu: Tools → Show Master Level Meter (auto shows first load)
- Track buttons: choose track (Track1..Track6)
- “Streaming uses”: updated every second from Output → Streaming settings
- Shows "Track1" if Output Mode is “Simple”
- Closing the window hides it; reopen via menu
---
## Implementation Notes
- Audio callback: minimal K-weight + accumulations only
- 400 ms window / 100 ms hop keeps cost low
- Lock-free sharing between audio and UI threads via atomic variables
---
## License
Plugin: GPL-2.0-or-later
OBS Studio (libobs) is GPLv2 (only).
Links dynamically to Qt6 (LGPLv3). If you bundle Qt libraries, include the LGPLv3 text and allow user relinking.
See `LICENSE` for full texts.

---
## Third-Party Summary
| Component | License | Notes |
|-----------|---------|-------|
| OBS (libobs) | GPLv2 | Core streaming / audio API |
| Qt 6 (Core/Gui/Widgets) | LGPLv3 | Dynamic linkage |
| (Transitive: FFmpeg, x264, etc.) | Mixed (LGPL/GPL) | Via OBS; not directly redistributed here |

---

## Security / Privacy
- No network access.
- Persists only window geometry & selected track via QSettings (local OS-dependent storage).

---
## Roadmap (Ideas)
- EBU R128 Short-Term (3 s)
- True Peak
- Skins / color themes
- Dock integration
- Enhanced recording (non-stream) master track visualization
  
---
## Q&A
- Required OBS?
    - OBS 29.0+ (Qt6 / C++17)

- macOS version?
    - 10.15+ (Catalina or later)
    - *Intel Macs are not supported. It should work though.*

- Windows version?
    - Windows 10+

- Other forks?
    - Likely works with Qt6-based forks; untested

- Conflicts with other plugins?
    - Multiple audio callbacks are allowed; report issues if found

- CPU usage?
    - Approx: macOS (M1) < 0.5%, Windows (i7-9700K) < 1%

- **Master effects planned?**
    - Current API (`audio_output_connect`) cannot feed processed data back; depends on future OBS API changes.
---

## Acknowledgements
- OBS Project & contributors
- Qt Project
- ITU-R BS.1770 / EBU R128 specifications
---
## Disclaimer
This documentation and software are provided “AS IS” without any warranty (express or implied).
Consult an attorney for compliance questions (GPL / LGPL) if distributing binaries broadly.
I assume no liability for any damage, data loss, malfunction, loss of profit, or other issues arising from installing or using this software. 

---
(End)
