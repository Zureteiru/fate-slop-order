#!/usr/bin/env python3
"""Inputs for the deinflection parity check, one per line (deduplicated):
- every substring a lookup tries (1-16 characters from each position) of the JP story text in
  .cache/scripts/JP_*.txt (tags stripped), and
- verb / adjective stems glued to common inflection endings, valid or not: the two
  deinflectors must agree on every input, not only on real Japanese."""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
out = set()

for path in glob.glob(os.path.join(ROOT, ".cache", "scripts", "JP_*.txt")):
    for line in open(path, encoding="utf-8"):
        if line.startswith(("＄", "$", "[", "＠", "@")) and not line.startswith("[r]"):
            continue
        text = re.sub(r"\[[^\]]*\]", "", line).strip()
        text = re.sub(r"[\s　]", "", text)
        for i in range(len(text)):
            for n in range(1, 17):
                if i + n <= len(text):
                    out.add(text[i:i + n])

STEMS = ["食べ", "見", "起き", "書", "泳", "話", "待", "死", "遊", "読", "帰", "買", "行", "来", "し", "勉強し",
         "問", "感じ", "美し", "高", "良", "い", "すご", "分か", "言", "飲", "出", "知", "思", "やら", "くれ"]
KANA = ["", "か", "き", "く", "け", "こ", "が", "ぎ", "ぐ", "げ", "ご", "さ", "し", "す", "せ", "そ", "た", "ち", "つ",
        "て", "と", "な", "に", "ぬ", "ね", "の", "ば", "び", "ぶ", "べ", "ぼ", "ま", "み", "む", "め", "も", "ら", "り",
        "る", "れ", "ろ", "わ", "い", "う", "え", "お", "っ", "ん", "かっ", "く"]
ENDINGS = ["", "る", "た", "て", "だ", "で", "ない", "なかった", "なくて", "ません", "ませんでした", "ます", "ました",
           "まして", "ましょう", "ている", "ていた", "ています", "てる", "てた", "てしまう", "てしまった", "ちゃう",
           "ちゃった", "じゃう", "じゃった", "ちまう", "ておく", "とく", "といた", "てくる", "てきた", "ていく", "てった",
           "てみる", "てみた", "てある", "てください", "てくれ", "てもらう", "たい", "たくない", "たかった", "たければ",
           "ば", "れば", "なければ", "なきゃ", "なくちゃ", "ちゃ", "じゃ", "たら", "だら", "たり", "だり", "よう", "ろ",
           "よ", "な", "なさい", "ず", "ずに", "ぬ", "ん", "んで", "られる", "られた", "れる", "させる", "せる",
           "させられる", "される", "そう", "すぎる", "すぎた", "ながら", "さ", "げ", "がる", "がった", "まい", "やがる",
           "ねえ", "へん", "ひん", "とる", "たろう", "かった", "くない", "くて", "ければ", "かろう", "き", "さそう"]
for s in STEMS:
    for k in KANA:
        for e in ENDINGS:
            if 1 <= len(s + k + e) <= 16:
                out.add(s + k + e)

sys.stdout.write("\n".join(sorted(out)) + "\n")
