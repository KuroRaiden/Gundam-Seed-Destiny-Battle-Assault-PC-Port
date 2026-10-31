#include "training_snapshot_writer.h"
#include "training_snapshot.h"
#include "training_tools.h"
#include "training_meter_overlay.h"
#include <memory>
#include <stdexcept>
namespace seed {
namespace {
struct RenderState {
    SDL_Renderer* renderer;
    SDL_Texture* target;
    int logical_w = 0, logical_h = 0;
    SDL_RendererLogicalPresentation logical{};
    float sx = 1, sy = 1;
    SDL_Rect viewport{}, clip{};
    bool viewport_set, clip_set;
    SDL_BlendMode blend{};
    Uint8 red = 0, green = 0, blue = 0, alpha = 255;
    explicit RenderState(SDL_Renderer* value)
        : renderer(value), target(SDL_GetRenderTarget(value)) {
        SDL_GetRenderLogicalPresentation(renderer, &logical_w, &logical_h, &logical);
        SDL_GetRenderScale(renderer, &sx, &sy);
        SDL_GetRenderViewport(renderer, &viewport);
        SDL_GetRenderClipRect(renderer, &clip);
        viewport_set = SDL_RenderViewportSet(renderer);
        clip_set = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        SDL_GetRenderDrawColor(renderer, &red, &green, &blue, &alpha);
    }
    ~RenderState() {
        SDL_SetRenderTarget(renderer, target);
        SDL_SetRenderLogicalPresentation(renderer, logical_w, logical_h, logical);
        SDL_SetRenderScale(renderer, sx, sy);
        SDL_SetRenderViewport(renderer, viewport_set ? &viewport : nullptr);
        SDL_SetRenderClipRect(renderer, clip_set ? &clip : nullptr);
        SDL_SetRenderDrawBlendMode(renderer, blend);
        SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    }
};
void require(bool ok) {
    if (!ok)
        throw std::runtime_error(SDL_GetError());
}
} // namespace
bool save_pending_training_snapshot(SDL_Window* window, SDL_Renderer* renderer,
                                    TrainingMeterOverlay& overlay,
                                    const std::filesystem::path& folder,
                                    const SDL_FRect& destination) {
    auto clean = take_training_snapshot();
    if (clean.empty())
        return false;
    try {
        int width = 0, height = 0;
        require(window && renderer && SDL_GetWindowSizeInPixels(window, &width, &height));
        require(width > 0 && height > 0);
        using Texture = std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)>;
        Texture target(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, width, height),
                       SDL_DestroyTexture);
        Texture sprites(
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, 240, 160),
            SDL_DestroyTexture);
        require(target && sprites);
        require(SDL_UpdateTexture(sprites.get(), nullptr,
                                  training_snapshot_display_pixels(clean.data()), 240 * 3));
        require(SDL_SetTextureScaleMode(sprites.get(), SDL_SCALEMODE_NEAREST));
        require(SDL_SetTextureBlendMode(sprites.get(), SDL_BLENDMODE_NONE));
        const auto filename = training_snapshot_filename(folder);
        {
            // This render target has the window's physical dimensions. The
            // live backbuffer, viewport and letterbox state are restored even
            // if PNG encoding or a renderer operation fails.
            RenderState restore(renderer);
            require(SDL_SetRenderTarget(renderer, target.get()));
            require(SDL_SetRenderLogicalPresentation(renderer, 0, 0,
                                                     SDL_LOGICAL_PRESENTATION_DISABLED));
            require(SDL_SetRenderScale(renderer, 1, 1));
            require(SDL_SetRenderViewport(renderer, nullptr));
            require(SDL_SetRenderClipRect(renderer, nullptr));
            require(SDL_SetRenderDrawColor(renderer, kTrainingSnapshotBlue[0],
                                           kTrainingSnapshotBlue[1], kTrainingSnapshotBlue[2],
                                           255));
            require(SDL_RenderClear(renderer));
            require(SDL_RenderTexture(renderer, sprites.get(), nullptr, &destination));
            overlay.draw(false, true);
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> image(
                SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
            require(bool(image));
            const auto utf8 = filename.u8string();
            require(SDL_SavePNG(image.get(), reinterpret_cast<const char*>(utf8.c_str())));
        }
        training_snapshot_notice("Snapshot saved in snapshots/" + filename.filename().string());
    } catch (const std::exception& error) {
        training_snapshot_notice(std::string("Snapshot could not be saved: ") + error.what());
    }
    return true;
}
} // namespace seed
