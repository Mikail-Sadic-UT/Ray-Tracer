#!/usr/bin/env bash
# Renders a camera orbit with rt_render and encodes it as an H.264 MP4.
#
#   make_video.sh --scene SCENE.json --out VIDEO.mp4 [--frames N] [--fps F] [--keep-frames DIR]
#                 [--rt-render BIN] [-- extra rt_render options]
#
# The scene is prepared once; rt_render then renders N frames on one turn around the camera
# target (--orbit) and prints T_frame over the path. Anything after `--` goes to rt_render,
# for example `-- --backend cuda --bvh lbvh --build-threads 16 --width 1280 --height 720`.
# Needs ffmpeg (sudo apt install ffmpeg).
set -euo pipefail

scene="" out="" frames=120 fps=30 keep="" rt_render="build/apps/rt_render/rt_render"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --scene) scene="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --frames) frames="$2"; shift 2 ;;
        --fps) fps="$2"; shift 2 ;;
        --keep-frames) keep="$2"; shift 2 ;;
        --rt-render) rt_render="$2"; shift 2 ;;
        --) shift; break ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "make_video.sh: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
done
[[ -n "$scene" && -n "$out" ]] || { echo "make_video.sh: --scene and --out are required" >&2; exit 2; }
command -v ffmpeg >/dev/null || { echo "make_video.sh: ffmpeg not found (sudo apt install ffmpeg)" >&2; exit 1; }
[[ -x "$rt_render" ]] || { echo "make_video.sh: $rt_render is not built (or pass --rt-render)" >&2; exit 1; }

if [[ -n "$keep" ]]; then
    dir="$keep"
else
    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' EXIT
fi

"$rt_render" --scene "$scene" --orbit "$frames" --frames "$dir" "$@"
# yuv420p needs even dimensions; the pad keeps odd sizes playable without rescaling.
ffmpeg -loglevel error -y -framerate "$fps" -i "$dir/frame_%04d.png" \
    -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" -c:v libx264 -pix_fmt yuv420p -crf 18 "$out"
echo "make_video.sh: wrote $out ($frames frames at $fps fps)" >&2
