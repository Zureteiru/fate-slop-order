#!/usr/bin/env python3
"""Compares deinflect_hoshi and deinflect_vnpop output line by line. Exit 1 on any difference.

  compare.py hoshi.tsv vnpop.tsv diff.txt"""
import sys

hoshi = open(sys.argv[1], encoding="utf-8").read().splitlines()
vnpop = open(sys.argv[2], encoding="utf-8").read().splitlines()
assert len(hoshi) == len(vnpop), (len(hoshi), len(vnpop))
diffs = []
order_only = 0
for h, v in zip(hoshi, vnpop):
    if h == v:
        continue
    text, hr = h.split("\t", 1)
    _, vr = v.split("\t", 1)
    hs, vs = hr.split(" ; "), vr.split(" ; ")
    if sorted(hs) == sorted(vs):
        order_only += 1
        continue
    diffs.append((text, sorted(set(hs) - set(vs)), sorted(set(vs) - set(hs))))
with open(sys.argv[3], "w", encoding="utf-8") as f:
    for text, missing, extra in diffs:
        f.write("== %s\n" % text)
        for m in missing:
            f.write("  only hoshidicts: %s\n" % m)
        for e in extra:
            f.write("  only vnpop:      %s\n" % e)
print("%d inputs, %d differ, %d differ only in order (details: %s)" % (len(hoshi), len(diffs), order_only, sys.argv[3]))
sys.exit(1 if diffs or order_only else 0)
