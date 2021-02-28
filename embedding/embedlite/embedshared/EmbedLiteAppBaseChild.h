/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_APP_EMBED_BASE_CHILD_H
#define MOZ_APP_EMBED_BASE_CHILD_H

#include "mozilla/embedlite/PEmbedLiteAppChild.h"  // for PEmbedLiteAppChild
#include "nsIObserver.h"                           // for nsIObserver
#include "EmbedLiteAppChildIface.h"

class EmbedLiteAppService;
class nsIWebBrowserChrome;

namespace mozilla {
namespace embedlite {

class EmbedLiteViewBaseChild;
class EmbedLiteWindowBaseChild;

class EmbedLiteAppBaseChild : public PEmbedLiteAppChild,
                              public nsIObserver,
                              public EmbedLiteAppChildIface
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  EmbedLiteAppBaseChild(MessageLoop* aParentLoop);
  void Init(MessageChannel* aParentChannel);
  EmbedLiteViewChildIface* GetViewByID(uint32_t aId);
  EmbedLiteViewChildIface* GetViewByChromeParent(nsIWebBrowserChrome* aParent);
  EmbedLiteWindowBaseChild* GetWindowByID(uint32_t aWindowID);
  bool CreateWindow(const uint32_t& parentId, const uint32_t& chromeFlags, uint32_t* createdID, bool* cancel);
  static EmbedLiteAppBaseChild* GetInstance();

protected:
  virtual ~EmbedLiteAppBaseChild();

  // Embed API ipdl interface
  virtual mozilla::ipc::IPCResult RecvSetBoolPref(const nsCString &, const bool &) override;
  virtual mozilla::ipc::IPCResult RecvSetCharPref(const nsCString &, const nsCString &) override;
  virtual mozilla::ipc::IPCResult RecvSetIntPref(const nsCString &, const int &) override;
  virtual mozilla::ipc::IPCResult RecvLoadGlobalStyleSheet(const nsCString &, const bool &) override;
  virtual mozilla::ipc::IPCResult RecvLoadComponentManifest(const nsCString &) override;

  // IPDL protocol impl
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  virtual mozilla::ipc::IPCResult RecvPreDestroy() override;
  virtual mozilla::ipc::IPCResult RecvObserve(const nsCString &topic,
                                              const nsString &data) override;
  virtual mozilla::ipc::IPCResult RecvAddObserver(const nsCString &) override;
  virtual mozilla::ipc::IPCResult RecvRemoveObserver(const nsCString &) override;
  virtual mozilla::ipc::IPCResult RecvAddObservers(InfallibleTArray<nsCString> &&observers) override;
  virtual mozilla::ipc::IPCResult RecvRemoveObservers(InfallibleTArray<nsCString> &&observers) override;
  virtual bool DeallocPEmbedLiteViewChild(PEmbedLiteViewChild*) override;
  virtual bool DeallocPEmbedLiteWindowChild(PEmbedLiteWindowChild*) override;

protected:
  MessageLoop* mParentLoop;
  std::map<uint32_t, EmbedLiteViewBaseChild*> mWeakViewMap;
  std::map<uint32_t, EmbedLiteWindowBaseChild*> mWeakWindowMap;
  void InitWindowWatcher();
  nsresult InitAppService();

private:
  friend class EmbedLiteViewBaseChild;
  friend class EmbedLiteViewThreadChild;
  friend class EmbedLiteViewProcessChild;
  friend class EmbedLiteAppProcessChild;

  DISALLOW_EVIL_CONSTRUCTORS(EmbedLiteAppBaseChild);
};

} // namespace embedlite
} // namespace mozilla

#endif // MOZ_APP_EMBED_BASE_CHILD_H
