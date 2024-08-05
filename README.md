# extension-videoplayer-mpeg
Native extension to play back MPEG1 video using [pl_mpeg](https://github.com/phoboslab/pl_mpeg).

## Usage

```lua
local buffer = resource.load(filename)

-- open video
local handle, err = mpeg.open(buffer, { loop = true })
if err then
	error(err)
	return
end

-- get video information
local info = mpeg.get_info(handle)
print(info.width, info.height)	-- video width and height
print(info.time, info.duration)	-- current playback time and video duration

-- get the buffer where video frames will be decoded
local framebuffer = mpeg.get_frame(handle)

-- advance playback and decode a new frame
mpeg.decode(handle, seconds)

-- seek to exactly 10 seconds and decode a new frame
local exact = true
mpeg.seek(handle, 10, exact)

-- close video and release all resources
mpeg.close(handle)
```