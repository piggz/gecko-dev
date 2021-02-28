/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_GTK2
#include <gtk/gtk.h>
#endif

#ifdef MOZ_WIDGET_QT
#include "nsQAppInstance.h"
#endif

#include "EmbedLog.h"

#include "EmbedLiteAppProcessChild.h"
#include "mozilla/Unused.h"
#include "nsThreadManager.h"
#include "nsServiceManagerUtils.h"
#include "nsIConsoleService.h"
#include "nsDebugImpl.h"
#include "EmbedLiteViewProcessChild.h"
#include "nsIWindowCreator.h"
#include "nsIWindowWatcher.h"
#include "WindowCreator.h"
#include "nsIEmbedAppService.h"
#include "EmbedLiteViewChildIface.h"
#include "EmbedLiteJSON.h"
#include "nsIComponentRegistrar.h"             // for nsIComponentRegistrar
#include "nsIComponentManager.h"               // for nsIComponentManager
#include "nsIFactory.h"
#include "mozilla/GenericFactory.h"
#include "mozilla/ModuleUtils.h"               // for NS_GENERIC_FACTORY_CONSTRUCTOR
#include "mozilla/layers/PCompositorBridgeChild.h"

#include "mozilla/Preferences.h"
#include "mozilla/dom/PContent.h"

using namespace base;
using namespace mozilla::ipc;
using namespace mozilla::layers;

namespace mozilla {
namespace embedlite {

EmbedLiteAppProcessChild*
EmbedLiteAppProcessChild::GetSingleton()
{
  return static_cast<EmbedLiteAppProcessChild*>(EmbedLiteAppBaseChild::GetInstance());
}

EmbedLiteAppProcessChild::EmbedLiteAppProcessChild()
  : EmbedLiteAppBaseChild(nullptr)
{
  LOGT();
  nsDebugImpl::SetMultiprocessMode("Child");
}

EmbedLiteAppProcessChild::~EmbedLiteAppProcessChild()
{
  LOGT();
}

bool
EmbedLiteAppProcessChild::Init(MessageLoop* aIOLoop,
                               base::ProcessId aParentPid,
                               IPC::Channel* aChannel)
{
#ifdef MOZ_WIDGET_GTK
  // We need to pass a display down to gtk_init because it's not going to
  // use the one from the environment on its own when deciding which backend
  // to use, and when starting under XWayland, it may choose to start with
  // the wayland backend instead of the x11 backend.
  // The DISPLAY environment variable is normally set by the parent process.
  char* display_name = PR_GetEnv("DISPLAY");
  if (display_name) {
    int argc = 3;
    char option_name[] = "--display";
    char* argv[] = {
      nullptr,
      option_name,
      display_name,
      nullptr
    };
    char** argvp = argv;
    gtk_init(&argc, &argvp);
  } else {
    gtk_init(nullptr, nullptr);
  }
#endif

#ifdef MOZ_WIDGET_QT
  // sigh, seriously
  nsQAppInstance::AddRef();
#endif

#ifdef MOZ_X11
  // Do this after initializing GDK, or GDK will install its own handler.
  XRE_InstallX11ErrorHandler();
#endif

#ifdef MOZ_NUWA_PROCESS
  SetTransport(aChannel);
#endif

  // Once we start sending IPC messages, we need the thread manager to be
  // initialized so we can deal with the responses. Do that here before we
  // try to construct the crash reporter.
  nsresult rv = nsThreadManager::get().Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  if (!Open(aChannel, aParentPid, aIOLoop)) {
    return false;
  }

  return true;
}

void
EmbedLiteAppProcessChild::InitXPCOM()
{
  LOGT("Initialize some global XPCOM stuff here");

  InitWindowWatcher();

  RecvSetBoolPref(nsDependentCString("layers.offmainthreadcomposition.enabled"), true);

  mozilla::DebugOnly<nsresult> rv = InitAppService();
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID);

  if (observerService) {
    observerService->NotifyObservers(nullptr, "embedliteInitialized", nullptr);
  }

  Unused << SendInitialized();

  InfallibleTArray<mozilla::dom::Pref> prefs;
  Preferences::GetPreferences(&prefs);
  SendPrefsArrayInitialized(prefs);
}

void
EmbedLiteAppProcessChild::ActorDestroy(ActorDestroyReason aWhy)
{
  LOGT("reason:%i", aWhy);
  if (AbnormalShutdown == aWhy) {
    NS_WARNING("shutting down early because of crash!");
    QuickExit();
  }

  XRE_ShutdownChildProcess();
}

void
EmbedLiteAppProcessChild::QuickExit()
{
    NS_WARNING("content process _exit()ing");
    _exit(0);
}

PEmbedLiteViewChild*
EmbedLiteAppProcessChild::AllocPEmbedLiteViewChild(const uint32_t& windowId, const uint32_t& id,
                                                   const uint32_t& parentId, const bool& isPrivateWindow,
                                                   const bool& isDesktopMode)
{
  LOGT("id:%u, parentId:%u", id, parentId);
  static bool sViewInitializeOnce = false;
  if (!sViewInitializeOnce) {
    gfxPlatform::GetPlatform();
    sViewInitializeOnce = true;
  }
  EmbedLiteViewProcessChild* view = new EmbedLiteViewProcessChild(windowId, id, parentId, isPrivateWindow, isDesktopMode);
  view->AddRef();
  return view;
}

PEmbedLiteWindowChild*
EmbedLiteAppProcessChild::AllocPEmbedLiteWindowChild(const uint16_t& width, const uint16_t& height, const uint32_t& id)
{
  LOGNI();
  return nullptr;
}

PCompositorBridgeChild*
EmbedLiteAppProcessChild::AllocPCompositorBridgeChild(Transport* aTransport, ProcessId aOtherProcess)
{
  LOGT();
  //return CompositorBridgeChild::Create(aTransport, aOtherProcess);
  return nullptr;
}

} // namespace embedlite
} // namespace mozilla

