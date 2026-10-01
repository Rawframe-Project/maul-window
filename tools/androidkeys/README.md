# Android key codes

What `tools/gen_android_keys.py` compiles into the table of Android key
codes and their Linux input codes (docs/adr/mwin-0026), kept exactly as
published:

- `Generic.kl`, Android's generic key layout, from
  `https://android.googlesource.com/platform/frameworks/base` at commit
  `1cdfff555f4a21f71ccc978290e2e212e2f8b168` (2025-03-26),
  `data/keyboards/Generic.kl`.
- `keycodes.h`, the NDK's key codes, from NDK r28 (28.2.13676358),
  `sysroot/usr/include/android/keycodes.h`.

Both are covered by the Apache License 2.0 in `LICENSE`, Copyright (C)
2010 The Android Open Source Project.

SHA-256 of each file:

```text
c7a26444f42b9544c134b8e829a0e6ddefee03b4684643b373134622bc45fac3  Generic.kl
2eecb01d0c779272e171bbe469f0425d51d9e7be6bfd2db6f227fd7d453ce938  keycodes.h
cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30  LICENSE
```
