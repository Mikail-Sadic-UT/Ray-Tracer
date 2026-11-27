## Extra benchmarks: CPU against GPU

Stills on c642-072.vista.tacc.utexas.edu: T_br and T_frame, median of 5 runs, lbvh, lit. CPU at B = R = top threads, GPU at B = top.

| Scene | Primitives | Resolution | CPU T_br | GPU T_br | CPU / GPU | CPU T_frame | GPU T_frame | CPU / GPU | Spread (max) | CPU = GPU |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---|
| dragon_green | 249,884 | 3840x2160 | 113.3 | 16.4 | 6.9x | 110.6 | 13.2 | 8.4x | 7% | yes |
| showcase | 3,206 | 3840x2160 | 61.1 | 7.6 | 8.0x | 61.2 | 7.1 | 8.6x | 6% | yes |
