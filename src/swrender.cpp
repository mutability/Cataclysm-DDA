#include "swrender.h"

#include <SDL_render.h>
#include <SDL_gpu_RendererImpl.h>

// Software renderer implementation for SDL_gpu on top of SDL_render.

// There is exactly one GPU_Context for each window.
// The associated SW_CONTEXT_DATA holds the SDL_Renderer for that window.
// SW_CONTEXT_DATA->render_target holds the GPU_Target that is the current
// render target for that SDL_Renderer
//
// There is exactly one GPU_Target with context != NULL for each window;
// this is the target that renders to the window. The associated GPU_Context
// is the context for the window (see above)
//
// There are zero or more other GPU_Targets created via GPU_LoadTarget.
// These have image != NULL (they render to an image) and have context = NULL,
// context_target = the window target
//
// GPU_Renderer->current_context_target points to the GPU_Target for the
// window that's currently in use (e.g. for later image loads)


typedef struct {
    SDL_Renderer *sdl_renderer;
    GPU_Target *render_target;
} SW_CONTEXT_DATA;

typedef struct {
    int refcount;
    Uint32 sdl_format;
    SDL_Texture *tex;
} SW_IMAGE_DATA;

static void unimplemented(const char *fn, ...)
{
    GPU_PushErrorCode(fn, GPU_ERROR_UNSUPPORTED_FUNCTION, "Not implemented");
}

static void unimplemented_warn(const char *fn, ...)
{
    GPU_LogWarning("%s is not implemented\n", fn);
}

static inline SW_CONTEXT_DATA *CONTEXT_DATA(GPU_Context *context)
{
    return context ? (SW_CONTEXT_DATA*) context->data : NULL;
}

static inline SW_IMAGE_DATA *IMAGE_DATA(GPU_Image *image)
{
    return image ? (SW_IMAGE_DATA*) image->data : NULL;
}

static inline GPU_Context *CONTEXT(GPU_Target *target)
{
    return target ? target->context_target->context : NULL;
}

static inline GPU_Context *CONTEXT(GPU_Image *image)
{
    return image ? image->context_target->context : NULL;
}

static inline SDL_Renderer *SDL_RENDERER(GPU_Context *context)
{
    return context ? CONTEXT_DATA(context)->sdl_renderer : NULL;
}

static inline SDL_Renderer *SDL_RENDERER(GPU_Target *target)
{
    return SDL_RENDERER(CONTEXT(target));
}

static inline SDL_Renderer *SDL_RENDERER(GPU_Image *image)
{
    return SDL_RENDERER(CONTEXT(image));
}

static inline SDL_Renderer *setRenderTarget(GPU_Renderer* renderer, GPU_Target *target)
{
    (void) renderer;

    SDL_Renderer *sdlr = SDL_RENDERER(target);
    if (CONTEXT_DATA(target->context_target->context)->render_target != target) {        
        CONTEXT_DATA(target->context_target->context)->render_target = target;
        
        if (target->image) {
            SDL_SetRenderTarget(sdlr, IMAGE_DATA(target->image)->tex);
        } else {
            SDL_SetRenderTarget(sdlr, NULL);
        }
    }

    return sdlr;
}

///////////////

static GPU_Target* SDLCALL swrender_Init(GPU_Renderer* renderer, GPU_RendererID renderer_request, Uint16 w, Uint16 h, GPU_WindowFlagEnum SDL_flags)
{
    (void) renderer_request;
    
    SDL_Window *window = SDL_GetWindowFromID(GPU_GetInitWindow());
    if (!window) {
        window = SDL_CreateWindow("",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  w, h,
                                  SDL_flags);
    }
        
    return renderer->impl->CreateTargetFromWindow(renderer, GPU_GetInitWindow(), NULL);
}

static GPU_Target* SDLCALL swrender_CreateTargetFromWindow(GPU_Renderer* renderer, Uint32 windowID, GPU_Target* target)
{
    if (target) {
        GPU_PushErrorCode("GPU_CreateTargetFromWindow", GPU_ERROR_UNSUPPORTED_FUNCTION, "reinitializing target not supported");
        return NULL;
    }

    if (renderer->current_context_target) {
        GPU_PushErrorCode("GPU_CreateTargetFromWindow", GPU_ERROR_UNSUPPORTED_FUNCTION, "multiple windows not supported");
        return NULL;
    }
    
    SDL_Window *window = SDL_GetWindowFromID(windowID);
    if (!window) {
        GPU_PushErrorCode("GPU_CreateTargetFromWindow", GPU_ERROR_BACKEND_ERROR, "Failed to acquire the window from the given ID.");
        return NULL;
    }

    SDL_Renderer *sdlr = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_SOFTWARE);
    if (!sdlr) {
        GPU_PushErrorCode("GPU_CreateTargetFromWindow", GPU_ERROR_BACKEND_ERROR, "Failed to initialize the SDL renderer: %s", SDL_GetError());
        return NULL;
    }
    
    target = (GPU_Target*)SDL_malloc(sizeof(GPU_Target));
    GPU_Context *context = (GPU_Context *)SDL_malloc(sizeof(GPU_Context));
    SW_CONTEXT_DATA *context_data = (SW_CONTEXT_DATA *)SDL_malloc(sizeof(SW_CONTEXT_DATA));
    
    if (!target || !context || !context_data) {
        SDL_free(target);
        SDL_free(context);
        SDL_free(context_data);
        return NULL;
    }

    int ww, hh;
    SDL_GetRendererOutputSize(sdlr, &ww, &hh);

    context->data = context_data;
    context_data->sdl_renderer = sdlr;
    context_data->render_target = target;
    context->windowID = windowID;
    context->window_w = context->drawable_w = ww;
    context->window_h = context->drawable_h = hh;
    
    target->renderer = renderer;
    target->context_target = target;
    target->image = NULL;
    target->data = NULL;

    target->w = (Uint16)ww;
    target->h = (Uint16)hh;
    target->using_virtual_resolution = GPU_FALSE;
    target->base_w = (Uint16)ww;
    target->base_h = (Uint16)hh;
    target->use_clip_rect = GPU_FALSE;
    target->use_color = GPU_FALSE;
    target->viewport.x = target->viewport.y = 0;
    target->viewport.w = ww;
    target->viewport.h = hh;
    target->use_camera = GPU_FALSE;

    target->context = context;
    
    target->refcount = 1;
    target->is_alias = GPU_FALSE;

    renderer->current_context_target = target;
    
    return target;
}

static GPU_Target* SDLCALL swrender_CreateAliasTarget(GPU_Renderer* renderer , GPU_Target* target)
{
    unimplemented("GPU_CreateAliasTarget", renderer, target);
    return NULL;
}

static void SDLCALL swrender_MakeCurrent(GPU_Renderer* renderer, GPU_Target* target, Uint32 windowID)
{
    (void) windowID;
    
    if (!target || !target->context)
        return;

    /* window ID ignored! */
    renderer->current_context_target = target;
}

static void SDLCALL swrender_SetAsCurrent(GPU_Renderer* renderer)
{
    (void) renderer;
    /* nothing */
}

static void SDLCALL swrender_ResetRendererState(GPU_Renderer* renderer)
{
    (void) renderer;
    /* nothing */
}

static GPU_bool SDLCALL swrender_SetWindowResolution(GPU_Renderer* renderer, Uint16 w, Uint16 h)
{
    unimplemented("GPU_SetWindowResolution", renderer, w, h);
    return GPU_FALSE;
}

static void SDLCALL swrender_SetVirtualResolution(GPU_Renderer* renderer, GPU_Target* target, Uint16 w, Uint16 h)
{
    unimplemented("GPU_SetVirtualResolution", renderer, target, w, h);
}

static void SDLCALL swrender_UnsetVirtualResolution(GPU_Renderer* renderer, GPU_Target* target)
{
    unimplemented("GPU_UnsetVirtualResolution", renderer, target);
}

static void SDLCALL swrender_Quit(GPU_Renderer* renderer)
{
    renderer->impl->FreeTarget(renderer, renderer->current_context_target);
    renderer->current_context_target = NULL;
}

static GPU_bool SDLCALL swrender_SetFullscreen(GPU_Renderer* renderer, GPU_bool enable_fullscreen, GPU_bool use_desktop_resolution)
{
    unimplemented("GPU_SetFullscreen", renderer, enable_fullscreen, use_desktop_resolution);
    return GPU_FALSE;
}

static GPU_Camera SDLCALL swrender_SetCamera(GPU_Renderer* renderer, GPU_Target* target, GPU_Camera* cam)
{
    unimplemented("GPU_SetCamera", renderer, target, cam);
    return GPU_Camera { 0, 0, 0, 0, 0 };
}

static GPU_Image* SDLCALL swrender_CreateImage(GPU_Renderer* renderer, Uint16 w, Uint16 h, GPU_FormatEnum format)
{
    if (!renderer->current_context_target) {
        GPU_PushErrorCode("GPU_CreateImage", GPU_ERROR_USER_ERROR, "No current context");
        return NULL;
    }
        
    GPU_Context *context = renderer->current_context_target->context;
    
    GPU_Image *image = (GPU_Image*)SDL_malloc(sizeof(GPU_Image));
    SW_IMAGE_DATA *image_data = (SW_IMAGE_DATA*)SDL_malloc(sizeof(SW_IMAGE_DATA));

    if (!image || !image_data) {
        SDL_free(image);
        SDL_free(image_data);
        return NULL;
    }

    switch (format) {
    case GPU_FORMAT_RGB:
        image_data->sdl_format = SDL_PIXELFORMAT_RGB888;
        image->num_layers = 3;
        image->bytes_per_pixel = 4;
        break;
    case GPU_FORMAT_RGBA:
        image_data->sdl_format = SDL_PIXELFORMAT_RGBA8888;
        image->num_layers = 4;
        image->bytes_per_pixel = 4;
        break;
    default:
        GPU_PushErrorCode("GPU_CreateImage", GPU_ERROR_BACKEND_ERROR, "Unsupported format");
        SDL_free(image);
        SDL_free(image_data);
        return NULL;
    }
    image_data->tex = SDL_CreateTexture(SDL_RENDERER(context), image_data->sdl_format, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!image_data->tex) {
        SDL_free(image);
        SDL_free(image_data);
        GPU_PushErrorCode("GPU_CreateImage", GPU_ERROR_BACKEND_ERROR, "SDL_CreateTexture failed: %s", SDL_GetError());
        return NULL;
    }
    image_data->refcount = 1;

    image->renderer = renderer;
    image->context_target = renderer->current_context_target;
    image->target = NULL;
    image->w = image->base_w = image->texture_w = w;
    image->h = image->base_h = image->texture_h = h;
    image->using_virtual_resolution = GPU_FALSE;
    image->format = format;
    image->has_mipmaps = GPU_FALSE;
    image->anchor_x = renderer->default_image_anchor_x;
    image->anchor_y = renderer->default_image_anchor_y;
    image->color.r = image->color.g = image->color.b = image->color.a = 255;
    image->use_blending = GPU_TRUE;
    image->blend_mode = GPU_GetBlendModeFromPreset(GPU_BLEND_NORMAL);
    SDL_SetTextureBlendMode(image_data->tex, SDL_BLENDMODE_BLEND);
    image->filter_mode = GPU_FILTER_LINEAR;
    image->snap_mode = GPU_SNAP_POSITION_AND_DIMENSIONS;
    image->wrap_mode_x = GPU_WRAP_NONE;
    image->wrap_mode_y = GPU_WRAP_NONE;
    image->data = image_data;
    image->refcount = 1;
    image->is_alias = GPU_FALSE;
    
    return image;
}

static GPU_Image* SDLCALL swrender_CreateImageUsingTexture(GPU_Renderer* renderer, Uint32 handle, GPU_bool take_ownership)
{
    unimplemented("GPU_CreateImageUsingTexture", renderer, handle, take_ownership);
    return NULL;
}

static GPU_Image* SDLCALL swrender_CreateAliasImage(GPU_Renderer*, GPU_Image* image)
{
    GPU_Image* result;

    if(image == NULL)
        return NULL;

    result = (GPU_Image*)SDL_malloc(sizeof(GPU_Image));
    // Copy the members
    *result = *image;

    // Alias info
    IMAGE_DATA(image)->refcount++;
    result->refcount = 1;
    result->is_alias = GPU_TRUE;

    return result;
}

static GPU_bool SDLCALL swrender_SaveImage(GPU_Renderer* renderer, GPU_Image* image, const char* filename, GPU_FileFormatEnum format)
{
    unimplemented("GPU_SaveImage", renderer, image, filename, format);
    return GPU_FALSE;
}

static GPU_Image* SDLCALL swrender_CopyImage(GPU_Renderer* renderer, GPU_Image* image)
{
    unimplemented("GPU_CopyImage", renderer, image);
    return NULL;
}

static void SDLCALL swrender_UpdateImage(GPU_Renderer* renderer, GPU_Image* image, const GPU_Rect* image_rect, SDL_Surface* surface, const GPU_Rect* surface_rect)
{
    (void) renderer;

    SDL_Rect src, dst;
    if (surface_rect) {
        src.x = (int) surface_rect->x;
        src.y = (int) surface_rect->y;
        src.w = (int) surface_rect->w;
        src.h = (int) surface_rect->h;
    } else {
        src.x = 0;
        src.y = 0;
        src.w = surface->w;        
        src.h = surface->h;
    }
    if (image_rect) {
        dst.x = (int) image_rect->x;
        dst.y = (int) image_rect->y;
        dst.w = (int) image_rect->w;
        dst.h = (int) image_rect->h;
    } else {
        dst.x = 0;
        dst.y = 0;
        dst.w = image->w;
        dst.h = image->h;
    }

    // clip src to source surface
    if (src.x < 0) {
        int overhang = -src.x;
        src.x += overhang;
        dst.x += overhang;
        src.w -= overhang;
        dst.w -= overhang;
    }    
    if (src.y < 0) {
        int overhang = -src.y;
        src.y += overhang;
        dst.y += overhang;
        src.h -= overhang;
        dst.h -= overhang;
    }
    if (src.x + src.w > surface->w) {
        int overhang = src.x + src.w - surface->w;
        src.w -= overhang;
        dst.w -= overhang;
    }
    if (src.y + src.h > surface->h) {
        int overhang = src.y + src.h - surface->h;
        src.h -= overhang;
        dst.h -= overhang;
    }
    
    // clip dst to dest surface
    if (dst.x < 0) {
        int overhang = -dst.x;
        src.x += overhang;
        dst.x += overhang;
        src.w -= overhang;
        dst.w -= overhang;
    }    
    if (dst.y < 0) {
        int overhang = -dst.y;
        src.y += overhang;
        dst.y += overhang;
        src.h -= overhang;
        dst.h -= overhang;
    }
    if (dst.x + dst.w > image->w) {
        int overhang = dst.x + dst.w - image->w;
        src.w -= overhang;
        dst.w -= overhang;
    }
    if (dst.y + dst.h > image->h) {
        int overhang = dst.y + dst.h - image->h;
        src.h -= overhang;
        dst.h -= overhang;
    }

    // clip dst against src
    if (src.w < dst.w) {
        dst.w = src.w;
    }
    if (src.h < dst.h) {
        dst.h = src.h;
    }
    
    // anything left?
    if (dst.w <= 0 || dst.h <= 0) {
        return;
    }

    SW_IMAGE_DATA *image_data = IMAGE_DATA(image);
    SDL_Surface *converted;
    if (image_data->sdl_format != surface->format->format) {
        int bpp;
        Uint32 Rmask, Gmask, Bmask, Amask;        
        SDL_PixelFormatEnumToMasks(image_data->sdl_format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
        converted = SDL_CreateRGBSurface(0, src.w, src.h, bpp, Rmask, Gmask, Bmask, Amask);
        if(!converted) {
            return;
        }

        SDL_BlitSurface(surface, &src, converted, NULL);
        src.x = src.y = 0;
        src.w = converted->w;
        src.h = converted->h;
        surface = converted;
    } else {
        converted = NULL;
    }

    // update it
    Uint8 *pixels = static_cast<Uint8*>(surface->pixels) + surface->format->BytesPerPixel * src.x + surface->pitch * src.y;
    SDL_UpdateTexture(image_data->tex, &dst, pixels, surface->pitch);

    if (converted) {
        SDL_FreeSurface(converted);
    }
}

static void SDLCALL swrender_UpdateImageBytes(GPU_Renderer* renderer, GPU_Image* image, const GPU_Rect* image_rect, const unsigned char* bytes, int bytes_per_row)
{
    (void) renderer;

    SW_IMAGE_DATA *image_data = IMAGE_DATA(image);

    SDL_Rect dst;
    if (image_rect) {
        dst.x = (int) image_rect->x;
        dst.y = (int) image_rect->y;
        dst.w = (int) image_rect->w;
        dst.h = (int) image_rect->h;
    } else {
        dst.x = 0;
        dst.y = 0;
        dst.w = image->w;
        dst.h = image->h;
    }

    // clip dst to dest surface
    if (dst.x < 0) {
        int overhang = -dst.x;
        bytes += overhang * image->bytes_per_pixel;
        dst.x += overhang;
        dst.w -= overhang;
    }    
    if (dst.y < 0) {
        int overhang = -dst.y;
        bytes += overhang * bytes_per_row;
        dst.y += overhang;
        dst.h -= overhang;
    }
    if (dst.x + dst.w > image->w) {
        int overhang = dst.x + dst.w - image->w;
        dst.w -= overhang;
    }
    if (dst.y + dst.h > image->h) {
        int overhang = dst.y + dst.h - image->h;
        dst.h -= overhang;
    }

    // anything left?
    if (dst.w <= 0 || dst.h <= 0) {
        return;
    }

    // update it
    SDL_UpdateTexture(image_data->tex, &dst, bytes, bytes_per_row);
}

static GPU_bool SDLCALL swrender_ReplaceImage(GPU_Renderer* renderer, GPU_Image* image, SDL_Surface* surface, const GPU_Rect* surface_rect)
{
    unimplemented("GPU_ReplaceImage", renderer, image, surface, surface_rect);
    return GPU_FALSE;
}

static GPU_Image* SDLCALL swrender_CopyImageFromSurface(GPU_Renderer* renderer, SDL_Surface* surface)
{    
    GPU_Image *image = renderer->impl->CreateImage(renderer, surface->w, surface->h, GPU_FORMAT_RGBA);
    if (!image) {
        return NULL;
    }

    renderer->impl->UpdateImage(renderer, image, NULL, surface, NULL);
    return image;
}

static GPU_Image* SDLCALL swrender_CopyImageFromTarget(GPU_Renderer* renderer, GPU_Target* target)
{
    unimplemented("GPU_CopyImageFromTarget", renderer, target);
    return NULL;
}

static SDL_Surface* SDLCALL swrender_CopySurfaceFromTarget(GPU_Renderer* renderer, GPU_Target* target)
{
    unimplemented("GPU_CopySurfaceFromTarget", renderer, target);
    return NULL;
}

static SDL_Surface* SDLCALL swrender_CopySurfaceFromImage(GPU_Renderer* renderer, GPU_Image* image)
{
    unimplemented("GPU_CopySurfaceFromImage", renderer, image);
    return NULL;
}

static void SDLCALL swrender_FreeImage(GPU_Renderer* renderer, GPU_Image* image)
{
    if( !image ) {
        return;
    }

    if( --image->refcount > 0 ) {
        return;
    }

    // Delete the attached target first
    if( image->target != NULL ) {
        GPU_Target* target = image->target;
        image->target = NULL;
        renderer->impl->FreeTarget( renderer, target );
    }
    
    // Does the renderer data need to be freed too?
    SW_IMAGE_DATA *image_data = IMAGE_DATA( image );
    if( --image_data->refcount <= 0 ) {
        SDL_DestroyTexture( image_data->tex );
        SDL_free( image_data );
    }

    SDL_free( image );
}

static GPU_Target* SDLCALL swrender_LoadTarget( GPU_Renderer* renderer, GPU_Image* image )
{
    if ( !image ) {
        return NULL;
    }
    
    if ( image->target ) {
        image->target->refcount++;
        return image->target;
    }
    
    GPU_Target *target = ( GPU_Target * )SDL_malloc( sizeof( GPU_Target ) );
    if ( !target ) {
        return target;
    }

    target->renderer = renderer;
    target->context_target = image->context_target;
    target->image = image;
    target->data = NULL;
    target->w = target->base_w = image->w;
    target->h = target->base_h = image->h;
    target->using_virtual_resolution = GPU_FALSE;
    target->use_clip_rect = GPU_FALSE;
    target->use_color = GPU_FALSE;
    target->viewport.x = target->viewport.y = 0;
    target->viewport.w = image->w;
    target->viewport.h = image->h;
    target->use_camera = GPU_FALSE;
    target->context = NULL;
    target->refcount = 1;
    target->is_alias = GPU_FALSE;

    image->target = target;
    return target;
}

static void SDLCALL swrender_FreeTarget(GPU_Renderer* renderer, GPU_Target* target)
{
    if ( !target ) {
        return;
    }

    if( --target->refcount > 0 ) {
        return;
    }

    if( !target->is_alias && target->image ) {
        target->image->target = NULL;
    }
    
    if ( target->context ) {
        // tear down rendererer
        SDL_DestroyRenderer(SDL_RENDERER(target->context));
        SDL_free(target->context->data);
        SDL_free(target->context);
        target->context = NULL;
    
        if (renderer->current_context_target == target) {
            renderer->current_context_target = NULL;
        }
    } else {
        if (CONTEXT_DATA(target->context_target->context)->render_target == target) {
            setRenderTarget(renderer, target->context_target);
        }
    }

    SDL_free(target);
}

static void SDLCALL swrender_Blit(GPU_Renderer* renderer, GPU_Image* image, GPU_Rect* src_rect, GPU_Target* target, float x, float y)
{
    SDL_Rect src, dst;
    if (src_rect) {
        src.x = (int)src_rect->x;
        src.y = (int)src_rect->y;
        src.w = (int)src_rect->w;
        src.h = (int)src_rect->h;
    } else {
        src.x = 0;
        src.y = 0;
        src.w = image->w;
        src.h = image->h;
    }
    
    dst.x = (int)x - src.w * image->anchor_x;
    dst.y = (int)y - src.h * image->anchor_y;
    dst.w = src.w;
    dst.h = src.h;
    
    SDL_RenderCopy(setRenderTarget(renderer, target),
                   IMAGE_DATA(image)->tex,
                   &src, &dst);
}

static void SDLCALL swrender_BlitRotate(GPU_Renderer* renderer, GPU_Image* image, GPU_Rect* src_rect, GPU_Target* target, float x, float y, float degrees)
{
    float w = src_rect ? src_rect->w : image->w;
    float h = src_rect ? src_rect->h : image->h;
    renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y, w * image->anchor_x, h * image->anchor_y, degrees, 1.0f, 1.0f);
}

static void SDLCALL swrender_BlitScale(GPU_Renderer* renderer, GPU_Image* image, GPU_Rect* src_rect, GPU_Target* target, float x, float y, float scaleX, float scaleY)
{
    float w = src_rect ? src_rect->w : image->w;
    float h = src_rect ? src_rect->h : image->h;
    renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y, w * image->anchor_x, h * image->anchor_y, 0.0f, scaleX, scaleY);
}

static void SDLCALL swrender_BlitTransform(GPU_Renderer* renderer, GPU_Image* image, GPU_Rect* src_rect, GPU_Target* target, float x, float y, float degrees, float scaleX, float scaleY)
{
    float w = src_rect ? src_rect->w : image->w;
    float h = src_rect ? src_rect->h : image->h;
    renderer->impl->BlitTransformX(renderer, image, src_rect, target, x, y, w * image->anchor_x, h * image->anchor_y, degrees, scaleX, scaleY);
}

static void SDLCALL swrender_BlitTransformX(GPU_Renderer* renderer, GPU_Image* image, GPU_Rect* src_rect, GPU_Target* target, float x, float y, float pivot_x, float pivot_y, float degrees, float scaleX, float scaleY)
{
    int flip = SDL_FLIP_NONE;
    if (scaleX < 0) {
        scaleX = -scaleX;
        flip |= SDL_FLIP_HORIZONTAL;
    }

    if (scaleY < 0) {
        scaleY = -scaleY;
        flip |= SDL_FLIP_VERTICAL;
    }
    
    SDL_Rect src, dst;
    if (src_rect) {
        src.x = (int)src_rect->x;
        src.y = (int)src_rect->y;
        src.w = (int)src_rect->w;
        src.h = (int)src_rect->h;
    } else {
        src.x = 0;
        src.y = 0;
        src.w = image->w;
        src.h = image->h;
    }

    dst.x = (int)x;
    dst.y = (int)y;
    dst.w = (int)(src.w * scaleX);
    dst.h = (int)(src.h * scaleY);

    SDL_Point center;
    center.x = pivot_x * scaleX;
    center.y = pivot_y * scaleY;

    SDL_RenderCopyEx(setRenderTarget(renderer, target),
                     IMAGE_DATA(image)->tex,
                     &src,
                     &dst,
                     degrees,
                     &center,
                     static_cast<SDL_RendererFlip>( flip ));
}

static void SDLCALL swrender_TriangleBatchX(GPU_Renderer* renderer, GPU_Image* image, GPU_Target* target, unsigned short num_vertices, void* values, unsigned int num_indices, unsigned short* indices, GPU_BatchFlagEnum flags)
{
    unimplemented("GPU_TriangleBatchX", renderer, image, target, num_vertices, values, num_indices, indices, flags);
}

static void SDLCALL swrender_GenerateMipmaps(GPU_Renderer* renderer, GPU_Image* image)
{
    (void) renderer;
    unimplemented("GPU_GenerateMipmaps", renderer, image);
}

static GPU_Rect SDLCALL swrender_SetClip(GPU_Renderer* renderer, GPU_Target* target, Sint16 x, Sint16 y, Uint16 w, Uint16 h)
{
    target->use_clip_rect = GPU_TRUE;

    GPU_Rect old = target->clip_rect;
    target->clip_rect.x = x;
    target->clip_rect.y = y;
    target->clip_rect.w = w;
    target->clip_rect.h = h;

    SDL_Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_RenderSetClipRect(setRenderTarget(renderer, target), &r);
    
    return old;
}

static void SDLCALL swrender_UnsetClip(GPU_Renderer* renderer, GPU_Target* target)
{
    target->use_clip_rect = GPU_FALSE;
    SDL_RenderSetClipRect(setRenderTarget(renderer, target), NULL);
}

static SDL_Color SDLCALL swrender_GetPixel(GPU_Renderer* renderer, GPU_Target* target, Sint16 x, Sint16 y)
{
    unimplemented("GPU_GetPixel", renderer, target, x, y);
    return SDL_Color { 0, 0, 0, 0 };
}

static void SDLCALL swrender_SetImageFilter(GPU_Renderer* renderer, GPU_Image* image, GPU_FilterEnum filter)
{
    unimplemented_warn("GPU_SetImageFilter", renderer, image, filter);
}

static void SDLCALL swrender_SetWrapMode(GPU_Renderer* renderer, GPU_Image* image, GPU_WrapEnum wrap_mode_x, GPU_WrapEnum wrap_mode_y)
{
    unimplemented_warn("GPU_SetWrapMode", renderer, image, wrap_mode_x, wrap_mode_y);
}

static void SDLCALL swrender_ClearRGBA(GPU_Renderer* renderer, GPU_Target* target, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Renderer *sdlr = setRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(sdlr, r, g, b, a);
    SDL_RenderClear(sdlr);
}

static void SDLCALL swrender_FlushBlitBuffer(GPU_Renderer* renderer)
{
    (void) renderer;
    /* ignored */
}

static void SDLCALL swrender_Flip(GPU_Renderer* renderer, GPU_Target* target)
{
    SDL_RenderPresent(setRenderTarget(renderer, target));
}

static Uint32 SDLCALL swrender_CreateShaderProgram(GPU_Renderer* renderer)
{
    unimplemented("GPU_CreateShaderProgram", renderer);
    return 0;
}

static void SDLCALL swrender_FreeShaderProgram(GPU_Renderer* renderer, Uint32 program_object)
{
    unimplemented("GPU_FreeShaderProgram", renderer, program_object);
}

static Uint32 SDLCALL swrender_CompileShader_RW(GPU_Renderer* renderer, GPU_ShaderEnum shader_type, SDL_RWops* shader_source, GPU_bool free_rwops)
{
    unimplemented("GPU_CompileShader_RW", renderer, shader_type, shader_source, free_rwops);
    return 0;
}

static Uint32 SDLCALL swrender_CompileShader(GPU_Renderer* renderer, GPU_ShaderEnum shader_type, const char* shader_source)
{
    unimplemented("GPU_CompileShader", renderer, shader_type, shader_source);
    return 0;
}

static void SDLCALL swrender_FreeShader(GPU_Renderer* renderer, Uint32 shader_object)
{
    unimplemented("GPU_FreeShader", renderer, shader_object);
}

static void SDLCALL swrender_AttachShader(GPU_Renderer* renderer, Uint32 program_object, Uint32 shader_object)
{
    unimplemented("GPU_AttachShader", renderer, program_object, shader_object);
}

static void SDLCALL swrender_DetachShader(GPU_Renderer* renderer, Uint32 program_object, Uint32 shader_object)
{
    unimplemented("GPU_DetachShader", renderer, program_object, shader_object);
}

static GPU_bool SDLCALL swrender_LinkShaderProgram(GPU_Renderer* renderer, Uint32 program_object)
{
    unimplemented("GPU_LinkShaderProgram", renderer, program_object);
    return GPU_FALSE;
}

static void SDLCALL swrender_ActivateShaderProgram(GPU_Renderer* renderer, Uint32 program_object, GPU_ShaderBlock* block)
{
    unimplemented("GPU_ActivateShaderProgram", renderer, program_object, block);
}

static void SDLCALL swrender_DeactivateShaderProgram(GPU_Renderer* renderer)
{
    unimplemented("GPU_DeactivateShaderProgram", renderer);
}

const char* SDLCALL swrender_GetShaderMessage(GPU_Renderer* renderer)
{
    unimplemented("GPU_GetShaderMessage", renderer);
    return NULL;
}

static int SDLCALL swrender_GetAttributeLocation(GPU_Renderer* renderer, Uint32 program_object, const char* attrib_name)
{
    unimplemented("GPU_GetAttributeLocation", renderer, program_object, attrib_name);
    return -1;
}

static int SDLCALL swrender_GetUniformLocation(GPU_Renderer* renderer, Uint32 program_object, const char* uniform_name)
{
    unimplemented("GPU_GetUniformLocation", renderer, program_object, uniform_name);
    return -1;
}

static GPU_ShaderBlock SDLCALL swrender_LoadShaderBlock(GPU_Renderer* renderer, Uint32 program_object, const char* position_name, const char* texcoord_name, const char* color_name, const char* modelViewMatrix_name)
{
    unimplemented("GPU_LoadShaderBlock", renderer, program_object, position_name, texcoord_name, color_name, modelViewMatrix_name);
    return GPU_ShaderBlock { 0, 0, 0, 0 };
}

static void SDLCALL swrender_SetShaderBlock(GPU_Renderer* renderer, GPU_ShaderBlock block)
{
    unimplemented("GPU_SetShaderBlock", renderer, block);
}

static void SDLCALL swrender_SetShaderImage(GPU_Renderer* renderer, GPU_Image* image, int location, int image_unit)
{
    unimplemented("GPU_SetShaderImage", renderer, image, location, image_unit);
}

static void SDLCALL swrender_GetUniformiv(GPU_Renderer* renderer, Uint32 program_object, int location, int* values)
{
    unimplemented("GPU_GetUniformiv", renderer, program_object, location, values);
}

static void SDLCALL swrender_SetUniformi(GPU_Renderer* renderer, int location, int value)
{
    unimplemented("GPU_SetUniformi", renderer, location, value);
}

static void SDLCALL swrender_SetUniformiv(GPU_Renderer* renderer, int location, int num_elements_per_value, int num_values, int* values)
{
    unimplemented("GPU_SetUniformiv", renderer, location, num_elements_per_value, num_values, values);
}

static void SDLCALL swrender_GetUniformuiv(GPU_Renderer* renderer, Uint32 program_object, int location, unsigned int* values)
{
    unimplemented("GPU_GetUniformuiv", renderer, program_object, location, values);
}

static void SDLCALL swrender_SetUniformui(GPU_Renderer* renderer, int location, unsigned int value)
{
    unimplemented("GPU_SetUniformui", renderer, location, value);
}

static void SDLCALL swrender_SetUniformuiv(GPU_Renderer* renderer, int location, int num_elements_per_value, int num_values, unsigned int* values)
{
    unimplemented("GPU_SetUniformuiv", renderer, location, num_elements_per_value, num_values, values);
}

static void SDLCALL swrender_GetUniformfv(GPU_Renderer* renderer, Uint32 program_object, int location, float* values)
{
    unimplemented("GPU_GetUniformfv", renderer, program_object, location, values);
}

static void SDLCALL swrender_SetUniformf(GPU_Renderer* renderer, int location, float value)
{
    unimplemented("GPU_SetUniformf", renderer, location, value);
}

static void SDLCALL swrender_SetUniformfv(GPU_Renderer* renderer, int location, int num_elements_per_value, int num_values, float* values)
{
    unimplemented("GPU_SetUniformfv", renderer, location, num_elements_per_value, num_values, values);
}

static void SDLCALL swrender_SetUniformMatrixfv(GPU_Renderer* renderer, int location, int num_matrices, int num_rows, int num_columns, GPU_bool transpose, float* values)
{
    unimplemented("GPU_SetUniformMatrixfv", renderer, location, num_matrices, num_rows, num_columns, transpose, values);
}

static void SDLCALL swrender_SetAttributef(GPU_Renderer* renderer, int location, float value)
{
    unimplemented("GPU_SetAttributef", renderer, location, value);
}

static void SDLCALL swrender_SetAttributei(GPU_Renderer* renderer, int location, int value)
{
    unimplemented("GPU_SetAttributei", renderer, location, value);
}

static void SDLCALL swrender_SetAttributeui(GPU_Renderer* renderer, int location, unsigned int value)
{
    unimplemented("GPU_SetAttributeui", renderer, location, value);
}

static void SDLCALL swrender_SetAttributefv(GPU_Renderer* renderer, int location, int num_elements, float* value)
{
    unimplemented("GPU_SetAttributefv", renderer, location, num_elements, value);
}

static void SDLCALL swrender_SetAttributeiv(GPU_Renderer* renderer, int location, int num_elements, int* value)
{
    unimplemented("GPU_SetAttributeiv", renderer, location, num_elements, value);
}

static void SDLCALL swrender_SetAttributeuiv(GPU_Renderer* renderer, int location, int num_elements, unsigned int* value)
{
    unimplemented("GPU_SetAttributeuiv", renderer, location, num_elements, value);
}

static void SDLCALL swrender_SetAttributeSource(GPU_Renderer* renderer, int num_values, GPU_Attribute source)
{
    unimplemented("GPU_SetAttributeSource", renderer, num_values, source);
}

static float SDLCALL swrender_SetLineThickness(GPU_Renderer* renderer, float thickness)
{
    unimplemented("GPU_SetLineThickness", renderer, thickness);
    return 0;
}

static float SDLCALL swrender_GetLineThickness(GPU_Renderer* renderer)
{
    unimplemented("GPU_GetLineThickness", renderer);
    return 0;
}

static void SDLCALL swrender_Pixel(GPU_Renderer* renderer, GPU_Target* target, float x, float y, SDL_Color color)
{
    SDL_Renderer *sdlr = setRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(sdlr, color.r, color.g, color.b, color.a);
    SDL_RenderDrawPoint(sdlr, (int)x, (int)y);
}

static void SDLCALL swrender_Line(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, SDL_Color color)
{
    SDL_Renderer *sdlr = setRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(sdlr, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(sdlr, (int)x1, (int)y1, (int)x2, (int)y2);
}

static void SDLCALL swrender_Arc(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float radius, float start_angle, float end_angle, SDL_Color color)
{
    unimplemented("GPU_Arc", renderer, target, x, y, radius, start_angle, end_angle, color);
}

static void SDLCALL swrender_ArcFilled(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float radius, float start_angle, float end_angle, SDL_Color color)
{
    unimplemented("GPU_ArcFilled", renderer, target, x, y, radius, start_angle, end_angle, color);
}

static void SDLCALL swrender_Circle(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float radius, SDL_Color color)
{
    unimplemented("GPU_Circle", renderer, target, x, y, radius, color);
}

static void SDLCALL swrender_CircleFilled(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float radius, SDL_Color color)
{
    unimplemented("GPU_CircleFilled", renderer, target, x, y, radius, color);
}

static void SDLCALL swrender_Ellipse(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float rx, float ry, float degrees, SDL_Color color)
{
    unimplemented("GPU_Ellipse", renderer, target, x, y, rx, ry, degrees, color);
}

static void SDLCALL swrender_EllipseFilled(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float rx, float ry, float degrees, SDL_Color color)
{
    unimplemented("GPU_EllipseFilled", renderer, target, x, y, rx, ry, degrees, color);
}

static void SDLCALL swrender_Sector(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float inner_radius, float outer_radius, float start_angle, float end_angle, SDL_Color color)
{
    unimplemented("GPU_Sector", renderer, target, x, y, inner_radius, outer_radius, start_angle, end_angle, color);
}

static void SDLCALL swrender_SectorFilled(GPU_Renderer* renderer, GPU_Target* target, float x, float y, float inner_radius, float outer_radius, float start_angle, float end_angle, SDL_Color color)
{
    unimplemented("GPU_SectorFilled", renderer, target, x, y, inner_radius, outer_radius, start_angle, end_angle, color);
}

static void SDLCALL swrender_Tri(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color color)
{
    unimplemented("GPU_Tri", renderer, target, x1, y1, x2, y2, x3, y3, color);
}

static void SDLCALL swrender_TriFilled(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, float x3, float y3, SDL_Color color)
{
    unimplemented("GPU_TriFilled", renderer, target, x1, y1, x2, y2, x3, y3, color);
}

static void SDLCALL swrender_Rectangle(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, SDL_Color color)
{
    SDL_Rect r;
    r.x = (int)x1;
    r.y = (int)y1;
    r.w = (int)(x2 - x1);
    r.h = (int)(y2 - y1);
    
    SDL_Renderer *sdlr = setRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(sdlr, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(sdlr, &r);
}

static void SDLCALL swrender_RectangleFilled(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, SDL_Color color)
{
    SDL_Rect r;
    r.x = (int)x1;
    r.y = (int)y1;
    r.w = (int)(x2 - x1);
    r.h = (int)(y2 - y1);
    
    SDL_Renderer *sdlr = setRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(sdlr, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(sdlr, &r);
}

static void SDLCALL swrender_RectangleRound(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, float radius, SDL_Color color)
{
    unimplemented("GPU_RectangleRound", renderer, target, x1, y1, x2, y2, radius, color);
}

static void SDLCALL swrender_RectangleRoundFilled(GPU_Renderer* renderer, GPU_Target* target, float x1, float y1, float x2, float y2, float radius, SDL_Color color)
{
    unimplemented("GPU_RectangleRoundFilled", renderer, target, x1, y1, x2, y2, radius, color);
}

static void SDLCALL swrender_Polygon(GPU_Renderer* renderer, GPU_Target* target, unsigned int num_vertices, float* vertices, SDL_Color color)
{
    unimplemented("GPU_Polygon", renderer, target, num_vertices, vertices, color);
}

static void SDLCALL swrender_PolygonFilled(GPU_Renderer* renderer, GPU_Target* target, unsigned int num_vertices, float* vertices, SDL_Color color)
{
    unimplemented("GPU_PolygonFilled", renderer, target, num_vertices, vertices, color);
}

static GPU_Renderer* swrender_CreateRenderer(GPU_RendererID request)
{
    GPU_Renderer* renderer = (GPU_Renderer*)SDL_malloc(sizeof(GPU_Renderer));
    if( !renderer )
        return NULL;

    memset(renderer, 0, sizeof(GPU_Renderer));

    renderer->id = request;
    renderer->id.renderer = GPU_RENDERER_CUSTOM_0 + 1;
    renderer->shader_language = GPU_LANGUAGE_NONE;
    renderer->min_shader_version = -1;
    renderer->max_shader_version = -1;
    
    renderer->default_image_anchor_x = 0.5f;
    renderer->default_image_anchor_y = 0.5f;
    
    renderer->current_context_target = NULL;
    
    renderer->impl = (GPU_RendererImpl*)SDL_malloc(sizeof(GPU_RendererImpl));
    memset(renderer->impl, 0, sizeof(GPU_RendererImpl));

    renderer->impl->Init = &swrender_Init;
    renderer->impl->CreateTargetFromWindow = &swrender_CreateTargetFromWindow;
    renderer->impl->CreateAliasTarget = &swrender_CreateAliasTarget;
    renderer->impl->MakeCurrent = &swrender_MakeCurrent;
    renderer->impl->SetAsCurrent = &swrender_SetAsCurrent;
    renderer->impl->ResetRendererState = &swrender_ResetRendererState;
    renderer->impl->SetWindowResolution = &swrender_SetWindowResolution;
    renderer->impl->SetVirtualResolution = &swrender_SetVirtualResolution;
    renderer->impl->UnsetVirtualResolution = &swrender_UnsetVirtualResolution;
    renderer->impl->Quit = &swrender_Quit;
    renderer->impl->SetFullscreen = &swrender_SetFullscreen;
    renderer->impl->SetCamera = &swrender_SetCamera;
    renderer->impl->CreateImage = &swrender_CreateImage;
    renderer->impl->CreateImageUsingTexture = &swrender_CreateImageUsingTexture;
    renderer->impl->CreateAliasImage = &swrender_CreateAliasImage;
    renderer->impl->SaveImage = &swrender_SaveImage;
    renderer->impl->CopyImage = &swrender_CopyImage;
    renderer->impl->UpdateImage = &swrender_UpdateImage;
    renderer->impl->UpdateImageBytes = &swrender_UpdateImageBytes;
    renderer->impl->ReplaceImage = &swrender_ReplaceImage;
    renderer->impl->CopyImageFromSurface = &swrender_CopyImageFromSurface;
    renderer->impl->CopyImageFromTarget = &swrender_CopyImageFromTarget;
    renderer->impl->CopySurfaceFromTarget = &swrender_CopySurfaceFromTarget;
    renderer->impl->CopySurfaceFromImage = &swrender_CopySurfaceFromImage;
    renderer->impl->FreeImage = &swrender_FreeImage;
    renderer->impl->LoadTarget = &swrender_LoadTarget;
    renderer->impl->FreeTarget = &swrender_FreeTarget;
    renderer->impl->Blit = &swrender_Blit;
    renderer->impl->BlitRotate = &swrender_BlitRotate;
    renderer->impl->BlitScale = &swrender_BlitScale;
    renderer->impl->BlitTransform = &swrender_BlitTransform;
    renderer->impl->BlitTransformX = &swrender_BlitTransformX;
    renderer->impl->TriangleBatchX = &swrender_TriangleBatchX;
    renderer->impl->GenerateMipmaps = &swrender_GenerateMipmaps;
    renderer->impl->SetClip = &swrender_SetClip;
    renderer->impl->UnsetClip = &swrender_UnsetClip;
    renderer->impl->GetPixel = &swrender_GetPixel;
    renderer->impl->SetImageFilter = &swrender_SetImageFilter;
    renderer->impl->SetWrapMode = &swrender_SetWrapMode;
    renderer->impl->ClearRGBA = &swrender_ClearRGBA;
    renderer->impl->FlushBlitBuffer = &swrender_FlushBlitBuffer;
    renderer->impl->Flip = &swrender_Flip;
    renderer->impl->CreateShaderProgram = &swrender_CreateShaderProgram;
    renderer->impl->FreeShaderProgram = &swrender_FreeShaderProgram;
    renderer->impl->CompileShader_RW = &swrender_CompileShader_RW;
    renderer->impl->CompileShader = &swrender_CompileShader;
    renderer->impl->FreeShader = &swrender_FreeShader;
    renderer->impl->AttachShader = &swrender_AttachShader;
    renderer->impl->DetachShader = &swrender_DetachShader;
    renderer->impl->LinkShaderProgram = &swrender_LinkShaderProgram;
    renderer->impl->ActivateShaderProgram = &swrender_ActivateShaderProgram;
    renderer->impl->DeactivateShaderProgram = &swrender_DeactivateShaderProgram;
    renderer->impl->GetShaderMessage = &swrender_GetShaderMessage;
    renderer->impl->GetAttributeLocation = &swrender_GetAttributeLocation;
    renderer->impl->GetUniformLocation = &swrender_GetUniformLocation;
    renderer->impl->LoadShaderBlock = &swrender_LoadShaderBlock;
    renderer->impl->SetShaderBlock = &swrender_SetShaderBlock;
    renderer->impl->SetShaderImage = &swrender_SetShaderImage;
    renderer->impl->GetUniformiv = &swrender_GetUniformiv;
    renderer->impl->SetUniformi = &swrender_SetUniformi;
    renderer->impl->SetUniformiv = &swrender_SetUniformiv;
    renderer->impl->GetUniformuiv = &swrender_GetUniformuiv;
    renderer->impl->SetUniformui = &swrender_SetUniformui;
    renderer->impl->SetUniformuiv = &swrender_SetUniformuiv;
    renderer->impl->GetUniformfv = &swrender_GetUniformfv;
    renderer->impl->SetUniformf = &swrender_SetUniformf;
    renderer->impl->SetUniformfv = &swrender_SetUniformfv;
    renderer->impl->SetUniformMatrixfv = &swrender_SetUniformMatrixfv;
    renderer->impl->SetAttributef = &swrender_SetAttributef;
    renderer->impl->SetAttributei = &swrender_SetAttributei;
    renderer->impl->SetAttributeui = &swrender_SetAttributeui;
    renderer->impl->SetAttributefv = &swrender_SetAttributefv;
    renderer->impl->SetAttributeiv = &swrender_SetAttributeiv;
    renderer->impl->SetAttributeuiv = &swrender_SetAttributeuiv;
    renderer->impl->SetAttributeSource = &swrender_SetAttributeSource;
    renderer->impl->SetLineThickness = &swrender_SetLineThickness;
    renderer->impl->GetLineThickness = &swrender_GetLineThickness;
    renderer->impl->Pixel = &swrender_Pixel;
    renderer->impl->Line = &swrender_Line;
    renderer->impl->Arc = &swrender_Arc;
    renderer->impl->ArcFilled = &swrender_ArcFilled;
    renderer->impl->Circle = &swrender_Circle;
    renderer->impl->CircleFilled = &swrender_CircleFilled;
    renderer->impl->Ellipse = &swrender_Ellipse;
    renderer->impl->EllipseFilled = &swrender_EllipseFilled;
    renderer->impl->Sector = &swrender_Sector;
    renderer->impl->SectorFilled = &swrender_SectorFilled;
    renderer->impl->Tri = &swrender_Tri;
    renderer->impl->TriFilled = &swrender_TriFilled;
    renderer->impl->Rectangle = &swrender_Rectangle;
    renderer->impl->RectangleFilled = &swrender_RectangleFilled;
    renderer->impl->RectangleRound = &swrender_RectangleRound;
    renderer->impl->RectangleRoundFilled = &swrender_RectangleRoundFilled;
    renderer->impl->Polygon = &swrender_Polygon;
    renderer->impl->PolygonFilled = &swrender_PolygonFilled;

    return renderer;
}

static void swrender_FreeRenderer(GPU_Renderer* renderer)
{
    if(renderer == NULL)
        return;

    SDL_free(renderer->impl);
    SDL_free(renderer);
}

GPU_RendererID register_software_renderer(void)
{
    GPU_RendererEnum e = GPU_ReserveNextRendererEnum();
    GPU_RendererID id = GPU_MakeRendererID("Software rendering", e, 0, 0);
    GPU_RegisterRenderer(id, &swrender_CreateRenderer, &swrender_FreeRenderer);
    return id;
}
