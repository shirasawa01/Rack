# Understudy のモジュールでケーブル確認用のパッチ reference/test_understudy.vcv を作る（実際のパネルのノブ・ジャックに当たるかを見る）。
# 開くには understudy の vcv/ をこの Rack に向けてビルドして読ませる：
#   make -C <understudy>/vcv RACK_DIR=$PWD dist
#   PLUGIN=<understudy>/vcv/dist/Understudy sh reference/shot_rack.sh reference/test_understudy.vcv
import json, os
mods = [  # id, model, pos [HP, 段]（幅：Maths 20、Furthrrrr 約 30、TokyoGate2 約 10、Wogglebug 10、ZeroCtrl 44）
    (1, "Maths", [0, 0]), (2, "Furthrrrr", [21, 0]), (3, "TokyoGate2", [56, 0]), (4, "Wogglebug", [70, 0]),
    (5, "ZeroCtrl", [0, 1]), (6, "Maths", [46, 1]),
]
cables = [  # 出力 (module, port) -> 入力 (module, port)、色
    ((1, 0), (1, 1), "#ff5fa6"),   # 同じモジュールの中（短い輪）
    ((1, 2), (1, 3), "#ff5fa6"),
    ((2, 0), (2, 2), "#ff5fa6"),
    ((4, 1), (3, 0), "#ff5fa6"),
    ((1, 5), (2, 4), "#ecebe5"),   # 隣のモジュール
    ((2, 3), (3, 2), "#ecebe5"),
    ((5, 0), (1, 6), "#ecebe5"),   # 段をまたぐ
    ((6, 1), (2, 6), "#ecebe5"),
    ((3, 1), (6, 2), "#2a2a2c"),
    ((5, 4), (6, 5), "#8b56cc"),   # 下の段どうし
    ((4, 3), (5, 2), "#2f72c4"),   # 遠く、交差
]
patch = {"version": "2.6.6", "zoom": 1.0, "gridOffset": [-1.0, -0.1],
         "modules": [{"id": i, "plugin": "Understudy", "model": m, "version": "2.0.0", "params": [], "pos": p} for i, m, p in mods],
         "cables": [{"id": 100 + k, "outputModuleId": o[0], "outputId": o[1], "inputModuleId": n[0], "inputId": n[1], "color": c}
                    for k, (o, n, c) in enumerate(cables)]}
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_understudy.vcv"), "w") as f:
    json.dump(patch, f, indent=1)
