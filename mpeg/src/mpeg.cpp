#include <stdlib.h>
#include <stdio.h>

#define EXTENSION_NAME VideoPlayerMPEG1
#define LIB_NAME "VideoPlayerMPEG1"
#define MODULE_NAME "mpeg"
#include <dmsdk/sdk.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

static const int s_BytesPerPixel = 3;


static plm_t *g_plm;
static dmBuffer::HBuffer g_VideoBuffer;
static int g_VideoBufferLuaRef;

static uint64_t g_VideoBufferStreamName = dmHashString64("rgb");


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
            dest[d_index + s_BytesPerPixel + RI] = plm_clamp(y + r);
            dest[d_index + s_BytesPerPixel + GI] = plm_clamp(y - g);
            dest[d_index + s_BytesPerPixel + BI] = plm_clamp(y + b);
            y = ((frame->y.data[y_index + yw]-16) * 76309) >> 16;
            dest[d_index + stride + RI] = plm_clamp(y + r);
            dest[d_index + stride + GI] = plm_clamp(y - g);
            dest[d_index + stride + BI] = plm_clamp(y + b);
            y = ((frame->y.data[y_index + yw + 1]-16) * 76309) >> 16;
            dest[d_index + stride + s_BytesPerPixel + RI] = plm_clamp(y + r);
            dest[d_index + stride + s_BytesPerPixel + GI] = plm_clamp(y - g);
            dest[d_index + stride + s_BytesPerPixel + BI] = plm_clamp(y + b);
            c_index += 1;
            y_index += 2;
            d_index += 2 * s_BytesPerPixel;
        }
    }
}

// decoder video callback
static void app_on_video(plm_t *mpeg, plm_frame_t *frame, void *user) {
    int w = plm_get_width(g_plm);
    uint8_t* data = 0;
    uint32_t datasize = 0;
    dmBuffer::GetBytes(g_VideoBuffer, (void**)&data, &datasize);
    plm_frame_to_rgb_flip(frame, data, w * 3);
    dmBuffer::ValidateBuffer(g_VideoBuffer);
}

static void app_on_audio(plm_t *mpeg, plm_samples_t *samples, void *user) {
    // int size = sizeof(float) * samples->count * 2;
    // SDL_QueueAudio(self->audio_device, samples->interleaved, size);
}

static int Open(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 2);

    // Get video buffer and create decoder
    dmScript::LuaHBuffer* videobuffer = dmScript::CheckBuffer(L, 1);
    uint8_t* videodata = 0;
    uint32_t videodatasize = 0;
    dmBuffer::GetBytes(videobuffer->m_Buffer, (void**)&videodata, &videodatasize);

    // Create the decoder with the data from the video buffer
    g_plm = plm_create_with_memory(videodata, videodatasize, false);
    // validate opened video buffer
    if (!plm_probe(g_plm, 5000 * 1024)) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "No MPEG video or audio streams found");
        return 2;
    }

    dmLogInfo(
        "Opened video with framerate: %f, samplerate: %d, duration: %f",
        plm_get_framerate(g_plm),
        plm_get_samplerate(g_plm),
        plm_get_duration(g_plm)
    );

    // Read options and set on decoder
    bool loop_video = false;
    bool enable_audio = false;
    if (!lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushvalue(L, 2);
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
    plm_set_loop(g_plm, loop_video);
    plm_set_audio_enabled(g_plm, enable_audio);
    plm_set_video_decode_callback(g_plm, app_on_video, 0x0);
    if (enable_audio) {
        plm_set_audio_decode_callback(g_plm, app_on_audio, 0x0);
    }

    // create frame buffer to which video frames are decoded
    int width = plm_get_width(g_plm);
    int height = plm_get_height(g_plm);
    const uint32_t size = width * height;
    dmBuffer::StreamDeclaration streams_decl[] = {
        {g_VideoBufferStreamName, dmBuffer::VALUE_TYPE_UINT8, 3}
    };
    dmBuffer::Create(size, streams_decl, 1, &g_VideoBuffer);

    // reset video frame buffer to 0
    uint8_t* framedata = 0;
    uint32_t framedatasize = 0;
    dmBuffer::GetBytes(g_VideoBuffer, (void**)&framedata, &framedatasize);
    memset(framedata, 0, framedatasize);

    // increase ref count of video frame buffer to avoid GC
    dmScript::LuaHBuffer buffer(g_VideoBuffer, dmScript::OWNER_C);
    dmScript::PushBuffer(L, buffer);
    g_VideoBufferLuaRef = dmScript::Ref(L, LUA_REGISTRYINDEX);

    // ok!
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return 2;

}

static int Close(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    dmScript::Unref(L, LUA_REGISTRYINDEX, g_VideoBufferLuaRef); // We want it destroyed by the GC
    plm_destroy(g_plm);
    g_plm = 0;
    return 0;
}


static int Decode(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    double seconds = luaL_checknumber(L, 1);
    plm_decode(g_plm, seconds);
    return 0;
}


static int Seek(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    double time = luaL_checknumber(L, 1);
    plm_seek(g_plm, time, false);
    return 0;
}

static int GetInfo(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    lua_newtable(L);
    lua_pushstring(L, "width");
    lua_pushnumber(L, plm_get_width(g_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "height");
    lua_pushnumber(L, plm_get_height(g_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "bytes_per_pixel");
    lua_pushnumber(L, s_BytesPerPixel);
    lua_rawset(L, -3);

    lua_pushstring(L, "time");
    lua_pushnumber(L, plm_get_time(g_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "duration");
    lua_pushnumber(L, plm_get_duration(g_plm));
    lua_rawset(L, -3);

    lua_pushstring(L, "eos");
    lua_pushboolean(L, plm_has_ended(g_plm));
    lua_rawset(L, -3);

    return 1;
}


static int GetFrame(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    lua_rawgeti(L,LUA_REGISTRYINDEX, g_VideoBufferLuaRef);

    return 1;
}

static const luaL_reg Module_methods[] =
{
    {"open", Open},
    {"close", Close},
    {"decode", Decode},
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
