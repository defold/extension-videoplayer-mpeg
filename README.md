# extension-videoplayer-mpeg
Native extension to play back MPEG1 video using [pl_mpeg](https://github.com/phoboslab/pl_mpeg).

## Usage

```lua
local buffer = resource.load(filename)

-- open video
assert(mpeg.open(buffer, { loop = true }))

-- get video information
local info = mpeg.get_info()
print(info.width, info.height)

-- get the buffer where video frames will be decoded
local framebuffer = mpeg.get_frame()

-- advance playback and decode a new frame
mpeg.decode(seconds)

-- seek to 10 seconds and decode a new frame
mpeg.seek(10)

-- close video and release all resources
mpeg.close()
```