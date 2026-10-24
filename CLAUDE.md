# ケーブル物理（VCV Rack 本体のフォーク）

VCV Rack 2 のパッチケーブルを、実機のパッチケーブルのように振る舞わせる改造。持ち主は自作モジュール集 **Understudy**（別リポジトリ `shirasawa01/understudy`）の作者。

- 仕様：`reference/CABLE_PHYSICS_SPEC.md`（チャットで作った原本。数値はここ）
- 動く正解：`reference/cable-bench.html`（ブラウザの試作）、`reference/shot.jpg`（その画面）。迷ったら試作に合わせる。
- 段取り・質問・決めたこと：understudy の `docs/cable_physics_plan.md`（**窓口はあちら**。決めたことはあちらの 7. に書く）

## ブランチ
- 作業は `cable-physics`。**タグ v2.6.6 から**切ってある（`v2` = v2.6.6 + 試作のアップロード 1 コミット）。
  Understudy は Rack SDK 2.6.6 でビルドしているので、同じ版ならプラグインをそのまま読める。上流の `v2` は取り込まない。
- 改造のコードはなるべく新しいファイル（`include/app/CablePhysics.hpp`・`src/app/CablePhysics.cpp` など）に置き、
  元のファイルの変更は呼び出す部分だけにする（上流の新しい版へ乗せ替えやすくするため）。
- 試作・仕様など上流に無いものは `reference/` と、この CLAUDE.md だけに置く。
- GPLv3。配布するなら「VCV」の名前とロゴは使わない（いまは自分で使うだけ）。

## クラウド（Linux）でのビルド
```
apt-get install -y libtool zstd libglu1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev libgtk-3-dev libjack-jackd2-dev libpulse-dev xvfb
git config submodule.dep/speexdsp.url https://github.com/xiph/speexdsp.git   # gitlab.xiph.org はクラウドから届かない
git submodule update --init --recursive --depth 1
make dep -j8 && make -j8
```
- 依存の取得に github.com・bitbucket.org・curl.se などが要る。ビルド済みの `dep/` は git に入らない（会話ごとにやり直し）。
- 確かめ：画面なしでパッチを開く（`./Rack -d -h <patch>`）、xvfb の上で起動して画面を撮る（試作の画面と見比べる）。
- Understudy を一緒に読ませるときは、understudy の `vcv/` を `make RACK_DIR=<Rack-SDK>` でビルドし、`dist/Understudy` を `plugins/` にコピー（開発モード `-d` は `plugins/` を見る）。

## 手元（Windows）
- ビルドと触っての確かめはユーザーが MSYS2 MINGW64 で手で打つ。コマンドはそのまま貼れる形で書く。
  手順は understudy の `docs/cable_physics_plan.md` の 3.（`C:\dev\Rack-src` に clone、`make run` で起動）。
- いつもの Rack Free・Rack SDK（`C:\dev\Rack-SDK`）はそのまま。改造版の設定とプラグインは `C:\dev\Rack-src` の中だけ。

## 使用量
- クラウドも Pro の使用量の枠で動く。区切り（段階）ごとに新しい会話にし、ログ・画像を読む量を控える。
- `cable-bench.html` は約 50KB。毎回全部は読まない：一度読んだら、数値と処理の要点を `reference/bench_notes.md` に書いておき、次からはそれを見る。
