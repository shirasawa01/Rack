# Core モジュールだけでケーブル確認用のパッチ reference/test_cables.vcv を作る（Fundamental なしの開発モードでも開ける）。
# 長さ（隣・同じ段の遠く・段をまたぐ）と交差がそろうように並べる。.vcv は先頭が '{' なら JSON のまま読める。
import json, os
mods = [  # id, model, pos [HP, 段]
    (1, "MIDIToCVInterface", [0, 0]), (2, "AudioInterface2", [12, 0]), (3, "AudioInterface", [40, 0]),
    (4, "CV-MIDI", [0, 1]), (5, "AudioInterface", [20, 1]),
]
cables = [  # 出力 (module, port) -> 入力 (module, port)、色
    ((1, 0), (2, 0), "#f3374b"),  # 隣（短い）
    ((1, 1), (2, 1), "#ffb437"),
    ((1, 2), (3, 0), "#00b56e"),  # 同じ段の遠く
    ((1, 3), (5, 2), "#3695ef"),  # 段をまたぐ
    ((3, 0), (4, 0), "#8b4ade"),  # 段をまたいで戻る（交差）
    ((5, 1), (4, 5), "#202020"),
    ((2, 0), (5, 6), "#e0e0e0"),
]
patch = {"version": "2.6.6", "zoom": 1.0, "gridOffset": [-1.0, -0.1],
         "modules": [{"id": i, "plugin": "Core", "model": m, "version": "2.6.6", "params": [], "pos": p} for i, m, p in mods],
         "cables": [{"id": 100 + k, "outputModuleId": o[0], "outputId": o[1], "inputModuleId": n[0], "inputId": n[1], "color": c}
                    for k, (o, n, c) in enumerate(cables)]}
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_cables.vcv"), "w") as f:
    json.dump(patch, f, indent=1)
