#include "player_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <switch.h>
#include <SDL2/SDL_opengles2.h>

#include "newpipe/log.hpp"

// Own static copy: borealis compiles stb_truetype with fontstash's allocator,
// which cannot be called without a fontstash context.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "borealis/extern/nanovg/stb_truetype.h"

namespace newpipe {
namespace {

constexpr int kAtlasWidth = 2048;
constexpr int kAtlasHeight = 2048;
// Line heights baked at 720p; scaled by screen height / 720.
constexpr float kBakeSizes[] = {20.0f, 28.0f, 40.0f};

// Codepoints covered: ASCII, Latin-1 (German umlauts etc.) and a few symbols.
const int kExtraCodepoints[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2713, 0x00D7};

const char* kVertexShader = R"(
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform vec2 u_screen;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    vec2 ndc = vec2(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// mode 0: rounded rect (v_uv = local position from the centre)
// mode 1: text (v_uv = atlas coordinates)
// mode 2: ring segment (v_uv = local position from the centre)
// mode 3: plain vertex colour
const char* kFragmentShader = R"(
precision mediump float;
uniform int u_mode;
uniform vec2 u_rect;
uniform float u_radius;
uniform vec4 u_ring;
uniform sampler2D u_texture;
varying vec2 v_uv;
varying vec4 v_color;
const float PI = 3.14159265;
void main() {
    if (u_mode == 1) {
        gl_FragColor = vec4(v_color.rgb, v_color.a * texture2D(u_texture, v_uv).a);
        return;
    }
    if (u_mode == 0) {
        vec2 q = abs(v_uv) - u_rect + vec2(u_radius);
        float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;
        float alpha = clamp(0.5 - dist, 0.0, 1.0);
        gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);
        return;
    }
    if (u_mode == 2) {
        float r = length(v_uv);
        float edge = min(r - u_ring.x, u_ring.y - r);
        float angle = atan(v_uv.x, -v_uv.y);
        if (angle < 0.0) {
            angle += 2.0 * PI;
        }
        float rel = mod(angle - u_ring.z + 4.0 * PI, 2.0 * PI);
        float alpha = clamp(edge + 0.5, 0.0, 1.0) * step(rel, u_ring.w);
        gl_FragColor = vec4(v_color.rgb, v_color.a * alpha);
        return;
    }
    gl_FragColor = v_color;
}
)";

GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        logf("player_ui: shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Decodes one UTF-8 sequence; invalid bytes come back as '?'.
int next_codepoint(const std::string& text, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    int length = 1;
    int codepoint = c;
    if (c >= 0xF0) {
        length = 4;
        codepoint = c & 0x07;
    } else if (c >= 0xE0) {
        length = 3;
        codepoint = c & 0x0F;
    } else if (c >= 0xC0) {
        length = 2;
        codepoint = c & 0x1F;
    } else if (c >= 0x80) {
        ++i;
        return '?';
    }
    if (i + length > text.size()) {
        i = text.size();
        return '?';
    }
    for (int k = 1; k < length; ++k) {
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    }
    i += length;
    return codepoint;
}

void push_vertex(std::vector<float>& out, float x, float y, float u, float v, UiColor c) {
    out.insert(out.end(), {x, y, u, v, c.r, c.g, c.b, c.a});
}

void push_quad(std::vector<float>& out, float x0, float y0, float x1, float y1,
               float u0, float v0, float u1, float v1, UiColor top, UiColor bottom) {
    push_vertex(out, x0, y0, u0, v0, top);
    push_vertex(out, x1, y0, u1, v0, top);
    push_vertex(out, x0, y1, u0, v1, bottom);
    push_vertex(out, x1, y0, u1, v0, top);
    push_vertex(out, x1, y1, u1, v1, bottom);
    push_vertex(out, x0, y1, u0, v1, bottom);
}

}  // namespace

PlayerUi::~PlayerUi() {
    this->shutdown();
}

bool PlayerUi::load_font() {
    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        this->pl_initialized_ = true;
        PlFontData font{};
        if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard)) && font.address && font.size) {
            const auto* bytes = static_cast<const uint8_t*>(font.address);
            this->font_data_.assign(bytes, bytes + font.size);
            return true;
        }
    }
    log_line("player_ui: shared font unavailable");
    return false;
}

bool PlayerUi::init(int screen_height) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        return false;
    }
    this->program_ = glCreateProgram();
    glAttachShader(this->program_, vs);
    glAttachShader(this->program_, fs);
    glLinkProgram(this->program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(this->program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        log_line("player_ui: program link failed");
        glDeleteProgram(this->program_);
        this->program_ = 0;
        return false;
    }

    this->attr_pos_ = glGetAttribLocation(this->program_, "a_pos");
    this->attr_uv_ = glGetAttribLocation(this->program_, "a_uv");
    this->attr_color_ = glGetAttribLocation(this->program_, "a_color");
    this->uni_screen_ = glGetUniformLocation(this->program_, "u_screen");
    this->uni_mode_ = glGetUniformLocation(this->program_, "u_mode");
    this->uni_rect_ = glGetUniformLocation(this->program_, "u_rect");
    this->uni_radius_ = glGetUniformLocation(this->program_, "u_radius");
    this->uni_ring_ = glGetUniformLocation(this->program_, "u_ring");
    this->uni_texture_ = glGetUniformLocation(this->program_, "u_texture");

    if (!this->load_font()) {
        return true;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, this->font_data_.data(), stbtt_GetFontOffsetForIndex(this->font_data_.data(), 0))) {
        log_line("player_ui: font init failed");
        return true;
    }

    std::vector<int> codepoints;
    for (int cp = 32; cp < 127; ++cp) {
        codepoints.push_back(cp);
    }
    for (int cp = 160; cp < 256; ++cp) {
        codepoints.push_back(cp);
    }
    codepoints.insert(codepoints.end(), std::begin(kExtraCodepoints), std::end(kExtraCodepoints));

    std::vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * kAtlasHeight, 0);
    stbtt_pack_context pack;
    if (!stbtt_PackBegin(&pack, pixels.data(), kAtlasWidth, kAtlasHeight, 0, 1, nullptr)) {
        log_line("player_ui: font pack begin failed");
        return true;
    }
    stbtt_PackSetOversampling(&pack, 2, 1);

    const float scale = std::max(1.0f, static_cast<float>(screen_height) / 720.0f);
    for (float base : kBakeSizes) {
        BakedSize baked;
        baked.pixel_size = base * scale;
        baked.codepoints = codepoints;
        std::vector<stbtt_packedchar> packed(codepoints.size());
        stbtt_pack_range range{};
        range.font_size = baked.pixel_size;
        range.array_of_unicode_codepoints = codepoints.data();
        range.num_chars = static_cast<int>(codepoints.size());
        range.chardata_for_range = packed.data();
        if (!stbtt_PackFontRanges(&pack, this->font_data_.data(), 0, &range, 1)) {
            logf("player_ui: atlas full at size %.0f", baked.pixel_size);
        }

        int ascent = 0, descent = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
        baked.ascent = ascent * stbtt_ScaleForPixelHeight(&info, baked.pixel_size);

        for (size_t i = 0; i < codepoints.size(); ++i) {
            float px = 0.0f, py = 0.0f;
            stbtt_aligned_quad quad;
            stbtt_GetPackedQuad(packed.data(), kAtlasWidth, kAtlasHeight, static_cast<int>(i), &px, &py, &quad, 0);
            baked.glyphs.push_back({quad.x0, quad.y0, quad.x1, quad.y1, quad.s0, quad.t0, quad.s1, quad.t1, px});
        }
        this->sizes_.push_back(std::move(baked));
    }
    stbtt_PackEnd(&pack);

    glGenTextures(1, &this->atlas_texture_);
    glBindTexture(GL_TEXTURE_2D, this->atlas_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasWidth, kAtlasHeight, 0, GL_ALPHA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    logf("player_ui: font atlas ready sizes=%zu scale=%.2f", this->sizes_.size(), scale);
    return true;
}

void PlayerUi::shutdown() {
    if (this->atlas_texture_) {
        glDeleteTextures(1, &this->atlas_texture_);
        this->atlas_texture_ = 0;
    }
    if (this->program_) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
    this->sizes_.clear();
    this->font_data_.clear();
    if (this->pl_initialized_) {
        plExit();
        this->pl_initialized_ = false;
    }
}

void PlayerUi::begin(int width, int height) {
    this->width_ = width;
    this->height_ = height;
    if (!this->program_) {
        return;
    }
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(this->program_);
    glUniform2f(this->uni_screen_, static_cast<float>(width), static_cast<float>(height));
    glUniform1i(this->uni_texture_, 0);
    glEnableVertexAttribArray(this->attr_pos_);
    glEnableVertexAttribArray(this->attr_uv_);
    glEnableVertexAttribArray(this->attr_color_);
}

void PlayerUi::end() {
    if (!this->program_) {
        return;
    }
    glDisableVertexAttribArray(this->attr_pos_);
    glDisableVertexAttribArray(this->attr_uv_);
    glDisableVertexAttribArray(this->attr_color_);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void PlayerUi::draw_quads(const std::vector<float>& vertices, int mode) {
    if (!this->program_ || vertices.empty()) {
        return;
    }
    glUniform1i(this->uni_mode_, mode);
    const GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(this->attr_pos_, 2, GL_FLOAT, GL_FALSE, stride, vertices.data());
    glVertexAttribPointer(this->attr_uv_, 2, GL_FLOAT, GL_FALSE, stride, vertices.data() + 2);
    glVertexAttribPointer(this->attr_color_, 4, GL_FLOAT, GL_FALSE, stride, vertices.data() + 4);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size() / 8));
}

void PlayerUi::rect(float x, float y, float w, float h, UiColor color, float radius) {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    const float hw = w / 2.0f;
    const float hh = h / 2.0f;
    std::vector<float> v;
    push_quad(v, x, y, x + w, y + h, -hw, -hh, hw, hh, color, color);
    if (this->program_) {
        glUniform2f(this->uni_rect_, hw, hh);
        glUniform1f(this->uni_radius_, std::min(radius, std::min(hw, hh)));
    }
    this->draw_quads(v, 0);
}

void PlayerUi::gradient(float x, float y, float w, float h, UiColor top, UiColor bottom) {
    std::vector<float> v;
    push_quad(v, x, y, x + w, y + h, 0, 0, 0, 0, top, bottom);
    this->draw_quads(v, 3);
}

void PlayerUi::triangle(float x1, float y1, float x2, float y2, float x3, float y3, UiColor color) {
    std::vector<float> v;
    push_vertex(v, x1, y1, 0, 0, color);
    push_vertex(v, x2, y2, 0, 0, color);
    push_vertex(v, x3, y3, 0, 0, color);
    this->draw_quads(v, 3);
}

void PlayerUi::ring(float cx, float cy, float radius, float thickness, float start, float sweep, UiColor color) {
    const float r = radius + 1.0f;
    std::vector<float> v;
    push_quad(v, cx - r, cy - r, cx + r, cy + r, -r, -r, r, r, color, color);
    if (this->program_) {
        glUniform4f(this->uni_ring_, radius - thickness, radius, start, sweep);
    }
    this->draw_quads(v, 2);
}

const PlayerUi::BakedSize& PlayerUi::pick_size(float size) const {
    for (const auto& baked : this->sizes_) {
        if (baked.pixel_size >= size * 0.95f) {
            return baked;
        }
    }
    return this->sizes_.back();
}

const PlayerUi::Glyph* PlayerUi::find_glyph(const BakedSize& baked, int codepoint) const {
    const auto it = std::find(baked.codepoints.begin(), baked.codepoints.end(), codepoint);
    if (it == baked.codepoints.end()) {
        return nullptr;
    }
    return &baked.glyphs[static_cast<size_t>(it - baked.codepoints.begin())];
}

float PlayerUi::measure(const std::string& utf8, float size) const {
    if (this->sizes_.empty()) {
        return 0.0f;
    }
    const BakedSize& baked = this->pick_size(size);
    const float k = size / baked.pixel_size;
    float width = 0.0f;
    for (size_t i = 0; i < utf8.size();) {
        const Glyph* glyph = this->find_glyph(baked, next_codepoint(utf8, i));
        if (!glyph) {
            glyph = this->find_glyph(baked, '?');
        }
        if (glyph) {
            width += glyph->advance * k;
        }
    }
    return width;
}

float PlayerUi::text(float x, float y, const std::string& utf8, float size, UiColor color, TextAlign align) {
    if (this->sizes_.empty() || utf8.empty()) {
        return 0.0f;
    }
    const float width = this->measure(utf8, size);
    if (align == TextAlign::Center) {
        x -= width / 2.0f;
    } else if (align == TextAlign::Right) {
        x -= width;
    }

    const BakedSize& baked = this->pick_size(size);
    const float k = size / baked.pixel_size;
    // y is the top of the line box; the baseline sits at the scaled ascent.
    const float baseline = std::round(y + baked.ascent * k);
    float pen = std::round(x);
    std::vector<float> v;
    for (size_t i = 0; i < utf8.size();) {
        const Glyph* glyph = this->find_glyph(baked, next_codepoint(utf8, i));
        if (!glyph) {
            glyph = this->find_glyph(baked, '?');
        }
        if (!glyph) {
            continue;
        }
        push_quad(v,
                  pen + glyph->x0 * k, baseline + glyph->y0 * k,
                  pen + glyph->x1 * k, baseline + glyph->y1 * k,
                  glyph->s0, glyph->t0, glyph->s1, glyph->t1, color, color);
        pen += glyph->advance * k;
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, this->atlas_texture_);
    this->draw_quads(v, 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    return width;
}

std::string PlayerUi::ellipsize(const std::string& utf8, float size, float max_width) const {
    if (this->measure(utf8, size) <= max_width) {
        return utf8;
    }
    const std::string dots = "\xE2\x80\xA6";
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        size_t start = i;
        next_codepoint(utf8, i);
        const std::string candidate = out + utf8.substr(start, i - start);
        if (this->measure(candidate + dots, size) > max_width) {
            break;
        }
        out = candidate;
    }
    return out + dots;
}

std::vector<std::string> PlayerUi::wrap(const std::string& utf8, float size, float max_width) const {
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    auto flush_word = [&]() {
        if (word.empty()) {
            return;
        }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && this->measure(candidate, size) > max_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char ch : utf8) {
        if (ch == '\n') {
            flush_word();
            lines.push_back(line);
            line.clear();
        } else if (ch == ' ') {
            flush_word();
        } else {
            word.push_back(ch);
        }
    }
    flush_word();
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace newpipe
