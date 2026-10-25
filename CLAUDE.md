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
apt-get install -y libtool zstd libglu1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libasound2-dev libgtk-3-dev libjack-jackd2-dev libpulse-dev xvfb imagemagick
git fetch --depth 5 origin v2 && git fetch --depth 1 https://github.com/VCVRack/Rack.git tag v2.6.6
git config submodule.dep/speexdsp.url https://github.com/xiph/speexdsp.git   # gitlab.xiph.org はクラウドから届かない
git submodule update --init --recursive --depth 1
git submodule foreach --quiet --recursive 'git reset -q --hard'   # 取得が途中で失敗すると中身が空のまま残るので戻す
make dep -j8 && make -j8
```
- **タグ v2.6.6 が要る**：Rack は版番号を `git describe --tags` で決める。タグが無いと版が空になり、Core モジュールすら読めない。
  フォークにはタグが無い（クラウドからはタグを push できなかった）ので、clone したら上流から取る（上の 2 行目）。v2.6.6 から 2 コミット先までの履歴も要る。
- 依存の取得に github.com・bitbucket.org・curl.se などが要る。ビルド済みの `dep/` は git に入らない（会話ごとにやり直し、20 分ほど）。
- 2026-10-24：改造前の状態で、ビルド・画面なしの起動・xvfb での撮影まで通った。
- 2026-10-24：段階 B〜F を入れた。物理は `src/app/CablePhysics.cpp`（Rack に依存しない）、Rack につなぐ部分と描画は `src/app/CableRig.cpp`。
  要点と置き場所は `reference/bench_notes.md`（試作を読み直す前にこちら）。
- **物理を変えたら** `python3 reference/physics_check/check_physics.py`（試作と全点を比べる。差 0 のはず）。落ち着き方は `--settle`。
- 2026-10-24：曲げ・ケーブル同士の摩擦・止まったかの判定を **仕様から変えた**（実機の写真ほど張りが無かったため）。試作 HTML も同じに直した。
  何をどう変えたかは `reference/bench_notes.md` の「仕様から変えたこと」。仕様の原本（`CABLE_PHYSICS_SPEC.md`）はそのまま。
- 確かめ：画面なしでパッチを開く（`./Rack -d -h <patch>`）。画面は `sh reference/shot_rack.sh [patch] [out.png]`（xvfb の上で起動して撮る。
  設定・ログは `$WORK` に置き、リポジトリを汚さない）。試作の画面と見比べる。
- ケーブル確認用のパッチ：`reference/test_cables.vcv`（Core モジュールだけ。Fundamental なしの開発モードでも開ける。作り直しは `python3 reference/make_test_patch.py`）。
- 触って確かめる：`sh reference/drive_rack.sh`（xdotool でマウスを動かしてから撮る。使い方は先頭のコメント）。
- Understudy を一緒に読ませるときは、understudy の `vcv/` をこの Rack に向けてビルド（`make -C <understudy>/vcv RACK_DIR=$PWD dist`）し、
  `PLUGIN=<understudy>/vcv/dist/Understudy sh reference/shot_rack.sh reference/test_understudy.vcv`（`-u` で起動するので `$WORK/plugins` に入れる。
  手で `-d` だけで起動するなら `plugins/`）。パッチの作り直しは `python3 reference/make_understudy_patch.py`。

## 手元（Windows）
- ビルドと触っての確かめはユーザーが MSYS2 MINGW64 で手で打つ。コマンドはそのまま貼れる形で書く。
  手順は understudy の `docs/cable_physics_plan.md` の 3.（`C:\dev\Rack-src` に clone、`make run` で起動）。
- いつもの Rack Free・Rack SDK（`C:\dev\Rack-SDK`）はそのまま。改造版の設定とプラグインは `C:\dev\Rack-src` の中だけ。
- 2026-10-25：手元の **M1 MacBook Air** でも動かす（ユーザーは Mac の初心者）。手順は understudy の `docs/cable_physics_plan.md` の 3b.（`~/dev/Rack-src`）。
  改造のコードに OS で分けた所は作らない（Windows・Mac・Linux で同じに動かす）。

## 使用量
- クラウドも Pro の使用量の枠で動く。区切り（段階）ごとに新しい会話にし、ログ・画像を読む量を控える。
- `cable-bench.html` は約 50KB。毎回全部は読まない：一度読んだら、数値と処理の要点を `reference/bench_notes.md` に書いておき、次からはそれを見る。
