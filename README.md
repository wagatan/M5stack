# M5Stack CoreS3 Avatar - Smart Dark Mode & Battery Monitor

M5Stack CoreS3向けにカスタマイズされた、環境光センサー連動型のスマートなアバタープログラムです。
周囲が暗くなると自動で画面を暗くして眠りにつき、バッテリーが減ると悲しい顔で教えてくれる、より「ペットらしさ」を感じられる機能を追加しています。

## ✨ 主な機能 (Features)

* 🌙 **自動ダークモード＆おやすみ機能**
  CoreS3内蔵の環境光センサー（LTR-553ALS-WA）を使用し、部屋が暗くなると自動的に画面の輝度を下げ（ダークモード）、アバターが「Zzz...」と眠りにつきます。明るくなると自動で目を覚まします。
* 🔋 **バッテリー低下アラート**
  バッテリー残量が10%未満になると、「I need some energy...」というセリフと共に悲しい顔（涙）で充電を促します。
* 😊 **ご機嫌スマイル（フリーズなし）**
  定期的に「Oh!」と声を出すアクションの際、不自然な汗（Doubt）の代わりにニコッと笑う（Happy）ように改良。処理を停止させる `delay()` を使わずタイマーで管理しているため、笑顔の間もアニメーションが滑らかに動き続けます。
* 🔇 **ミュート機能**
  ボタン操作でいつでも簡単に音声のON/OFF（ミュート）を切り替えられます。
* 🎨 **表情バグの修正**
  複数の状態（怒り、睡眠、アクション）が重なった際に元の顔に戻らなくなるバグを修正し、安定した動作を実現しています。

## 📦 必要なハードウェア (Hardware Requirements)

* **M5Stack CoreS3** (または CoreS3 SE)
* **M5GO Bottom3** (マイク、スピーカー、バッテリー、LED用)

## 🛠 開発環境 (Environment)

* VSCode + **PlatformIO**
* 使用ライブラリ:
  * `m5stack/M5Unified`
  * `meganetaaan/M5Stack-Avatar`
  * `fastled/FastLED`

## 🎮 操作方法 (Usage)

* **BtnA (画面左下)**: アバターのカラーパレット（色）を変更します。ダブルクリックで画面の向きを回転します。
* **BtnB (画面中央下)**: 音声のミュート（ON/OFF）を切り替えます。
* **電源ボタン**: プログラムを再起動します。
* **センサーテスト**: 画面下部中央（カメラレンズの横あたり）を指でピタッと覆い隠すと、強制的にダークモード（おやすみ状態）をテストできます。

## 📄 クレジットとライセンス (Credits & License)

このプロジェクトは **MIT License** のもとで公開されています。
詳細は `LICENSE` ファイルをご確認ください。

* **Original Author**: [Takao Akaki](https://github.com/takao-akaki) (Copyright (c) 2022)
* **M5Stack-Avatar Library**: [meganetaaan](https://github.com/meganetaaan/m5stack-avatar)
* **Arranged by**: Tomoshi Wagata (2026) - *Added CoreS3 ALS dark mode, smart non-blocking expressions, and battery monitoring.*
