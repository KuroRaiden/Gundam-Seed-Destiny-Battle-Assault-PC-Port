# 0.1.7 fixed

- Fixed JP character-select alignment in fullscreen and windows with side/top bars.
- Fixed pixel seams at fractional scales in the software renderer.
- Stage select and returning from the F1 movelist keep the correct alignment.

Verified in Direct3D 11 and software rendering, including borderless/exclusive
fullscreen, wide/tall windows, odd pixel dimensions, and return to windowed mode.
This changes host presentation only; gameplay and netplay data are unchanged.
