#include <stdlib.h>
#include <stdio.h>

#define EXTENSION_NAME VideoPlayerMPEG1
#define LIB_NAME "VideoPlayerMPEG1"
#define MODULE_NAME "mpeg"
#include <dmsdk/sdk.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static const int      BYTES_PER_PIXEL = 3;
static const uint64_t FRAME_BUFFER_STREAM_NAME = dmHashString64("rgb");
// static const size_t   STREAMING_BUFFER_CAPACITY = PLM_BUFFER_DEFAULT_SIZE;
static const size_t   STREAMING_BUFFER_CAPACITY = 128 * 1024;

struct MPEG1Video {
    plm_t*                     m_plm;
    plm_buffer_t*              m_StreamBuffer;
    dmBuffer::HBuffer          m_FrameBuffer;
    int                        m_FrameBufferLuaRef;
};

// from pl_mpeg.h with flip on the vertical axis
static const int RI = 0;
static const int GI = 1;
static const int BI = 2;
void plm_frame_to_rgb_flip(plm_frame_t *frame, uint8_t *dest, int stride) {
    int cols = frame->width >> 1;
    int rows = frame->height >> 1;
    int yw = frame->y.width;
    int cw = frame->cb.width;
    for (int row = 0; row < rows; row++) {
        int c_index = row * cw;
        int y_index = row * 2 * yw;
        int d_index = (rows - row - 1) * 2 * stride;
        for (int col = 0; col < cols; col++) {
            int y;
            int cr = frame->cr.data[c_index] - 128;
            int cb = frame->cb.data[c_index] - 128;
            int r = (cr * 104597) >> 16;
            int g = (cb * 25674 + cr * 53278) >> 16;
            int b = (cb * 132201) >> 16;
            y = ((frame->y.data[y_index + 0]-16) * 76309) >> 16;
            dest[d_index + 0 + RI] = plm_clamp(y + r);
            dest[d_index + 0 + GI] = plm_clamp(y - g);
            dest[d_index + 0 + BI] = plm_clamp(y + b);
            y = ((frame->y.data[y_index + 1]-16) * 76309) >> 16;
            dest[d_index + BYTES_PER_PIXEL + RI] = plm_clamp(y + r);
            dest[d_index + BYTES_PER_PIXEL + GI] = plm_clamp(y - g);
            dest[d_index + BYTES_PER_PIXEL + BI] = plm_clamp(y + b);
            y = ((frame->y.data[y_index + yw]-16) * 76309) >> 16;
            dest[d_index + stride + RI] = plm_clamp(y + r);
            dest[d_index + stride + GI] = plm_clamp(y - g);
            dest[d_index + stride + BI] = plm_clamp(y + b);
            y = ((frame->y.data[y_index + yw + 1]-16) * 76309) >> 16;
            dest[d_index + stride + BYTES_PER_PIXEL + RI] = plm_clamp(y + r);
            dest[d_index + stride + BYTES_PER_PIXEL + GI] = plm_clamp(y - g);
            dest[d_index + stride + BYTES_PER_PIXEL + BI] = plm_clamp(y + b);
            c_index += 1;
            y_index += 2;
            d_index += 2 * BYTES_PER_PIXEL;
        }
    }
}

// decoder video callback
static void OnVideoFrameDecoded(plm_t *mpeg, plm_frame_t *frame, void *user)
{
    MPEG1Video* video = (MPEG1Video*)user;

    // create frame buffer if needed
    if (video->m_FrameBuffer == 0)
    {
        // create frame buffer to which video frames are decoded
        int width = plm_get_width(video->m_plm);
        int height = plm_get_height(video->m_plm);
        const uint32_t size = width * height;
        dmBuffer::StreamDeclaration streams_decl[] = {
            { FRAME_BUFFER_STREAM_NAME, dmBuffer::VALUE_TYPE_UINT8, 3 }
        };
        dmBuffer::Create(size, streams_decl, 1, &video->m_FrameBuffer);

        // reset frame buffer to 0
        uint8_t* framedata = 0;
        uint32_t framedatasize = 0;
        dmBuffer::GetBytes(video->m_FrameBuffer, (void**)&framedata, &framedatasize);
        memset(framedata, 0, framedatasize);
    }

    // get frame buffer bytes
    uint8_t* data = 0;
    uint32_t datasize = 0;
    dmBuffer::GetBytes(video->m_FrameBuffer, (void**)&data, &datasize);

    // decode frame to buffer
    int w = plm_get_width(video->m_plm);
    plm_frame_to_rgb_flip(frame, data, w * 3);

    // validate the integrity of the frame buffer
    dmBuffer::ValidateBuffer(video->m_FrameBuffer);
}


static void OnAudioSamplesDecoded(plm_t *mpeg, plm_samples_t *samples, void *user)
{
    // MPEG1Video* video = (MPEG1Video*)user;
    // int size = sizeof(float) * samples->count * 2;
    // SDL_QueueAudio(self->audio_device, samples->interleaved, size);
}


static MPEG1Video* CheckVideo(lua_State* L, int index)
{
    MPEG1Video* video = (MPEG1Video*)(uintptr_t)luaL_checknumber(L, index);
    if (video == 0x0)
    {
        luaL_error(L, "%s", "Video is null");
    }
    return video;
}


static void SetVideoOptions(MPEG1Video* video, lua_State* L, int index)
{
    // Read options and set on decoder
    bool loop_video = false;
    bool enable_audio = false;
    if (!lua_isnil(L, index)) {
        luaL_checktype(L, index, LUA_TTABLE);
        lua_pushvalue(L, index);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            const char* option = lua_tostring(L, -2);
            if (strcmp(option, "loop") == 0)
            {
                loop_video = lua_toboolean(L, -1);
            }
            else if (strcmp(option, "audio") == 0)
            {
                enable_audio = lua_toboolean(L, -1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    plm_set_loop(video->m_plm, loop_video);
    plm_set_audio_enabled(video->m_plm, enable_audio);
    plm_set_video_decode_callback(video->m_plm, OnVideoFrameDecoded, video);
    if (enable_audio) {
        plm_set_audio_decode_callback(video->m_plm, OnAudioSamplesDecoded, video);
    }
}


static int Open(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 2);

    MPEG1Video* video = new MPEG1Video;
    memset(video, 0, sizeof(*video));

    uint8_t* videodata = 0;
    size_t videodatasize = 0;
    if (lua_isstring(L, 1))
    {
        videodata = (uint8_t*)luaL_checklstring(L, 1, &videodatasize);
    }
    else if (dmScript::IsBuffer(L, 1))
    {
        dmScript::LuaHBuffer* videobuffer = dmScript::CheckBuffer(L, 1);
        uint32_t videodatacomponents = 0;
        uint32_t videodatastride = 0;
        dmBuffer::Result r = dmBuffer::GetStream(videobuffer->m_Buffer, dmHashString64("data"), (void**)&videodata, (uint32_t*)&videodatasize, &videodatacomponents, &videodatastride);
        // dmBuffer::GetBytes(videobuffer->m_Buffer, (void**)&videodata, &videodatasize);
    }
    else
    {
        lua_pushboolean(L, 0);
        luaL_error(L, "%s", "Video is null");
        return 2;
    }

    // Create the decoder with the data from the video buffer
    bool free_when_done = false;
    video->m_plm = plm_create_with_memory(videodata, videodatasize, free_when_done);
    // validate opened video buffer
    if (!plm_probe(video->m_plm, 5000 * 1024)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "No MPEG video or audio streams found");
        return 2;
    }

    SetVideoOptions(video, L, 2);

    // ok!
    lua_pushnumber(L, (uintptr_t)video);
    lua_pushnil(L);
    return 2;
}


static int Stream(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 2);

    MPEG1Video* video = new MPEG1Video;
    memset(video, 0, sizeof(*video));

    video->m_StreamBuffer = plm_buffer_create_with_capacity(STREAMING_BUFFER_CAPACITY);

    int destroy_when_done = true;
    video->m_plm = plm_create_with_buffer(video->m_StreamBuffer, destroy_when_done);
    // plm_buffer_set_load_callback(video->m_StreamBuffer, OnVideoBufferNeedsData, video);

    SetVideoOptions(video, L, 1);

    // ok!
    lua_pushnumber(L, (uintptr_t)video);
    lua_pushnil(L);
    return 2;
}


static int Write(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 2);

    MPEG1Video* video = CheckVideo(L, 1);

    if (video->m_StreamBuffer == 0)
    {
        lua_pushnil(L);
        lua_pushstring(L, "Video was not created using stream()");
        return 0;
    }

    size_t length = 0;
    uint8_t* bytes = (uint8_t*)luaL_checklstring(L, 2, &length);

    if (length == 0)
    {
        plm_buffer_signal_end(video->m_StreamBuffer);
        lua_pushnumber(L, 0);
        lua_pushnil(L);
    }
    else
    {
        size_t bytes_written = plm_buffer_write(video->m_StreamBuffer, bytes, length);
        lua_pushnumber(L, bytes_written);
        lua_pushnil(L);
    }
    return 0;
}


static int Close(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    MPEG1Video* video = CheckVideo(L, 1);

    if (video->m_FrameBufferLuaRef != 0)
    {
        // We want it destroyed by the GC
        dmScript::Unref(L, LUA_REGISTRYINDEX, video->m_FrameBufferLuaRef);
        video->m_FrameBufferLuaRef = 0;
    }

    if (video->m_FrameBuffer != 0)
    {
        dmBuffer::Destroy(video->m_FrameBuffer);
        video->m_FrameBuffer = 0;
    }

    plm_destroy(video->m_plm);
    video->m_plm = 0;

    delete video;
    return 0;
}


static int Decode(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    MPEG1Video* video = CheckVideo(L, 1);
    double seconds = luaL_checknumber(L, 2);
    if (!plm_has_ended(video->m_plm))
    {
        plm_decode(video->m_plm, seconds);
    }
    return 0;
}


static int Seek(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    MPEG1Video* video = CheckVideo(L, 1);
    double time = luaL_checknumber(L, 2);
    bool seek_exact = lua_toboolean(L, 3);
    plm_seek(video->m_plm, time, seek_exact);
    return 0;
}


static int Rewind(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    MPEG1Video* video = CheckVideo(L, 1);
    plm_rewind(video->m_plm);
    return 0;
}


static int GetInfo(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    MPEG1Video* video = CheckVideo(L, 1);

    lua_newtable(L);
    lua_pushstring(L, "width");
    lua_pushnumber(L, plm_get_width(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "height");
    lua_pushnumber(L, plm_get_height(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "framerate");
    lua_pushnumber(L, plm_get_framerate(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "samplerate");
    lua_pushnumber(L, plm_get_samplerate(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "bytes_per_pixel");
    lua_pushnumber(L, BYTES_PER_PIXEL);
    lua_rawset(L, -3);

    lua_pushstring(L, "time");
    lua_pushnumber(L, plm_get_time(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "duration");
    lua_pushnumber(L, plm_get_duration(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "eos");
    lua_pushboolean(L, plm_has_ended(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "is_looping");
    lua_pushboolean(L, plm_get_loop(video->m_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "is_streaming");
    lua_pushboolean(L, (video->m_StreamBuffer != 0));
    lua_rawset(L, -3);

    if (video->m_StreamBuffer)
    {
        lua_pushstring(L, "streaming_buffer_bytes_remaining");
        lua_pushnumber(L, plm_buffer_get_remaining(video->m_StreamBuffer));
        lua_rawset(L, -3);

        lua_pushstring(L, "streaming_buffer_capacity");
        lua_pushnumber(L, video->m_StreamBuffer->capacity);
        lua_rawset(L, -3);

        lua_pushstring(L, "streaming_buffer_size");
        lua_pushnumber(L, plm_buffer_get_size(video->m_StreamBuffer));
        lua_rawset(L, -3);
    }

    return 1;
}


static int GetFrame(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    MPEG1Video* video = CheckVideo(L, 1);

    if (video->m_FrameBuffer != 0)
    {
        if (video->m_FrameBufferLuaRef == 0)
        {
            // increase ref count of frame buffer to avoid GC
            dmScript::LuaHBuffer buffer(video->m_FrameBuffer, dmScript::OWNER_C);
            dmScript::PushBuffer(L, buffer);
            video->m_FrameBufferLuaRef = dmScript::Ref(L, LUA_REGISTRYINDEX);
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, video->m_FrameBufferLuaRef);
    }
    else
    {
        lua_pushnil(L);
    }

    return 1;
}

static const luaL_reg Module_methods[] =
{
    {"open", Open},
    {"stream", Stream},
    {"write", Write},
    {"close", Close},
    {"decode", Decode},
    {"rewind", Rewind},
    {"seek", Seek},
    {"get_info", GetInfo},
    {"get_frame", GetFrame},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeVideoPlayerMPEG1(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeVideoPlayerMPEG1(dmExtension::Params* params)
{
    LuaInit(params->m_L);
    dmLogInfo("Registered %s Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeVideoPlayerMPEG1(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeVideoPlayerMPEG1(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeVideoPlayerMPEG1, AppFinalizeVideoPlayerMPEG1, InitializeVideoPlayerMPEG1, 0, 0, FinalizeVideoPlayerMPEG1)
