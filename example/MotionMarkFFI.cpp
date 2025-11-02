#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#if defined(_WIN32)
    #define MM_EXPORT extern "C" __declspec(dllexport)
#else
    #define MM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// The public MotionMark surface wrapper that consumers interact with.
struct MotionMarkSurface {
    sk_sp<SkSurface> surface;
    SkCanvas* canvas = nullptr;
    SkImageInfo info;
};

// Minimal POD types mirroring the previous SkiaSharp C API structs/enums the FFI exposed.
struct sk_rect_t {
    float left;
    float top;
    float right;
    float bottom;
};

using sk_color_t = SkColor;

enum sk_clipop_t : int {
    SK_CLIPOP_DIFFERENCE = static_cast<int>(SkClipOp::kDifference),
    SK_CLIPOP_INTERSECT = static_cast<int>(SkClipOp::kIntersect),
};

enum sk_stroke_cap_t : int {
    SK_STROKE_CAP_BUTT = static_cast<int>(SkPaint::kButt_Cap),
    SK_STROKE_CAP_ROUND = static_cast<int>(SkPaint::kRound_Cap),
    SK_STROKE_CAP_SQUARE = static_cast<int>(SkPaint::kSquare_Cap),
};

enum sk_stroke_join_t : int {
    SK_STROKE_JOIN_MITER = static_cast<int>(SkPaint::kMiter_Join),
    SK_STROKE_JOIN_ROUND = static_cast<int>(SkPaint::kRound_Join),
    SK_STROKE_JOIN_BEVEL = static_cast<int>(SkPaint::kBevel_Join),
};

enum sk_paint_style_t : int {
    SK_PAINT_STYLE_FILL = static_cast<int>(SkPaint::kFill_Style),
    SK_PAINT_STYLE_STROKE = static_cast<int>(SkPaint::kStroke_Style),
    SK_PAINT_STYLE_STROKE_AND_FILL = static_cast<int>(SkPaint::kStrokeAndFill_Style),
};

// Lightweight wrappers that provide a stable ABI while delegating to C++ Skia objects.
struct sk_paint_t {
    SkPaint paint;
};

struct sk_path_t {
    SkPathBuilder builder;
};

using sk_canvas_t = SkCanvas;

// Helper utilities -----------------------------------------------------------------------------

static SkRect to_rect(const sk_rect_t* rect) {
    if (!rect) {
        return SkRect::MakeEmpty();
    }
    return SkRect::MakeLTRB(rect->left, rect->top, rect->right, rect->bottom);
}

static bool recreate_surface(MotionMarkSurface* wrapper, int width, int height) {
    if (!wrapper || width <= 0 || height <= 0) {
        return false;
    }

    SkImageInfo info = SkImageInfo::Make(width,
                                         height,
                                         kBGRA_8888_SkColorType,
                                         kPremul_SkAlphaType,
                                         nullptr);

    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) {
        return false;
    }

    wrapper->surface = std::move(surface);
    wrapper->canvas = wrapper->surface->getCanvas();
    wrapper->info = info;
    return wrapper->canvas != nullptr;
}

static SkClipOp to_clipop(sk_clipop_t op) {
    switch (op) {
        case SK_CLIPOP_DIFFERENCE:
            return SkClipOp::kDifference;
        case SK_CLIPOP_INTERSECT:
        default:
            return SkClipOp::kIntersect;
    }
}

static SkPaint::Cap to_cap(sk_stroke_cap_t cap) {
    switch (cap) {
        case SK_STROKE_CAP_ROUND:
            return SkPaint::kRound_Cap;
        case SK_STROKE_CAP_SQUARE:
            return SkPaint::kSquare_Cap;
        case SK_STROKE_CAP_BUTT:
        default:
            return SkPaint::kButt_Cap;
    }
}

static SkPaint::Join to_join(sk_stroke_join_t join) {
    switch (join) {
        case SK_STROKE_JOIN_ROUND:
            return SkPaint::kRound_Join;
        case SK_STROKE_JOIN_BEVEL:
            return SkPaint::kBevel_Join;
        case SK_STROKE_JOIN_MITER:
        default:
            return SkPaint::kMiter_Join;
    }
}

static SkPaint::Style to_style(sk_paint_style_t style) {
    switch (style) {
        case SK_PAINT_STYLE_STROKE:
            return SkPaint::kStroke_Style;
        case SK_PAINT_STYLE_STROKE_AND_FILL:
            return SkPaint::kStrokeAndFill_Style;
        case SK_PAINT_STYLE_FILL:
        default:
            return SkPaint::kFill_Style;
    }
}

// Surface management ---------------------------------------------------------------------------

MM_EXPORT MotionMarkSurface* mm_surface_create(int width, int height) {
    auto wrapper = std::make_unique<MotionMarkSurface>();
    if (!recreate_surface(wrapper.get(), width, height)) {
        return nullptr;
    }
    return wrapper.release();
}

MM_EXPORT bool mm_surface_resize(MotionMarkSurface* wrapper, int width, int height) {
    if (!wrapper) {
        return false;
    }
    if (wrapper->info.width() == width && wrapper->info.height() == height) {
        return true;
    }
    return recreate_surface(wrapper, width, height);
}

MM_EXPORT void mm_surface_destroy(MotionMarkSurface* wrapper) {
    delete wrapper;
}

MM_EXPORT sk_canvas_t* mm_surface_get_canvas(MotionMarkSurface* wrapper) {
    return wrapper ? wrapper->canvas : nullptr;
}

MM_EXPORT bool mm_surface_read_pixels(MotionMarkSurface* wrapper,
                                      void* dstPixels,
                                      size_t dstRowBytes) {
    if (!wrapper || !dstPixels || dstRowBytes == 0) {
        return false;
    }
    return wrapper->surface->readPixels(wrapper->info, dstPixels, dstRowBytes, 0, 0);
}

MM_EXPORT int mm_surface_width(MotionMarkSurface* wrapper) {
    return wrapper ? wrapper->info.width() : 0;
}

MM_EXPORT int mm_surface_height(MotionMarkSurface* wrapper) {
    return wrapper ? wrapper->info.height() : 0;
}

// Canvas helpers -------------------------------------------------------------------------------

MM_EXPORT void mm_sk_canvas_save(sk_canvas_t* canvas) {
    if (canvas) {
        canvas->save();
    }
}

MM_EXPORT void mm_sk_canvas_restore(sk_canvas_t* canvas) {
    if (canvas) {
        canvas->restore();
    }
}

MM_EXPORT void mm_sk_canvas_translate(sk_canvas_t* canvas, float dx, float dy) {
    if (canvas) {
        canvas->translate(dx, dy);
    }
}

MM_EXPORT void mm_sk_canvas_clip_rect_with_operation(sk_canvas_t* canvas,
                                                     const sk_rect_t* rect,
                                                     sk_clipop_t operation,
                                                     bool doAA) {
    if (canvas && rect) {
        canvas->clipRect(to_rect(rect), to_clipop(operation), doAA);
    }
}

MM_EXPORT void mm_sk_canvas_clear(sk_canvas_t* canvas, sk_color_t color) {
    if (canvas) {
        canvas->clear(color);
    }
}

MM_EXPORT void mm_sk_canvas_draw_path(sk_canvas_t* canvas,
                                      sk_path_t* path,
                                      const sk_paint_t* paint) {
    if (canvas && path && paint) {
        canvas->drawPath(path->builder.snapshot(), paint->paint);
    }
}

// Path helpers ---------------------------------------------------------------------------------

MM_EXPORT sk_path_t* mm_sk_path_new() {
    return new sk_path_t();
}

MM_EXPORT void mm_sk_path_delete(sk_path_t* path) {
    delete path;
}

MM_EXPORT void mm_sk_path_reset(sk_path_t* path) {
    if (path) {
        path->builder.reset();
    }
}

MM_EXPORT void mm_sk_path_move_to(sk_path_t* path, float x, float y) {
    if (path) {
        path->builder.moveTo(SkPoint::Make(x, y));
    }
}

MM_EXPORT void mm_sk_path_line_to(sk_path_t* path, float x, float y) {
    if (path) {
        path->builder.lineTo(SkPoint::Make(x, y));
    }
}

MM_EXPORT void mm_sk_path_quad_to(sk_path_t* path,
                                  float x0,
                                  float y0,
                                  float x1,
                                  float y1) {
    if (path) {
        path->builder.quadTo(SkPoint::Make(x0, y0), SkPoint::Make(x1, y1));
    }
}

MM_EXPORT void mm_sk_path_cubic_to(sk_path_t* path,
                                   float x0,
                                   float y0,
                                   float x1,
                                   float y1,
                                   float x2,
                                   float y2) {
    if (path) {
        path->builder.cubicTo(SkPoint::Make(x0, y0),
                              SkPoint::Make(x1, y1),
                              SkPoint::Make(x2, y2));
    }
}

// Paint helpers --------------------------------------------------------------------------------

MM_EXPORT sk_paint_t* mm_sk_paint_new() {
    return new sk_paint_t();
}

MM_EXPORT void mm_sk_paint_delete(sk_paint_t* paint) {
    delete paint;
}

MM_EXPORT void mm_sk_paint_set_antialias(sk_paint_t* paint, bool antialias) {
    if (paint) {
        paint->paint.setAntiAlias(antialias);
    }
}

MM_EXPORT void mm_sk_paint_set_style(sk_paint_t* paint, sk_paint_style_t style) {
    if (paint) {
        paint->paint.setStyle(to_style(style));
    }
}

MM_EXPORT void mm_sk_paint_set_color(sk_paint_t* paint, sk_color_t color) {
    if (paint) {
        paint->paint.setColor(color);
    }
}

MM_EXPORT void mm_sk_paint_set_stroke_width(sk_paint_t* paint, float width) {
    if (paint) {
        paint->paint.setStrokeWidth(width);
    }
}

MM_EXPORT void mm_sk_paint_set_stroke_cap(sk_paint_t* paint, sk_stroke_cap_t cap) {
    if (paint) {
        paint->paint.setStrokeCap(to_cap(cap));
    }
}

MM_EXPORT void mm_sk_paint_set_stroke_join(sk_paint_t* paint, sk_stroke_join_t join) {
    if (paint) {
        paint->paint.setStrokeJoin(to_join(join));
    }
}
