#include "Skin.h"
#include "FontData.h"
#include <map>
#include <thread>
#include <functional>

namespace skin
{
//==============================================================================
// Typography
static juce::Typeface::Ptr makeTypeface (const unsigned char* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}

juce::Font font (float height, Weight w, bool condensed)
{
    using namespace fontdata;
    static juce::Typeface::Ptr medium   = makeTypeface (Barlow_Medium, Barlow_MediumSize);
    static juce::Typeface::Ptr semibold = makeTypeface (Barlow_SemiBold, Barlow_SemiBoldSize);
    static juce::Typeface::Ptr bold     = makeTypeface (Barlow_Bold, Barlow_BoldSize);
    static juce::Typeface::Ptr cMedium  = makeTypeface (BarlowSemiCondensed_Medium, BarlowSemiCondensed_MediumSize);
    static juce::Typeface::Ptr cSemi    = makeTypeface (BarlowSemiCondensed_SemiBold, BarlowSemiCondensed_SemiBoldSize);
    juce::Typeface::Ptr t = condensed ? (w == Weight::Medium ? cMedium : cSemi)
                                      : (w == Weight::Medium ? medium : (w == Weight::SemiBold ? semibold : bold));
    return juce::Font (t).withHeight (height);
}

juce::Font label (float height)   { return font (height, Weight::SemiBold, true).withExtraKerningFactor (0.11f); }
juce::Font numeric (float height, Weight w) { return font (height, w, false).withExtraKerningFactor (0.02f); }

const std::array<Finish, 5>& finishes()
{
    static const std::array<Finish, 5> f {{
        { "Obsidian",       juce::Colour (0xff0b0b0d) },
        { "Black Titanium", juce::Colour (0xff0e0f11) },
        { "Carbon",         juce::Colour (0xff0a0a0a) },
        { "Midnight",       juce::Colour (0xff0a0c12) },
        { "Onyx Bronze",    juce::Colour (0xff100d0b) },
    }};
    return f;
}

// Restrained spectral palette: spectral cyan, icy silver, electric indigo, ultraviolet, deep magenta, champagne.
juce::Colour accentA (int e)
{
    static const juce::Colour c[] = { juce::Colour (0xff2fe3ff), juce::Colour (0xffdfe7ff), juce::Colour (0xff5b6cff),
                                      juce::Colour (0xffa35cff), juce::Colour (0xffff2f9f), juce::Colour (0xfff2c98a) };
    return c[juce::jlimit (0, 5, e)];
}

juce::Colour accentB (int e)
{
    static const juce::Colour c[] = { juce::Colour (0xff5b8cff), juce::Colour (0xff9fb3ff), juce::Colour (0xff8f5cff),
                                      juce::Colour (0xffd95cff), juce::Colour (0xffff5ccf), juce::Colour (0xffff9e6e) };
    return c[juce::jlimit (0, 5, e)];
}

//==============================================================================
// Noise
static inline float hash01 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (float) (x & 0xffffff) / 16777216.0f;
}
static inline float smoother (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Runs fn(rowBegin, rowEnd) over [0, rows) on all cores.
template <typename Fn>
static void parallelRows (int rows, Fn&& fn)
{
    const int threads = juce::jlimit (1, 16, (int) std::thread::hardware_concurrency());
    if (threads == 1 || rows < 64) { fn (0, rows); return; }
    std::vector<std::thread> pool;
    const int chunk = (rows + threads - 1) / threads;
    for (int t = 0; t < threads; ++t)
    {
        const int b = t * chunk, e = juce::jmin (rows, b + chunk);
        if (b < e) pool.emplace_back ([&fn, b, e] { fn (b, e); });
    }
    for (auto& th : pool) th.join();
}

// Box blur on a float field (separable, `passes` times).
static void blurField (std::vector<float>& f, int W, int H, int r, int passes)
{
    if (r < 1) return;
    std::vector<float> tmp ((size_t) std::max (W, H));
    for (int p = 0; p < passes; ++p)
    {
        for (int y = 0; y < H; ++y)
        {
            float* row = &f[(size_t) y * (size_t) W];
            float acc = 0.0f;
            for (int x = -r; x <= r; ++x) acc += row[juce::jlimit (0, W - 1, x)];
            for (int x = 0; x < W; ++x)
            {
                tmp[(size_t) x] = acc / (float) (2 * r + 1);
                acc += row[juce::jmin (W - 1, x + r + 1)] - row[juce::jmax (0, x - r)];
            }
            std::copy (tmp.begin(), tmp.begin() + W, row);
        }
        for (int x = 0; x < W; ++x)
        {
            float acc = 0.0f;
            for (int y = -r; y <= r; ++y) acc += f[(size_t) juce::jlimit (0, H - 1, y) * (size_t) W + (size_t) x];
            for (int y = 0; y < H; ++y)
            {
                tmp[(size_t) y] = acc / (float) (2 * r + 1);
                acc += f[(size_t) juce::jmin (H - 1, y + r + 1) * (size_t) W + (size_t) x]
                     - f[(size_t) juce::jmax (0, y - r) * (size_t) W + (size_t) x];
            }
            for (int y = 0; y < H; ++y) f[(size_t) y * (size_t) W + (size_t) x] = tmp[(size_t) y];
        }
    }
}

//==============================================================================
// Relief
Relief::Relief (int width, int height, float scale, juce::Colour t, bool transp, juce::Point<float> origin)
    : W (juce::jmax (1, (int) std::ceil ((float) width * scale))), H (juce::jmax (1, (int) std::ceil ((float) height * scale))),
      s (scale), ox (origin.x), oy (origin.y), tint (t), transparent (transp)
{
    const size_t n = (size_t) W * (size_t) H;
    h.assign (n, 0.0f);
    mat.assign (n, transp ? Empty : Chassis);
    cover.assign (n, transp ? 0.0f : 1.0f);
    spin.assign (n, 0);
}

float Relief::sdRect (float px, float py, juce::Rectangle<float> r, float radius) const
{
    const float cx = r.getCentreX(), cy = r.getCentreY();
    const float hx = r.getWidth() * 0.5f - radius, hy = r.getHeight() * 0.5f - radius;
    const float qx = std::abs (px - cx) - hx, qy = std::abs (py - cy) - hy;
    const float ox = juce::jmax (qx, 0.0f), oy = juce::jmax (qy, 0.0f);
    return std::sqrt (ox * ox + oy * oy) + juce::jmin (juce::jmax (qx, qy), 0.0f) - radius;
}

template <typename Fn>
void Relief::forBox (juce::Rectangle<float> box, Fn&& fn)
{
    const int x0 = juce::jmax (0, (int) std::floor ((box.getX() - ox - 2.0f) * s)), x1 = juce::jmin (W, (int) std::ceil ((box.getRight() - ox + 2.0f) * s));
    const int y0 = juce::jmax (0, (int) std::floor ((box.getY() - oy - 2.0f) * s)), y1 = juce::jmin (H, (int) std::ceil ((box.getBottom() - oy + 2.0f) * s));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            fn ((size_t) y * (size_t) W + (size_t) x, ((float) x + 0.5f) / s + ox, ((float) y + 0.5f) / s + oy);
}

void Relief::emboss (const juce::Path& path, float height, float bevel, Mat top, Mat edge)
{
    // rasterise the shape, soften it into a bevel profile, and add it to the height field
    juce::Image mask (juce::Image::SingleChannel, W, H, true);
    {
        juce::Graphics g (mask);
        g.addTransform (juce::AffineTransform::translation (-ox, -oy).scaled (s));
        g.setColour (juce::Colours::white);
        g.fillPath (path);
    }
    const auto box = path.getBounds().expanded (bevel + 2.0f);
    const int x0 = juce::jmax (0, (int) std::floor ((box.getX() - ox) * s)), x1 = juce::jmin (W, (int) std::ceil ((box.getRight() - ox) * s));
    const int y0 = juce::jmax (0, (int) std::floor ((box.getY() - oy) * s)), y1 = juce::jmin (H, (int) std::ceil ((box.getBottom() - oy) * s));
    if (x1 <= x0 || y1 <= y0) return;
    const int bw = x1 - x0, bh = y1 - y0;
    std::vector<float> m ((size_t) bw * (size_t) bh), hard ((size_t) bw * (size_t) bh);
    {
        juce::Image::BitmapData d (mask, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x)
                hard[(size_t) y * bw + x] = m[(size_t) y * bw + x] = (float) *d.getPixelPointer (x0 + x, y0 + y) / 255.0f;
    }
    blurField (m, bw, bh, juce::jmax (1, juce::roundToInt (bevel * s * 0.5f)), 2);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x)
        {
            const float v = m[(size_t) y * bw + x];
            if (v <= 0.002f) continue;
            const size_t i = (size_t) (y0 + y) * (size_t) W + (size_t) (x0 + x);
            h[i] += height * s * smoother ((v - 0.02f) / 0.96f);
            if (hard[(size_t) y * bw + x] > 0.5f || v > 0.04f)
            {
                mat[i] = v > 0.96f ? top : edge;
                spin[i] = 0;
                if (transparent) cover[i] = juce::jmax (cover[i], juce::jmin (1.0f, v * 2.0f));
            }
        }
}

void Relief::setMat (int i, Mat m, float coverage)
{
    mat[(size_t) i] = m;
    if (transparent) cover[(size_t) i] = juce::jmax (cover[(size_t) i], coverage);
}

void Relief::plate (juce::Rectangle<float> r, float radius, float height, float bevel, Mat top, Mat edge)
{
    forBox (r, [&] (size_t i, float px, float py)
    {
        const float d = sdRect (px, py, r, radius);
        if (d > 0.75f / s) return;
        const float t = juce::jlimit (0.0f, 1.0f, -d / bevel);
        h[i] += height * s * smoother (t);
        setMat ((int) i, t < 0.999f ? edge : top, juce::jlimit (0.0f, 1.0f, 0.5f - d * s));
        spin[i] = 0;
    });
}

void Relief::recess (juce::Rectangle<float> r, float radius, float depth, float bevel, Mat floor, Mat edge)
{
    forBox (r, [&] (size_t i, float px, float py)
    {
        const float d = sdRect (px, py, r, radius);
        if (d > 0.0f) return;
        const float t = juce::jlimit (0.0f, 1.0f, -d / bevel);
        h[i] -= depth * s * smoother (t);
        mat[i] = t < 0.999f ? edge : floor;
        spin[i] = 0;
    });
}

void Relief::groove (juce::Rectangle<float> r, float radius, float width, float depth)
{
    forBox (r, [&] (size_t i, float px, float py)
    {
        const float d = std::abs (sdRect (px, py, r, radius));
        const float hw = width * 0.5f;
        if (d >= hw) return;
        const float u = d / hw;
        h[i] -= depth * s * (1.0f - u * u);
    });
}

void Relief::grooveLine (juce::Point<float> a, juce::Point<float> b, float width, float depth)
{
    const juce::Line<float> line (a, b);
    forBox (juce::Rectangle<float> (a, b).expanded (width), [&] (size_t i, float px, float py)
    {
        juce::Point<float> nearest;
        const float d = line.getDistanceFromPoint ({ px, py }, nearest);
        const float hw = width * 0.5f;
        if (d >= hw) return;
        const float u = d / hw;
        h[i] -= depth * s * (1.0f - u * u);
    });
}

void Relief::mount (juce::Point<float> c, float radius, float depth)
{
    const float bevel = 2.2f;
    forBox (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), [&] (size_t i, float px, float py)
    {
        const float d = std::hypot (px - c.x, py - c.y) - radius;
        if (d > 0.0f) return;
        const float t = juce::jlimit (0.0f, 1.0f, -d / bevel);
        h[i] -= depth * s * smoother (t);
        mat[i] = t < 0.999f ? Edge : Floor;
        spin[i] = 0;
    });
}

void Relief::knobBody (juce::Point<float> c, float R, float height)
{
    // SWEEP BAR control, layered like a machined assembly:
    //   dark-chrome lathe-turned skirt  |  deep separation groove  |  floating black-titanium ring
    //   (conical, spun)                 |  fine inner groove       |  polished obsidian dome cap
    spinCentres.push_back (c);
    const auto idx = (uint8_t) juce::jmin (255, (int) spinCentres.size() - 1);
    forBox (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c), [&] (size_t i, float px, float py)
    {
        const float r = std::hypot (px - c.x, py - c.y);
        if (r > R + 0.75f / s) return;
        auto bell = [] (float x, float centre, float halfWidth) { const float u = (x - centre) / halfWidth; return u * u < 1.0f ? (1.0f - u * u) * (1.0f - u * u) : 0.0f; };

        // skirt: a cylinder with a rounded shoulder and fine lathe rings
        float z = height * smoother ((R - r) / (0.10f * R));
        z += 0.10f * std::sin (r * 6.9813f);                     // ~0.9 pt turning marks
        Mat m = Chrome;
        if (r > 0.955f * R) m = Edge;                            // polished shoulder catches the key light

        // deep separation groove: the inner ring appears to float above the skirt
        z -= 3.2f * bell (r, 0.882f * R, 0.022f * R);
        if (std::abs (r - 0.882f * R) < 0.016f * R) m = Floor;

        // floating titanium ring: raised, gently conical so it carries a reflection band
        const float ringLift = smoother ((0.866f * R - r) / (0.03f * R));
        z += ringLift * (2.4f - 0.9f * juce::jlimit (0.0f, 1.0f, (r - 0.60f * R) / (0.266f * R)));
        if (r < 0.866f * R && r >= 0.60f * R) m = (r > 0.840f * R) ? Edge : Titanium;

        // fine inner groove, then the obsidian dome
        z -= 1.5f * bell (r, 0.588f * R, 0.016f * R);
        if (r < 0.60f * R && r >= 0.576f * R) m = Floor;
        const float capR = 0.576f * R;
        if (r < capR)
        {
            // spherical dome: enough curvature to catch a crisp reflection of the key light
            const float Rs = capR * 1.30f;
            const float sag = Rs - std::sqrt (Rs * Rs - capR * capR);
            const float dome = std::sqrt (juce::jmax (0.0f, Rs * Rs - r * r)) - (Rs - sag);
            z += smoother ((capR - r) / (0.04f * R)) * (0.5f + dome);
            m = r > capR - 0.03f * R ? Edge : Obsidian;
        }

        h[i] += z * s;
        mat[i] = m;
        spin[i] = (m == Chrome || m == Titanium) ? idx : 0;
    });
}

juce::Image Relief::render()
{
    const size_t n = (size_t) W * (size_t) H;

    // ---- ambient occlusion: how far each point sits below its neighbourhood, at two scales
    std::vector<float> b1 (h), b2 (h);
    blurField (b1, W, H, juce::jmax (1, juce::roundToInt (2.5f * s)), 2);
    blurField (b2, W, H, juce::jmax (1, juce::roundToInt (10.0f * s)), 2);

    // ---- key light, rim light
    const float Lx = -0.48f, Ly = -0.74f, Lz = 0.47f;        // from the top-left, fairly high
    const float Rx = 0.74f, Ry = -0.48f, Rz = 0.47f;         // cool rim from the top-right
    const float lxy = std::sqrt (Lx * Lx + Ly * Ly);
    const float tanE = Lz / lxy, sdx = Lx / lxy, sdy = Ly / lxy;
    float hMax = 0.0f;
    for (auto v : h) hMax = juce::jmax (hMax, v);

    // ---- ray-marched soft shadows on a half-resolution grid (upsampled bilinearly)
    const int ss = s >= 1.75f ? 2 : 1;
    const int SW = (W + ss - 1) / ss, SH = (H + ss - 1) / ss;
    std::vector<float> shadow ((size_t) SW * (size_t) SH, 1.0f);
    const float maxDist = 70.0f * s;
    parallelRows (SH, [&] (int rowBegin, int rowEnd) {
    for (int sy = rowBegin; sy < rowEnd; ++sy)
        for (int sx = 0; sx < SW; ++sx)
        {
            const int x = juce::jmin (W - 1, sx * ss), y = juce::jmin (H - 1, sy * ss);
            const float h0 = h[(size_t) y * (size_t) W + (size_t) x];
            if (h0 >= hMax - 0.01f) continue;
            float vis = 1.0f, t = 1.0f * s;
            while (t < maxDist)
            {
                const int qx = (int) ((float) x + sdx * t), qy = (int) ((float) y + sdy * t);
                if (qx < 0 || qy < 0 || qx >= W || qy >= H) break;
                const float ray = h0 + t * tanE;
                if (ray > hMax) break;
                const float hs = h[(size_t) qy * (size_t) W + (size_t) qx];
                const float occ = (ray - hs) / (t * 0.30f + 0.8f * s);
                vis = juce::jmin (vis, juce::jlimit (0.0f, 1.0f, occ + 0.5f));
                if (vis <= 0.0f) break;
                t += juce::jmax (0.75f * s, t * 0.09f);
            }
            shadow[(size_t) sy * (size_t) SW + (size_t) sx] = vis;
        }
    });
    auto shadowAt = [&] (int x, int y)
    {
        if (ss == 1) return shadow[(size_t) y * (size_t) SW + (size_t) x];
        const float fx = (float) x / (float) ss, fy = (float) y / (float) ss;
        const int x0 = juce::jmin (SW - 1, (int) fx), y0 = juce::jmin (SH - 1, (int) fy);
        const int x1 = juce::jmin (SW - 1, x0 + 1), y1 = juce::jmin (SH - 1, y0 + 1);
        const float ax = fx - (float) x0, ay = fy - (float) y0;
        const float a = shadow[(size_t) y0 * SW + x0] + ax * (shadow[(size_t) y0 * SW + x1] - shadow[(size_t) y0 * SW + x0]);
        const float b = shadow[(size_t) y1 * SW + x0] + ax * (shadow[(size_t) y1 * SW + x1] - shadow[(size_t) y1 * SW + x0]);
        return a + ay * (b - a);
    };

    // ---- materials
    struct M { float r, g, b, pw, spec, metal; int aniso; };
    const float tr = tint.getFloatRed(), tg = tint.getFloatGreen(), tb = tint.getFloatBlue();
    const M table[] = {
        { 0, 0, 0, 1, 0, 0, 0 },                                   // Empty
        { tr * 0.70f, tg * 0.70f, tb * 0.70f, 12, 0.05f, 0.5f, 1 },  // Chassis: matte black anodized, fine brushing
        { tr * 1.05f, tg * 1.05f, tb * 1.05f, 26, 0.10f, 0.7f, 1 },  // Plate: satin black titanium, brushed
        { 0.30f, 0.31f, 0.34f, 90, 1.10f, 1.0f, 0 },               // Edge: polished dark-chrome chamfer
        { 0.012f, 0.012f, 0.014f, 10, 0.04f, 0.0f, 0 },            // Floor: carbon black
        { 0.004f, 0.005f, 0.007f, 260, 0.55f, 0.0f, 0 },           // Glass: smoked
        { 0.030f, 0.030f, 0.032f, 6, 0.03f, 0.0f, 0 },             // Rubber
        { 0.050f, 0.052f, 0.058f, 40, 0.50f, 0.85f, 0 },           // Knurl: black anodized
        { 0.095f, 0.098f, 0.108f, 42, 1.00f, 1.0f, 2 },            // Spun: black anodized aluminium face
        { tr * 1.35f, tg * 1.35f, tb * 1.35f, 34, 0.20f, 0.5f, 0 },  // Cap: anodized key cap
        { 0.16f, 0.165f, 0.18f, 140, 1.35f, 1.0f, 2 },             // Chrome: dark chrome, lathe-turned
        { 0.060f, 0.062f, 0.068f, 34, 0.85f, 0.9f, 2 },            // Titanium: satin black titanium, spun
        { 0.006f, 0.006f, 0.008f, 150, 1.40f, 0.0f, 0 },           // Obsidian: polished volcanic glass
    };

    const int oxp = juce::roundToInt (ox * s), oyp = juce::roundToInt (oy * s);   // absolute pixel offset
    std::vector<float> rowNoise ((size_t) H);
    for (int y = 0; y < H; ++y)
    {
        const int ay = y + oyp;
        rowNoise[(size_t) y] = hash01 ((uint32_t) ay * 2654435761U) * 0.55f
                             + hash01 ((uint32_t) (ay / juce::jmax (1, (int) (2 * s))) * 40503U + 7U) * 0.30f
                             + hash01 ((uint32_t) (ay / juce::jmax (1, (int) (9 * s))) * 9176U + 3U) * 0.15f;
    }

    juce::Image img (juce::Image::ARGB, W, H, true);
    juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
    const float hx = 0.5f;   // finite difference scale

    parallelRows (H, [&] (int rowBegin, int rowEnd) {
    for (int y = rowBegin; y < rowEnd; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const size_t i = (size_t) y * (size_t) W + (size_t) x;
            const float a = cover[i];
            if (a <= 0.0f) continue;
            const Mat mt = (Mat) mat[i];
            const M& m = table[mt == Empty ? Chassis : mt];

            // normal from the height field
            const float hl = h[(size_t) y * W + (size_t) juce::jmax (0, x - 1)], hr = h[(size_t) y * W + (size_t) juce::jmin (W - 1, x + 1)];
            const float hu = h[(size_t) juce::jmax (0, y - 1) * W + x], hd = h[(size_t) juce::jmin (H - 1, y + 1) * W + x];
            float nx = -(hr - hl) * hx, ny = -(hd - hu) * hx, nz = 1.0f;
            const float inv = 1.0f / std::sqrt (nx * nx + ny * ny + nz * nz);
            nx *= inv; ny *= inv; nz *= inv;

            const float occ = (b1[i] - h[i]) / s * 0.11f + (b2[i] - h[i]) / s * 0.040f;
            const float ao = juce::jlimit (0.18f, 1.0f, 1.0f - juce::jmax (0.0f, occ));
            const float sh = shadowAt (x, y);

            // albedo + micro detail
            float ar = m.r, ag = m.g, ab = m.b, specMul = 1.0f;
            const float px = hash01 ((uint32_t) ((x + oxp) * 73856093) ^ (uint32_t) ((y + oyp) * 19349663));
            if (m.aniso == 1)
            {
                const float streak = hash01 ((uint32_t) ((x + oxp) / juce::jmax (1, (int) (40 * s))) * 83492791U ^ (uint32_t) (y + oyp) * 2246822519U);
                const float g = 0.09f * (rowNoise[(size_t) y] - 0.5f) + 0.06f * (streak - 0.5f) + 0.05f * (px - 0.5f);
                ar *= 1.0f + g; ag *= 1.0f + g; ab *= 1.0f + g;
                specMul = 0.8f + 0.4f * rowNoise[(size_t) y];
            }
            else if (m.aniso == 2 && spin[i] != 0)
            {
                const auto c = spinCentres[spin[i]];
                const float rr = std::hypot (((float) x + 0.5f) / s + ox - c.x, ((float) y + 0.5f) / s + oy - c.y) * s;
                const float ring = hash01 ((uint32_t) (rr * 1.3f) * 2654435761U);
                ar *= 0.94f + 0.12f * ring; ag *= 0.94f + 0.12f * ring; ab *= 0.94f + 0.12f * ring;
                specMul = 0.65f + 0.7f * hash01 ((uint32_t) (rr * 0.7f) * 40503U + 3U);
            }
            else
            {
                const float g = 0.04f * (px - 0.5f);
                ar *= 1.0f + g; ag *= 1.0f + g; ab *= 1.0f + g;
            }

            // diffuse: sky ambient (occluded), key (shadowed), cool rim
            const float ndl = juce::jmax (0.0f, nx * Lx + ny * Ly + nz * Lz);
            const float ndr = juce::jmax (0.0f, nx * Rx + ny * Ry + nz * Rz);
            const float amb = 0.24f * (0.5f + 0.5f * nz) * ao;
            const float key = 1.10f * ndl * sh * (0.55f + 0.45f * ao);
            const float rim = 0.30f * ndr * ao;
            float cr = ar * (amb + key + rim * 0.85f), cg = ag * (amb + key + rim * 0.92f), cb = ab * (amb + key + rim * 1.10f);

            // specular: Blinn, or Kajiya-Kay along the machining direction
            auto specular = [&] (float lx, float ly, float lz)
            {
                float hxv = lx, hyv = ly, hzv = lz + 1.0f;
                const float hl2 = 1.0f / std::sqrt (hxv * hxv + hyv * hyv + hzv * hzv);
                hxv *= hl2; hyv *= hl2; hzv *= hl2;
                if (m.aniso == 0 || (m.aniso == 2 && spin[i] == 0))
                    return std::pow (juce::jmax (0.0f, nx * hxv + ny * hyv + nz * hzv), m.pw);
                float tx = 1.0f, ty = 0.0f;                          // brushed: grain runs horizontally
                if (m.aniso == 2)
                {
                    const auto c = spinCentres[spin[i]];
                    const float dx = ((float) x + 0.5f) / s + ox - c.x, dy = ((float) y + 0.5f) / s + oy - c.y;
                    const float dl = std::sqrt (dx * dx + dy * dy) + 1e-4f;
                    tx = -dy / dl; ty = dx / dl;                     // spun: grooves run around the centre
                }
                const float tdotn = tx * nx + ty * ny;
                float txo = tx - nx * tdotn, tyo = ty - ny * tdotn, tzo = -nz * tdotn;
                const float tl = 1.0f / std::sqrt (txo * txo + tyo * tyo + tzo * tzo + 1e-6f);
                const float th = (txo * hxv + tyo * hyv + tzo * hzv) * tl;
                return std::pow (std::sqrt (juce::jmax (0.0f, 1.0f - th * th)), m.pw * 1.6f);
            };
            const float spec = m.spec * specMul * (specular (Lx, Ly, Lz) * sh + 0.45f * specular (Rx, Ry, Rz) * ao);
            const float mx = juce::jmax (ar, juce::jmax (ag, ab)) + 1e-4f;
            const float sr = 1.0f + m.metal * (ar / mx - 1.0f), sgc = 1.0f + m.metal * (ag / mx - 1.0f), sbc = 1.0f + m.metal * (ab / mx - 1.0f);
            cr += spec * sr; cg += spec * sgc; cb += spec * sbc * 1.02f;

            // gentle highlight roll-off
            auto tone = [] (float v) { return v / (1.0f + 0.32f * v) * 1.12f; };
            auto to8 = [&] (float v) { return (uint8_t) juce::jlimit (0, 255, juce::roundToInt (tone (v) * 255.0f * a)); };
            auto* p = (juce::PixelARGB*) data.getPixelPointer (x, y);
            p->setARGB ((uint8_t) juce::roundToInt (a * 255.0f), to8 (cr), to8 (cg), to8 (cb));
        }
    }
    });
    return img;
}

//==============================================================================
// Light helpers
void blur (juce::Image& img, int radius, int passes)
{
    if (radius < 1 || ! img.isValid()) return;
    const int W = img.getWidth(), H = img.getHeight();
    juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
    std::vector<std::array<float, 4>> buf ((size_t) W * (size_t) H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const auto* p = (const juce::PixelARGB*) d.getPixelPointer (x, y);
            buf[(size_t) y * W + x] = { (float) p->getAlpha(), (float) p->getRed(), (float) p->getGreen(), (float) p->getBlue() };
        }
    std::vector<std::array<float, 4>> tmp ((size_t) std::max (W, H));
    const float norm = 1.0f / (float) (2 * radius + 1);
    for (int pass = 0; pass < passes; ++pass)
    {
        for (int y = 0; y < H; ++y)
        {
            std::array<float, 4> acc {};
            auto at = [&] (int x) -> const std::array<float, 4>& { return buf[(size_t) y * W + (size_t) juce::jlimit (0, W - 1, x)]; };
            for (int x = -radius; x <= radius; ++x) for (int c = 0; c < 4; ++c) acc[(size_t) c] += at (x)[(size_t) c];
            for (int x = 0; x < W; ++x)
            {
                for (int c = 0; c < 4; ++c) tmp[(size_t) x][(size_t) c] = acc[(size_t) c] * norm;
                const auto& add = at (x + radius + 1); const auto& sub = at (x - radius);
                for (int c = 0; c < 4; ++c) acc[(size_t) c] += add[(size_t) c] - sub[(size_t) c];
            }
            for (int x = 0; x < W; ++x) buf[(size_t) y * W + x] = tmp[(size_t) x];
        }
        for (int x = 0; x < W; ++x)
        {
            std::array<float, 4> acc {};
            auto at = [&] (int y) -> const std::array<float, 4>& { return buf[(size_t) juce::jlimit (0, H - 1, y) * W + (size_t) x]; };
            for (int y = -radius; y <= radius; ++y) for (int c = 0; c < 4; ++c) acc[(size_t) c] += at (y)[(size_t) c];
            for (int y = 0; y < H; ++y)
            {
                for (int c = 0; c < 4; ++c) tmp[(size_t) y][(size_t) c] = acc[(size_t) c] * norm;
                const auto& add = at (y + radius + 1); const auto& sub = at (y - radius);
                for (int c = 0; c < 4; ++c) acc[(size_t) c] += add[(size_t) c] - sub[(size_t) c];
            }
            for (int y = 0; y < H; ++y) buf[(size_t) y * W + x] = tmp[(size_t) y];
        }
    }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const auto& v = buf[(size_t) y * W + x];
            auto* p = (juce::PixelARGB*) d.getPixelPointer (x, y);
            const auto a = (uint8_t) juce::jlimit (0, 255, juce::roundToInt (v[0]));
            p->setARGB (a, (uint8_t) juce::jlimit (0, (int) a, juce::roundToInt (v[1])),
                        (uint8_t) juce::jlimit (0, (int) a, juce::roundToInt (v[2])), (uint8_t) juce::jlimit (0, (int) a, juce::roundToInt (v[3])));
        }
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float bloom, float bloomAlpha)
{
    juce::Path thick;
    juce::PathStrokeType (width * 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (thick, p);
    juce::DropShadow (c.withAlpha (bloomAlpha), juce::roundToInt (bloom), {}).drawForPath (g, thick);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.6f).withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (juce::jmax (0.6f, width * 0.38f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void led (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
{
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.fillEllipse (c.x - r - 1.2f, c.y - r - 1.2f, (r + 1.2f) * 2.0f, (r + 1.2f) * 2.0f);
    juce::Path dome;
    dome.addEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    if (on)
    {
        juce::DropShadow (col.withAlpha (0.9f), juce::roundToInt (r * 3.2f), {}).drawForPath (g, dome);
        g.setGradientFill (juce::ColourGradient (col.interpolatedWith (juce::Colours::white, 0.75f), c.x - r * 0.2f, c.y - r * 0.3f,
                                                 col, c.x + r, c.y + r, true));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (col.withMultipliedBrightness (0.28f), c.x, c.y - r, juce::Colour (0xff070708), c.x, c.y + r, false));
    }
    g.fillPath (dome);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.75f : 0.22f));
    g.fillEllipse (c.x - r * 0.5f, c.y - r * 0.62f, r * 0.55f, r * 0.38f);
}

void lens (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c, float strength)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    juce::DropShadow (c.withAlpha (0.40f * strength), 9, {}).drawForPath (g, shape);
    // diffused backlight: brighter at the top where the light pipe enters
    g.setGradientFill (juce::ColourGradient (c.interpolatedWith (juce::Colours::white, 0.12f).withAlpha (strength), 0.0f, r.getY(),
                                             c.withMultipliedBrightness (0.62f).withAlpha (strength), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    // hot spot
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f * strength), r.getCentreX(), r.getY() + r.getHeight() * 0.25f,
                                             juce::Colours::white.withAlpha (0.0f), r.getCentreX(), r.getBottom(), true));
    g.fillPath (shape);
    // lens edge: bright upper lip, dark lower lip (it is a moulded piece sitting in the cap)
    g.setColour (juce::Colours::white.withAlpha (0.38f * strength));
    g.drawLine (r.getX() + radius, r.getY() + 0.75f, r.getRight() - radius, r.getY() + 0.75f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void ledMeter (juce::Graphics& g, juce::Rectangle<float> r, int n, float level, float peak)
{
    const float gap = 2.0f;
    const float w = (r.getWidth() - gap * (float) (n - 1)) / (float) n;
    const int peakSeg = juce::jlimit (-1, n - 1, (int) std::floor (peak * (float) n) - 1);
    int litCount = 0;
    for (int i = 0; i < n; ++i)
    {
        const float pos = ((float) i + 0.5f) / (float) n;
        const juce::Colour c = pos < 0.70f ? juce::Colour (0xff2fe3ff) : (pos < 0.88f ? juce::Colour (0xffa35cff) : juce::Colour (0xffff2f9f));
        const bool lit = pos < level || i == peakSeg;
        if (lit) ++litCount;
        const juce::Rectangle<float> seg (r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight());
        if (lit)
        {
            g.setColour (c.withAlpha (0.22f));
            g.fillRoundedRectangle (seg.expanded (1.5f), 2.0f);
            g.setColour (c);
            g.fillRoundedRectangle (seg, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (seg.withHeight (seg.getHeight() * 0.35f).reduced (0.5f, 0.0f).translated (0.0f, 1.0f));
        }
        else
        {
            // unlit LED: dark coloured lens with a faint top reflection
            g.setColour (c.withMultipliedBrightness (0.22f).withAlpha (0.85f));
            g.fillRoundedRectangle (seg, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (seg.withHeight (seg.getHeight() * 0.3f).reduced (0.5f, 0.0f).translated (0.0f, 1.0f));
        }
    }
}

void knobPointer (juce::Graphics& g, juce::Point<float> c, float D, float angle, juce::Colour light)
{
    // a slot machined into the titanium ring with a light guide inside it
    const float R = D * 0.5f;
    const juce::Point<float> d (std::sin (angle), -std::cos (angle));
    const juce::Line<float> line (c + d * (R * 0.635f), c + d * (R * 0.825f));
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawLine (line, juce::jmax (3.2f, R * 0.10f));
    juce::Path core;
    core.startNewSubPath (line.getStart());
    core.lineTo (line.getEnd());
    juce::Path thick;
    juce::PathStrokeType (juce::jmax (2.0f, R * 0.06f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (thick, core);
    juce::DropShadow (light.withAlpha (0.85f), juce::roundToInt (juce::jmax (4.0f, R * 0.18f)), {}).drawForPath (g, thick);
    g.setColour (light);
    g.strokePath (core, juce::PathStrokeType (juce::jmax (1.6f, R * 0.05f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.strokePath (core, juce::PathStrokeType (juce::jmax (0.6f, R * 0.018f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void knobRing (juce::Graphics& g, juce::Point<float> c, float D, float v, juce::Colour light)
{
    // light rising from the gap between knob and mount wall
    const float R = D * 0.5f, rr = R * 1.075f;
    const float a0 = -2.35619f, a1 = 2.35619f, av = a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, v);
    juce::Path track;
    track.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
    g.setColour (light.withAlpha (0.08f));
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (v <= 0.002f) return;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, av, true);
    glowStroke (g, arc, light, 1.7f, 7.0f, 0.6f);
}

void knobScale (juce::Graphics& g, juce::Point<float> c, float D, bool numbers)
{
    const float R = D * 0.5f;
    const float a0 = -2.35619f, a1 = 2.35619f;
    const int ticks = 41;
    for (int i = 0; i < ticks; ++i)
    {
        const bool major = i % 4 == 0;
        const float a = a0 + (a1 - a0) * (float) i / (float) (ticks - 1);
        const juce::Point<float> d (std::sin (a), -std::cos (a));
        const float r1 = R * 1.25f, r2 = R * (major ? 1.35f : 1.30f);
        g.setColour (colours::text.withAlpha (major ? 0.55f : 0.20f));
        g.drawLine (juce::Line<float> (c + d * r1, c + d * r2), major ? 1.1f : 0.8f);
    }
    if (! numbers) return;
    g.setFont (label (juce::jmax (8.5f, R * 0.24f)));
    for (int k = 0; k <= 10; ++k)
    {
        const float a = a0 + (a1 - a0) * (float) k / 10.0f;
        const juce::Point<float> p = c + juce::Point<float> (std::sin (a), -std::cos (a)) * (R * 1.52f);
        g.setColour (colours::textDim.withAlpha (k % 5 == 0 ? 0.9f : 0.5f));
        g.drawText (juce::String (k), juce::Rectangle<float> (p.x - 10.0f, p.y - 7.0f, 20.0f, 14.0f), juce::Justification::centred, false);
    }
}

juce::Image faderCap (int w, int h, float scale)
{
    Relief rel (w, h, scale * 2.0f, juce::Colour (0xff2a2b2f), true);
    const juce::Rectangle<float> r (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f);
    rel.plate (r, 2.5f, 7.0f, 2.2f, Relief::Cap, Relief::Edge);
    for (float yy : { 0.38f, 0.5f, 0.62f })
        rel.grooveLine ({ r.getX() + 2.5f, r.getY() + r.getHeight() * yy }, { r.getRight() - 2.5f, r.getY() + r.getHeight() * yy }, 1.0f, 1.2f);
    auto img = rel.render();
    return img.rescaled (juce::roundToInt ((float) w * scale), juce::roundToInt ((float) h * scale), juce::Graphics::highResamplingQuality);
}

const juce::Image& keyCap (int w, int h, float scale)
{
    static std::map<juce::String, juce::Image> cache;
    const juce::String key = juce::String (w) + "x" + juce::String (h) + "@" + juce::String (scale, 2);
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;

    const int m = kCapMargin;
    const int TW = w + 2 * m, TH = h + 2 * m;
    const float ss = scale * 2.0f;
    Relief rel (TW, TH, ss, juce::Colour (0xff1c1d20), true);
    const juce::Rectangle<float> r ((float) m, (float) m, (float) w, (float) h);
    const float radius = juce::jmin (5.0f, (float) juce::jmin (w, h) * 0.16f);
    rel.plate (r, radius, 6.0f, 2.0f, Relief::Cap, Relief::Edge);
    // shallow dish in the cap top: real keys are slightly concave
    rel.recess (r.reduced (3.5f), juce::jmax (1.0f, radius - 2.0f), 0.8f, 3.0f, Relief::Cap, Relief::Cap);
    auto cap = rel.render();

    // shadows from the cap's silhouette: tight contact shadow + softer cast shadow away from the light
    juce::Image shadow (juce::Image::ARGB, cap.getWidth(), cap.getHeight(), true);
    {
        juce::Graphics g (shadow);
        juce::Path p;
        p.addRoundedRectangle (r.toFloat() * ss, radius * ss);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (p, juce::AffineTransform::translation (1.5f * ss, 4.5f * ss));
    }
    blur (shadow, juce::roundToInt (3.0f * ss), 3);
    juce::Image contact (juce::Image::ARGB, cap.getWidth(), cap.getHeight(), true);
    {
        juce::Graphics g (contact);
        juce::Path p;
        p.addRoundedRectangle ((r.expanded (0.6f)) * ss, radius * ss);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.fillPath (p, juce::AffineTransform::translation (0.3f * ss, 1.0f * ss));
    }
    blur (contact, juce::roundToInt (0.9f * ss), 2);

    juce::Image out (juce::Image::ARGB, cap.getWidth(), cap.getHeight(), true);
    {
        juce::Graphics g (out);
        g.drawImageAt (shadow, 0, 0);
        g.drawImageAt (contact, 0, 0);
        g.drawImageAt (cap, 0, 0);
    }
    auto finalImg = out.rescaled (juce::roundToInt ((float) TW * scale), juce::roundToInt ((float) TH * scale), juce::Graphics::highResamplingQuality);
    return cache.emplace (key, finalImg).first->second;
}

juce::Image glassSheen (int w, int h, float radius)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Graphics g (img);
    const float fw = (float) w, fh = (float) h;
    juce::Path clip;
    clip.addRoundedRectangle (0.0f, 0.0f, fw, fh, radius);
    g.reduceClipRegion (clip);
    // broad reflection of the studio softbox across the upper glass
    juce::Path refl;
    refl.startNewSubPath (0.0f, 0.0f);
    refl.lineTo (fw * 0.58f, 0.0f);
    refl.quadraticTo (fw * 0.32f, fh * 0.22f, 0.0f, fh * 0.42f);
    refl.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.050f), 0.0f, 0.0f,
                                             juce::Colours::white.withAlpha (0.004f), fw * 0.35f, fh * 0.35f, false));
    g.fillPath (refl);
    // thin specular line where the glass meets the bezel
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0.0f, 0.0f, juce::Colours::white.withAlpha (0.0f), 0.0f, 3.0f, false));
    g.fillRect (0.0f, 0.0f, fw, 3.0f);
    return img;
}
//==============================================================================
// SWEEP BAR logotype
//
// Monoline geometric letters on a cap-height grid: square counters, 45-degree chamfers instead of curves,
// bevelled joins (every corner is a cut, never a point), detached crossbars that float off the stem.
// A precision slit runs through the whole name at mid-height: that is the "sweep bar", and it carries light.
Wordmark wordmark (juce::Point<float> origin, float H)
{
    Wordmark w;
    const float u = H / 21.0f;               // design unit
    const float t = 2.5f * u;                // stroke
    const float i = t * 0.5f;                // centreline inset
    const float ch = 4.2f * u;               // chamfer
    const float m = H * 0.5f;                // mid line
    const float track = 6.0f * u;
    const float space = 15.0f * u;

    struct Glyph { float width; std::function<void (juce::Path&, float)> draw; };
    auto poly = [] (juce::Path& p, std::initializer_list<juce::Point<float>> pts, float x0)
    {
        bool first = true;
        for (auto q : pts) { if (first) p.startNewSubPath (q.x + x0, q.y); else p.lineTo (q.x + x0, q.y); first = false; }
    };
    using P = juce::Point<float>;
    const float wS = 15.5f * u, wW = 24.0f * u, wE = 13.5f * u, wP = 15.0f * u, wB = 15.5f * u, wA = 16.0f * u, wR = 15.5f * u;

    std::vector<Glyph> glyphs {
        { wS, [&] (juce::Path& p, float x) { poly (p, { P (wS, i), P (i + ch, i), P (i, i + ch), P (i, m - ch * 0.5f), P (i + ch * 0.5f, m),
                                                       P (wS - i - ch * 0.5f, m), P (wS - i, m + ch * 0.5f), P (wS - i, H - i - ch), P (wS - i - ch, H - i), P (0.0f, H - i) }, x); } },
        { wW, [&] (juce::Path& p, float x) { poly (p, { P (i, 0.0f), P (i, H - i - ch), P (i + ch, H - i), P (wW - i - ch, H - i), P (wW - i, H - i - ch), P (wW - i, 0.0f) }, x);
                                            poly (p, { P (wW * 0.5f, H * 0.30f), P (wW * 0.5f, H - i) }, x); } },
        { wE, [&] (juce::Path& p, float x) { poly (p, { P (wE, i), P (i + ch * 0.6f, i), P (i, i + ch * 0.6f), P (i, H - i - ch * 0.6f), P (i + ch * 0.6f, H - i), P (wE, H - i) }, x);
                                            poly (p, { P (t + 2.4f * u, m), P (wE - 1.0f * u, m) }, x); } },
        { wE, [&] (juce::Path& p, float x) { poly (p, { P (wE, i), P (i + ch * 0.6f, i), P (i, i + ch * 0.6f), P (i, H - i - ch * 0.6f), P (i + ch * 0.6f, H - i), P (wE, H - i) }, x);
                                            poly (p, { P (t + 2.4f * u, m), P (wE - 1.0f * u, m) }, x); } },
        { wP, [&] (juce::Path& p, float x) { poly (p, { P (i, H), P (i, i), P (wP - i - ch, i), P (wP - i, i + ch), P (wP - i, m - ch * 0.6f),
                                                       P (wP - i - ch * 0.6f, m), P (t + 2.4f * u, m) }, x); } },
        { space, [] (juce::Path&, float) {} },
        { wB, [&] (juce::Path& p, float x) { poly (p, { P (i, H - i), P (i, i), P (wB - 1.2f * u - i - ch * 0.8f, i), P (wB - 1.2f * u - i, i + ch * 0.8f),
                                                       P (wB - 1.2f * u - i, m - ch * 0.5f), P (wB - 1.2f * u - i - ch * 0.5f, m), P (t + 2.4f * u, m) }, x);
                                            poly (p, { P (t + 2.4f * u, m), P (wB - i - ch * 0.5f, m), P (wB - i, m + ch * 0.5f), P (wB - i, H - i - ch * 0.8f),
                                                       P (wB - i - ch * 0.8f, H - i), P (i, H - i) }, x); } },
        { wA, [&] (juce::Path& p, float x) { poly (p, { P (i, H), P (i, i + ch), P (i + ch, i), P (wA - i - ch, i), P (wA - i, i + ch), P (wA - i, H) }, x);
                                            poly (p, { P (t + 2.4f * u, m), P (wA - t - 2.4f * u, m) }, x); } },
        { wR, [&] (juce::Path& p, float x) { poly (p, { P (i, H), P (i, i), P (wR - i - ch, i), P (wR - i, i + ch), P (wR - i, m - ch * 0.6f),
                                                       P (wR - i - ch * 0.6f, m), P (t + 2.4f * u, m) }, x);
                                            poly (p, { P (wR - 6.0f * u, m + t * 0.5f), P (wR - i, H - i - ch * 0.4f), P (wR - i, H) }, x); } },
    };

    const juce::PathStrokeType stroke (t, juce::PathStrokeType::beveled, juce::PathStrokeType::butt);
    float x = 0.0f;
    for (size_t g = 0; g < glyphs.size(); ++g)
    {
        juce::Path centre;
        glyphs[g].draw (centre, x);
        if (! centre.isEmpty())
        {
            juce::Path outline;
            stroke.createStrokedPath (outline, centre);
            outline.applyTransform (juce::AffineTransform::translation (origin.x, origin.y));
            w.all.addPath (outline);
            w.letters.push_back (outline);
        }
        x += glyphs[g].width + (g + 1 < glyphs.size() && glyphs[g].width != space && glyphs[g + 1].width != space ? track : 0.0f);
    }
    w.bounds = juce::Rectangle<float> (origin.x, origin.y, x, H);
    w.slitY = origin.y + m;
    w.slitHeight = 1.2f * u;
    w.slitX0 = origin.x - 10.0f * u;
    w.slitX1 = origin.x + x + 26.0f * u;
    return w;
}
} // namespace skin
