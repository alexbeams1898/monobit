#include "FontManager.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include <glad/glad.h>

// stb_truetype and stb_rect_pack -- single-header font rasterizer.
// IMPLEMENTATION defined here only (same pattern as stb_image in TextureManager.cpp).
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

static constexpr int ATLAS_SIZE = 512;
static constexpr int GROUP_ATLAS_SIZE = 1024; // larger atlas for multi-size packing
static constexpr int FIRST_CHAR = 32;         // space
static constexpr int CHAR_COUNT = 95;         // ASCII 32-126 inclusive

struct FontData
{
    GLuint atlas_tex = 0;
    GlyphInfo glyphs[CHAR_COUNT]{};
    float line_height = 0.0f;
    float ascent = 0.0f;
};

static std::vector<FontData> sFonts;
static bool sInitialized = false;

void FontManager::init()
{
    sInitialized = true;
}

void FontManager::shutdown()
{
    // Track deleted textures to avoid double-deleting shared atlases.
    std::vector<GLuint> deleted;
    for (auto& f : sFonts)
    {
        if (f.atlas_tex != 0)
        {
            bool already = false;
            for (const GLuint d : deleted)
            {
                if (d == f.atlas_tex)
                {
                    already = true;
                    break;
                }
            }
            if (!already)
            {
                glDeleteTextures(1, &f.atlas_tex);
                deleted.push_back(f.atlas_tex);
            }
            f.atlas_tex = 0;
        }
    }
    sFonts.clear();
    sInitialized = false;
}

FontHandle FontManager::loadFont(const std::string& path, float size_px)
{
    if (!sInitialized)
        return INVALID_FONT;

    // Read .ttf file into memory.
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
    {
        std::cerr << "[FontManager] Failed to open: " << path << "\n";
        return INVALID_FONT;
    }
    fseek(f, 0, SEEK_END);
    const long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> ttf_buf(static_cast<size_t>(fsize));
    fread(ttf_buf.data(), 1, static_cast<size_t>(fsize), f);
    fclose(f);

    // Bake glyph atlas.
    std::vector<unsigned char> atlas(static_cast<size_t>(ATLAS_SIZE) * ATLAS_SIZE);
    stbtt_bakedchar cdata[CHAR_COUNT]{};
    const int result = stbtt_BakeFontBitmap(ttf_buf.data(), 0, size_px, atlas.data(), ATLAS_SIZE,
                                            ATLAS_SIZE, FIRST_CHAR, CHAR_COUNT, cdata);
    if (result <= 0)
    {
        std::cerr << "[FontManager] BakeFontBitmap failed for: " << path << " at " << size_px
                  << "px\n";
        return INVALID_FONT;
    }

    // Extract font metrics for line height.
    stbtt_fontinfo info{};
    stbtt_InitFont(&info, ttf_buf.data(), 0);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    const float scale = stbtt_ScaleForPixelHeight(&info, size_px);

    FontData fd{};
    fd.ascent = static_cast<float>(ascent) * scale;
    fd.line_height = static_cast<float>(ascent - descent + line_gap) * scale;

    // Convert baked char data to GlyphInfo.
    for (int i = 0; i < CHAR_COUNT; ++i)
    {
        const auto& bc = cdata[i];
        fd.glyphs[i].u0 = static_cast<float>(bc.x0) / ATLAS_SIZE;
        fd.glyphs[i].v0 = static_cast<float>(bc.y0) / ATLAS_SIZE;
        fd.glyphs[i].u1 = static_cast<float>(bc.x1) / ATLAS_SIZE;
        fd.glyphs[i].v1 = static_cast<float>(bc.y1) / ATLAS_SIZE;
        fd.glyphs[i].x_off = bc.xoff;
        fd.glyphs[i].y_off = bc.yoff;
        fd.glyphs[i].width = static_cast<float>(bc.x1 - bc.x0);
        fd.glyphs[i].height = static_cast<float>(bc.y1 - bc.y0);
        fd.glyphs[i].advance = bc.xadvance;
    }

    // Upload atlas to GL as a single-channel (red) texture.
    glGenTextures(1, &fd.atlas_tex);
    glBindTexture(GL_TEXTURE_2D, fd.atlas_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE,
                 atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    auto handle = static_cast<FontHandle>(sFonts.size());
    sFonts.push_back(fd);
    std::cout << "[FontManager] Loaded " << path << " at " << size_px << "px (handle " << handle
              << ")\n";
    return handle;
}

std::vector<FontHandle> FontManager::loadFontGroup(const std::string& path,
                                                   std::initializer_list<float> sizes)
{
    std::vector<FontHandle> handles;
    if (!sInitialized || sizes.size() == 0)
        return handles;

    // Read .ttf file once.
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
    {
        std::cerr << "[FontManager] Failed to open: " << path << "\n";
        return handles;
    }
    fseek(f, 0, SEEK_END);
    const long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> ttf_buf(static_cast<size_t>(fsize));
    fread(ttf_buf.data(), 1, static_cast<size_t>(fsize), f);
    fclose(f);

    // Extract font metrics (shared across sizes, scaled per-size below).
    stbtt_fontinfo info{};
    stbtt_InitFont(&info, ttf_buf.data(), 0);
    int raw_ascent = 0, raw_descent = 0, raw_line_gap = 0;
    stbtt_GetFontVMetrics(&info, &raw_ascent, &raw_descent, &raw_line_gap);

    // Pack all sizes into one atlas using stb_truetype packing API.
    const size_t count = sizes.size();
    std::vector<stbtt_pack_range> ranges(count);
    std::vector<std::vector<stbtt_packedchar>> char_data(count);

    size_t idx = 0;
    for (const float sz : sizes)
    {
        char_data[idx].resize(CHAR_COUNT);
        ranges[idx].font_size = sz;
        ranges[idx].first_unicode_codepoint_in_range = FIRST_CHAR;
        ranges[idx].num_chars = CHAR_COUNT;
        ranges[idx].chardata_for_range = char_data[idx].data();
        ranges[idx].array_of_unicode_codepoints = nullptr;
        ranges[idx].h_oversample = 1;
        ranges[idx].v_oversample = 1;
        ++idx;
    }

    const int atlasW = GROUP_ATLAS_SIZE;
    const int atlasH = GROUP_ATLAS_SIZE;
    std::vector<unsigned char> atlas(static_cast<size_t>(atlasW) * atlasH);

    stbtt_pack_context ctx{};
    stbtt_PackBegin(&ctx, atlas.data(), atlasW, atlasH, 0, 1, nullptr);
    const int packOk =
        stbtt_PackFontRanges(&ctx, ttf_buf.data(), 0, ranges.data(), static_cast<int>(count));
    stbtt_PackEnd(&ctx);

    if (packOk == 0)
    {
        std::cerr << "[FontManager] PackFontRanges failed for: " << path << "\n";
        return handles;
    }

    // Upload shared atlas.
    GLuint shared_tex = 0;
    glGenTextures(1, &shared_tex);
    glBindTexture(GL_TEXTURE_2D, shared_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE,
                 atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create one FontData per size, all sharing the same atlas texture.
    // Only the first handle "owns" the texture (cleaned up in shutdown).
    idx = 0;
    for (const float sz : sizes)
    {
        const float scale = stbtt_ScaleForPixelHeight(&info, sz);
        FontData fd{};
        fd.atlas_tex = shared_tex;
        fd.ascent = static_cast<float>(raw_ascent) * scale;
        fd.line_height = static_cast<float>(raw_ascent - raw_descent + raw_line_gap) * scale;

        for (int i = 0; i < CHAR_COUNT; ++i)
        {
            const auto& pc = char_data[idx][static_cast<size_t>(i)];
            fd.glyphs[i].u0 = static_cast<float>(pc.x0) / atlasW;
            fd.glyphs[i].v0 = static_cast<float>(pc.y0) / atlasH;
            fd.glyphs[i].u1 = static_cast<float>(pc.x1) / atlasW;
            fd.glyphs[i].v1 = static_cast<float>(pc.y1) / atlasH;
            fd.glyphs[i].x_off = pc.xoff;
            fd.glyphs[i].y_off = pc.yoff;
            fd.glyphs[i].width = static_cast<float>(pc.x1 - pc.x0);
            fd.glyphs[i].height = static_cast<float>(pc.y1 - pc.y0);
            fd.glyphs[i].advance = pc.xadvance;
        }

        auto handle = static_cast<FontHandle>(sFonts.size());
        sFonts.push_back(fd);
        handles.push_back(handle);
        std::cout << "[FontManager] Loaded " << path << " at " << sz << "px (handle " << handle
                  << ", shared atlas)\n";
        ++idx;
    }

    return handles;
}

const GlyphInfo* FontManager::glyph(FontHandle handle, char ch)
{
    if (handle < 0 || handle >= static_cast<int>(sFonts.size()))
        return nullptr;
    const int idx = static_cast<int>(ch) - FIRST_CHAR;
    if (idx < 0 || idx >= CHAR_COUNT)
        return nullptr;
    return &sFonts[static_cast<size_t>(handle)].glyphs[idx];
}

uint32_t FontManager::atlasTexture(FontHandle handle)
{
    if (handle < 0 || handle >= static_cast<int>(sFonts.size()))
        return 0;
    return sFonts[static_cast<size_t>(handle)].atlas_tex;
}

float FontManager::lineHeight(FontHandle handle)
{
    if (handle < 0 || handle >= static_cast<int>(sFonts.size()))
        return 0.0f;
    return sFonts[static_cast<size_t>(handle)].line_height;
}

float FontManager::ascent(FontHandle handle)
{
    if (handle < 0 || handle >= static_cast<int>(sFonts.size()))
        return 0.0f;
    return sFonts[static_cast<size_t>(handle)].ascent;
}
