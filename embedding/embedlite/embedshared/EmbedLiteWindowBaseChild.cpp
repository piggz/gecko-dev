/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmbedLog.h"

#include <math.h>

#include "nsWindow.h"
#include "EmbedLiteWindowBaseChild.h"
#include "mozilla/Unused.h"
#include "Hal.h"
#include "ScreenOrientation.h"
#include "nsIScreen.h"
#include "nsIScreenManager.h"
#include "gfxPlatform.h"

using namespace mozilla::dom;

namespace mozilla {

namespace layers {
void ShutdownTileCache();
}
namespace embedlite {

namespace {

static std::map<uint32_t, EmbedLiteWindowBaseChild*> sWindowChildMap;

} // namespace

EmbedLiteWindowBaseChild::EmbedLiteWindowBaseChild(const uint16_t& width, const uint16_t& height, const uint32_t& aId)
  : mId(aId)
  , mWidget(nullptr)
  , mBounds(0, 0, width, height)
  , mRotation(ROTATION_0)
{
  MOZ_ASSERT(sWindowChildMap.find(aId) == sWindowChildMap.end());
  sWindowChildMap[aId] = this;

  MOZ_COUNT_CTOR(EmbedLiteWindowBaseChild);

  mCreateWidgetTask = NewCancelableRunnableMethod("EmbedLiteWindowBaseChild::CreateWidget",
                                                  this,
                                                  &EmbedLiteWindowBaseChild::CreateWidget);
  MessageLoop::current()->PostTask(mCreateWidgetTask.forget());

  // Make sure gfx platform is initialized and ready to go.
  gfxPlatform::GetPlatform();
}

EmbedLiteWindowBaseChild *EmbedLiteWindowBaseChild::From(const uint32_t id)
{
  std::map<uint32_t, EmbedLiteWindowBaseChild*>::const_iterator it = sWindowChildMap.find(id);
  if (it != sWindowChildMap.end()) {
    return it->second;
  }
  return nullptr;
}

EmbedLiteWindowBaseChild::~EmbedLiteWindowBaseChild()
{
  MOZ_ASSERT(sWindowChildMap.find(mId) != sWindowChildMap.end());
  sWindowChildMap.erase(sWindowChildMap.find(mId));

  MOZ_COUNT_DTOR(EmbedLiteWindowBaseChild);

  if (mCreateWidgetTask) {
    mCreateWidgetTask->Cancel();
    mCreateWidgetTask = nullptr;
  }

  if (sWindowChildMap.empty()) {
    mozilla::layers::ShutdownTileCache();
  }
}

nsWindow *EmbedLiteWindowBaseChild::GetWidget() const
{
  return static_cast<nsWindow*>(mWidget.get());
}

void EmbedLiteWindowBaseChild::ActorDestroy(ActorDestroyReason aWhy)
{
  LOGT("reason:%i", aWhy);
}

mozilla::ipc::IPCResult EmbedLiteWindowBaseChild::RecvDestroy()
{
  LOGT("destroy");
  mWidget = nullptr;
  Unused << SendDestroyed();
  PEmbedLiteWindowChild::Send__delete__(this);
  return IPC_OK();
}

mozilla::ipc::IPCResult EmbedLiteWindowBaseChild::RecvSetSize(const gfxSize &aSize)
{
  mBounds = LayoutDeviceIntRect(0, 0, (int)nearbyint(aSize.width), (int)nearbyint(aSize.height));
  LOGT("this:%p width: %f, height: %f as int w: %d h: %h", this, aSize.width, aSize.height, (int)nearbyint(aSize.width), (int)nearbyint(aSize.height));
  if (mWidget) {
    mWidget->Resize(aSize.width, aSize.height, true);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult EmbedLiteWindowBaseChild::RecvSetContentOrientation(const uint32_t &aRotation)
{
  LOGT("this:%p", this);
  mRotation = static_cast<mozilla::ScreenRotation>(aRotation);
  if (mWidget) {
    nsWindow* widget = GetWidget();
    widget->SetRotation(mRotation);
    widget->UpdateSize();
  }

  nsresult rv;
  nsCOMPtr<nsIScreenManager> screenMgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1", &rv);
  NS_ENSURE_TRUE(screenMgr, IPC_OK());

  nsIntRect rect;
  int32_t colorDepth, pixelDepth;
  nsCOMPtr<nsIScreen> screen;

  screenMgr->GetPrimaryScreen(getter_AddRefs(screen));
  screen->GetRect(&rect.x, &rect.y, &rect.width, &rect.height);
  screen->GetColorDepth(&colorDepth);
  screen->GetPixelDepth(&pixelDepth);

  mozilla::dom::ScreenOrientationInternal orientation = eScreenOrientation_Default;
  uint16_t angle = 0;
  switch (mRotation) {
    case ROTATION_0:
      angle = 0;
      orientation = mozilla::dom::eScreenOrientation_PortraitPrimary;
      break;
    case ROTATION_90:
      angle = 90;
      orientation = mozilla::dom::eScreenOrientation_LandscapePrimary;
      break;
    case ROTATION_180:
      angle = 180;
      orientation = mozilla::dom::eScreenOrientation_PortraitSecondary;
      break;
    case ROTATION_270:
      angle = 270;
      orientation = mozilla::dom::eScreenOrientation_LandscapeSecondary;
      break;
    default:
      break;
  }

  hal::NotifyScreenConfigurationChange(hal::ScreenConfiguration(
      rect, orientation, angle, colorDepth, pixelDepth));

  return IPC_OK();
}

void EmbedLiteWindowBaseChild::CreateWidget()
{
  LOGT("this:%p", this);
  if (mCreateWidgetTask) {
    mCreateWidgetTask->Cancel();
    mCreateWidgetTask = nullptr;
  }

  mWidget = new nsWindow(this);
  GetWidget()->SetRotation(mRotation);

  nsWidgetInitData  widgetInit;
  widgetInit.clipChildren = true;
  widgetInit.mWindowType = eWindowType_toplevel;

  // nsWindow::CreateCompositor() reads back Size
  // when it creates the compositor.
  Unused << mWidget->Create(
              nullptr, 0,              // no parents
              mBounds,
              &widgetInit              // HandleWidgetEvent
              );
  GetWidget()->UpdateSize();
  Unused << SendInitialized();
}

} // namespace embedlite
} // namespace mozilla
