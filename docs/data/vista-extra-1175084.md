## Extra benchmarks: CPU against GPU

Stills on c642-032.vista.tacc.utexas.edu: T_br and T_frame, median of 5 runs, lbvh, lit. CPU at B = R = top threads, GPU at B = top.

| Scene | Primitives | Resolution | CPU T_br | GPU T_br | CPU / GPU | CPU T_frame | GPU T_frame | CPU / GPU | Spread (max) | CPU = GPU |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| terrain_10k | 9,800 | 1920x1080 | 8.3 | 2.1 | 4.0x | 7.4 | 0.9 | 8.3x | 18% | yes |
| map_crew | 3,024,680 | 3840x2160 | 130.4 | 37.5 | 3.5x | 106.2 | 13.1 | 8.1x | 22% | yes |
| dragon_gold | 249,892 | 3840x2160 | 100.7 | 26.3 | 3.8x | 96.8 | 22.7 | 4.3x | 4% | yes |
| balls | 50 | 3840x2160 | 74.2 | 13.3 | 5.6x | 73.5 | 13.0 | 5.6x | 2% | yes |

Walkthroughs: one preparation, then one timed frame per camera (render and download; PNG writing excluded). Frame time varies along the path by design, so min and max describe the path, not noise.

| Scene | Resolution | Frames | CPU frame median (min–max) ms | GPU frame median (min–max) ms | CPU / GPU | CPU total s | GPU total s | Frames identical |
|---|---|---:|---:|---:|---:|---:|---:|---|
| house | 1920x1080 | 240 | 63.8 (51.7–83.5) | 8.6 (5.6–15.9) | 7.0x | 15.3 | 2.2 | 240 of 240 |
| house | 3840x2160 | 240 | 231.8 (189.0–304.4) | 25.2 (17.8–35.3) | 9.2x | 55.8 | 6.0 | 240 of 240 |
