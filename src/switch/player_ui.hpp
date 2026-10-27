#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace newpipe {

struct UiColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class TextAlign { Left, Center, Right };

// Small GLES2 2D renderer for the player overlay: rounded rectangles,
// vertical gradients, rings and anti-aliased text from the console's shared
// system font. Coordinates are pixels with the origin at the top left.
// Drawn on top of the mpv frame, so it sets all GL state it relies on.
class PlayerUi {
public:
    ~PlayerUi();

    // Needs a current GL context. Returns false when shaders or the font fail;
    // the overlay then falls back to plain rectangles without text.
    bool init(int screen_height);
    void shutdown();
    bool has_font() const { return atlas_texture_ != 0; }

    void begin(int width, int height);
    void end();

    void rect(float x, float y, float w, float h, UiColor color, float radius = 0.0f);
    void gradient(float x, float y, float w, float h, UiColor top, UiColor bottom);
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, UiColor color);
    // Ring segment centred on (cx, cy); angles in radians, clockwise from 12 o'clock.
    void ring(float cx, float cy, float radius, float thickness, float start, float sweep, UiColor color);

    // Returns the drawn width. size is the line height in pixels.
    float text(float x, float y, const std::string& utf8, float size, UiColor color, TextAlign align = TextAlign::Left);
    float measure(const std::string& utf8, float size) const;
    // Cuts the text with "…" so it fits max_width.
    std::string ellipsize(const std::string& utf8, float size, float max_width) const;
    // Greedy word wrap into lines no wider than max_width.
    std::vector<std::string> wrap(const std::string& utf8, float size, float max_width) const;

private:
    struct Glyph {
        float x0, y0, x1, y1;  // quad offsets at bake size
        float s0, t0, s1, t1;  // atlas coordinates
        float advance;
    };
    struct BakedSize {
        float pixel_size = 0.0f;
        float ascent = 0.0f;
        std::vector<int> codepoints;
        std::vector<Glyph> glyphs;
    };

    const BakedSize& pick_size(float size) const;
    const Glyph* find_glyph(const BakedSize& baked, int codepoint) const;
    void draw_quads(const std::vector<float>& vertices, int mode);
    bool load_font();

    unsigned int program_ = 0;
    int attr_pos_ = -1;
    int attr_uv_ = -1;
    int attr_color_ = -1;
    int uni_screen_ = -1;
    int uni_mode_ = -1;
    int uni_rect_ = -1;
    int uni_radius_ = -1;
    int uni_ring_ = -1;
    int uni_texture_ = -1;
    unsigned int atlas_texture_ = 0;
    std::vector<BakedSize> sizes_;
    std::vector<uint8_t> font_data_;
    bool pl_initialized_ = false;
    int width_ = 1280;
    int height_ = 720;
};

}  // namespace newpipe
