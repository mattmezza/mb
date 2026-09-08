# Browser capability fixture media

`pattern.webm` is an original, local-only two-second VP8 video test pattern. It
is 320x180 at 24 frames per second and has no audio. It was generated twice
from FFmpeg's procedural `testsrc2` lavfi source; both outputs were byte-for-
byte identical.

Generation command (run from the repository root, replacing `OUTPUT.webm` with
a fresh temporary path):

```sh
/usr/bin/ffmpeg -hide_banner -loglevel error \
  -f lavfi -i 'testsrc2=size=320x180:rate=24:duration=2' \
  -an -f webm -c:v libvpx -threads 1 -deadline good -cpu-used 0 \
  -crf 30 -b:v 0 -flags +bitexact -fflags +bitexact OUTPUT.webm
```

Generator: `/usr/bin/ffmpeg`, FFmpeg n9.0.1. The checked-in file SHA-256 is
`19a16ba64778fdd865b1584796f2e91c6558e6d6adf72605cf6ac8464df2f4d2`.

Format inspection:

```sh
/usr/bin/ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate \
  -show_entries format=duration -of default=noprint_wrappers=1 \
  mb/test/pages/pattern.webm
```
