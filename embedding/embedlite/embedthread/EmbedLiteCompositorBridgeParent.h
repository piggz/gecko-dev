/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_EmbedLiteCompositorBridgeParent_h
#define mozilla_layers_EmbedLiteCompositorBridgeParent_h

#include "Layers.h"
#include "base/task.h" // for CancelableRunnable
#include "mozilla/Mutex.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorManagerParent.h"

#include <functional>

namespace mozilla {

namespace layers {
class LayerManagerComposite;
}

namespace embedlite {

class EmbedLiteWindowListener;

class EmbedLiteCompositorBridgeParent : public mozilla::layers::CompositorBridgeParent
{
public:
  EmbedLiteCompositorBridgeParent(uint32_t windowId,
                                  mozilla::layers::CompositorManagerParent *aManager,
                                  CSSToLayoutDeviceScale aScale,
                                  const TimeDuration &aVsyncRate,
                                  const CompositorOptions &aOptions,
                                  bool aRenderToEGLSurface,
                                  const gfx::IntSize &aSurfaceSize);

  void SetSurfaceSize(int width, int height);
  void* GetPlatformImage(int* width, int* height);
  void GetPlatformImage(const std::function<void(void *image, int width, int height)> &callback);
  void SuspendRendering();
  void ResumeRendering();

  void PresentOffscreenSurface();

protected:
  friend class EmbedLitePuppetWidget;

  virtual ~EmbedLiteCompositorBridgeParent();
  virtual PLayerTransactionParent*
  AllocPLayerTransactionParent(const nsTArray<LayersBackend>& aBackendHints,
                               const uint64_t& aId) override;
  virtual bool DeallocPLayerTransactionParent(PLayerTransactionParent* aLayers) override;
  virtual void CompositeToDefaultTarget() override;

private:
  void PrepareOffscreen();

  uint32_t mWindowId;
  RefPtr<CancelableRunnable> mCurrentCompositeTask;
  gfx::IntSize mSurfaceSize;
  bool mUseExternalGLContext;
  Mutex mRenderMutex;

  DISALLOW_EVIL_CONSTRUCTORS(EmbedLiteCompositorBridgeParent);
};

} // embedlite
} // mozilla

#endif // mozilla_layers_EmbedLiteCompositorBridgeParent_h
