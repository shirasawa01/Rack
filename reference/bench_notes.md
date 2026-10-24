# 試作 cable-bench.html の要点と、Rack 側の置き場所

試作（約 50KB）を毎回読まないためのメモ。2026-10-24、段階 B〜F を入れたときに書いた。数値は仕様 `CABLE_PHYSICS_SPEC.md` と同じ。

## 物理（`src/app/CablePhysics.cpp`、Rack に依存しない）

| 試作 | C++ | メモ |
|---|---|---|
| `makeCable` | `makeCable()` | 初期の弧（上向きの輪／下向き）、二分法 60 回、クセ `mem*0.8` |
| `chooseLength` の自動 | `chooseLength()` | `距離*1.1 + 2*LB + 3` 以上の最短。届かない判定 `tooShort()` は `距離 + 2*LB + 1.5` |
| `rebuildObstacles` | `World::rebuildObstacles()` | `parts`（ノブ等）→ ブーツの順。格子 2cm |
| `step` | `World::step()` | サブ 2・反復 16、4 反復ごとに ケーブル同士 → 静的。最後にクセ（`plast`） |
| `frame` の中身 | `World::frame()` | ドラッグ中 1 ステップ、ほかは 4。`still > 12` か 100 フレームで固定 |
| `wake` | `World::wake()` | 寝ていたら速度ゼロ（`prev = 今`） |
| `collideStatic` / `rub` | `collideStatic()` | 机は `Params::desk`（Rack では切ってある）。**同じジャックに挿さった他のブーツも自分のと同じく端 3 点は無視**（試作に重ね挿しは無い） |
| `collideCables` / `segPush` | `collideCables()` / `segPush()` | 格子 1.2cm。**Map の挿入順を `shCells` で再現**（結果が順番に依る） |
| `applyDrag`、pointerdown/up | `startDrag` / `moveDrag` / `endDrag` | 掴んだ点を `w = 0`、離すと `wake(100)` |

- 試作と全点が一致する（`python3 reference/physics_check/check_physics.py`、差 0）。物理を変えたら必ず回す。
- 試作の `Math.hypot` は 3 引数。Rack は C++11 なので `hypot3()` を自前で持つ。

## Rack につなぐ部分（`src/app/CableRig.cpp`）

- `Rig` が CableWidget ごとに `Entry`（物理のケーブル・描画用の点・パッチから読んだ JSON）を持つ。`RackWidget::step()` から 1 フレーム 1 回。
- ジャック A = 出力側、B = 入力側。cm = Rack の px / 29.528。
- 挿していないプラグ（手で持っている間）は、物理なしで「挿した瞬間の弧」をマウスまで毎フレーム作り直して描くだけ。
- モジュールが動いたら固定点を動かして起こす。届かなくなったら標準の長さで作り直す。
- パネルの部品（仕様 5.1）：`ModuleWidget::getParams()` / `getPorts()` から。ノブ 1.6〜2.2cm（直径 0.6→2.7cm で直線）、
  `SliderKnob` 0.5cm、ほかの param（スイッチ・ボタン）1.0cm、ジャック 0.3cm。3.5cm を超える param（表示・タッチ面）は無視。
- 起こすのは：新しく作ったケーブル、モジュールが動いた、ケーブルが消えた、設定が変わった（ただし前のフレームから寝ていたケーブルがあるときだけ）。
  **パッチから読んだケーブルは起こさない**（保存したときの形のまま。読み直しても差 0 を確かめた）。

## 描画（`CableRig.cpp` の後半、数字は `Look`）

- 試作 `densify` → `densify()`：3D で 1 回 1:2:1 → Catmull-Rom 5 分割。光の向きのずらし `o = n (n・L)`。
- 試作はカメラ 0° でも透視が残る（端のブーツが筒に見える）。Rack は真正面の平行投影なので、ブーツは先端の円だけ（リブは見えない）。
- フラット：10 点ずつの区間をブーツと一緒に z の低い順。縁（w+1）は自分の範囲、本体は前後 3 点はみ出す。
- 立体：ケーブル 1 本を区間に分けず 5 本の線で連続に描き、平均 z で並べる（仕様 4.4）。自分のブーツより下になるときはブーツを先に描く。
- 立体のブーツ：試作の 4 段の放射グラデーションを、NanoVG の 2 色グラデーション 3 枚（中間・ハイライト・縁）で近似。
- 影：ブーツは根元から影の点まで、ケーブルは 10 点ずつ butt で、高さで薄く・太く。立体は 2.2 倍・0.35 倍をもう 1 本。
- `settings::cableOpacity`（半透明）は物理のケーブルには効かない（重ねた線が透けて汚くなるため）。

## 見比べ・触って確かめる

- `sh reference/shot_rack.sh`（撮る）、`sh reference/drive_rack.sh`（xdotool で触ってから撮る）、`node reference/shot_bench.js <dir>`（試作を真正面で撮る）。
- 設定は `SETTINGS='"cableStyle": "round"'`、Understudy を読ませるのは `PLUGIN=<understudy>/vcv/dist/Understudy`。
- クラウドの xvfb はソフトウェア描画なので fps は手元より低い（test_understudy.vcv で 物理なし 23・影なし 19・影あり 15 fps）。
