/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EmbedLog.h"

#include "TabChildHelper.h"
#include "nsIWidget.h"

#include "TabChild.h"
#include "EmbedLiteViewChildIface.h"
#include "EmbedLiteViewThreadChild.h"
#include "EmbedLiteJSON.h"
#include "apz/src/AsyncPanZoomController.h" // for AsyncPanZoomController
#include "nsIDOMDocument.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/Unused.h"

#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"

#include "nsNetUtil.h"
#include "nsIDOMWindowUtils.h"
#include "nsContentUtils.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "nsGlobalWindow.h"
#include "nsIDocShell.h"
#include "nsViewportInfo.h"
#include "nsPIWindowRoot.h"
#include "nsThreadUtils.h" // for mozilla::Runnable
#include "mozilla/Preferences.h"
#include "nsIFrame.h"
#include "nsView.h"
#include "nsLayoutUtils.h"
#include "nsIDocumentInlines.h"
#include "APZCCallbackHelper.h"
#include "EmbedFrame.h"

static const char BEFORE_FIRST_PAINT[] = "before-first-paint";
static const char CANCEL_DEFAULT_PAN_ZOOM[] = "cancel-default-pan-zoom";
static const char BROWSER_ZOOM_TO_RECT[] = "browser-zoom-to-rect";
static const char DETECT_SCROLLABLE_SUBFRAME[] = "detect-scrollable-subframe";
static bool sDisableViewportHandler = getenv("NO_VIEWPORT") != 0;

using namespace mozilla;
using namespace mozilla::embedlite;
using namespace mozilla::layers;
using namespace mozilla::layout;
using namespace mozilla::dom;
using namespace mozilla::widget;

static const CSSSize kDefaultViewportSize(980, 480);

static bool sPostAZPCAsJsonViewport(false);

TabChildHelper::TabChildHelper(EmbedLiteViewChildIface* aView)
  : mView(aView)
  , mHasValidInnerSize(false)
  , mIPCOpen(false)
{
  LOGT();

//  mScrolling = sDisableViewportHandler == false ? ASYNC_PAN_ZOOM : DEFAULT_SCROLLING;

  // Init default prefs
  static bool sPrefInitialized = false;
  if (!sPrefInitialized) {
    sPrefInitialized = true;
    Preferences::AddBoolVarCache(&sPostAZPCAsJsonViewport, "embedlite.azpc.json.viewport", false);
  }

  nsCOMPtr<nsIObserverService> observerService =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID);

  if (observerService) {
    observerService->AddObserver(this,
                                 BEFORE_FIRST_PAINT,
                                 false);
    observerService->AddObserver(this,
                                 CANCEL_DEFAULT_PAN_ZOOM,
                                 false);
    observerService->AddObserver(this,
                                 BROWSER_ZOOM_TO_RECT,
                                 false);
    observerService->AddObserver(this,
                                 DETECT_SCROLLABLE_SUBFRAME,
                                 false);
  }
  if (!InitTabChildGlobal()) {
    NS_WARNING("Failed to register child global ontext");
  }
}

TabChildHelper::~TabChildHelper()
{
  LOGT();
  mGlobal = nullptr;

  if (mTabChildGlobal) {
    EventListenerManager* elm = mTabChildGlobal->GetExistingListenerManager();
    if (elm) {
      elm->Disconnect();
    }
    mTabChildGlobal->mTabChild = nullptr;
  }
}

void
TabChildHelper::Disconnect()
{
  LOGT();
  mIPCOpen = false;
  if (mTabChildGlobal) {
    // The messageManager relays messages via the TabChild which
    // no longer exists.
    static_cast<nsFrameMessageManager*>
    (mTabChildGlobal->mMessageManager.get())->Disconnect();
    mTabChildGlobal->mMessageManager = nullptr;
  }
}

class EmbedUnloadScriptEvent : public mozilla::Runnable
{
public:
  explicit EmbedUnloadScriptEvent(TabChildHelper* aTabChild, TabChildGlobal* aTabChildGlobal)
    : mozilla::Runnable("TabChildHelper::EmbedUnloadScriptEvent")
    , mTabChild(aTabChild)
    , mTabChildGlobal(aTabChildGlobal)
  { }

  NS_IMETHOD Run() {
    LOGT();
    RefPtr<Event> event = NS_NewDOMEvent(mTabChildGlobal, nullptr, nullptr);
    if (event) {
      event->InitEvent(NS_LITERAL_STRING("unload"), false, false);
      event->SetTrusted(true);

      bool dummy;
      mTabChildGlobal->DispatchEvent(event, &dummy);
    }

    return NS_OK;
  }

  RefPtr<TabChildHelper> mTabChild;
  TabChildGlobal* mTabChildGlobal;
};

void
TabChildHelper::Unload()
{
  LOGT();
  if (mTabChildGlobal) {
    // Let the frame scripts know the child is being closed
    nsContentUtils::AddScriptRunner(
      new EmbedUnloadScriptEvent(this, mTabChildGlobal)
    );
  }
  nsCOMPtr<nsIObserverService> observerService =
    do_GetService(NS_OBSERVERSERVICE_CONTRACTID);

  observerService->RemoveObserver(this, BEFORE_FIRST_PAINT);
  observerService->RemoveObserver(this, CANCEL_DEFAULT_PAN_ZOOM);
  observerService->RemoveObserver(this, BROWSER_ZOOM_TO_RECT);
  observerService->RemoveObserver(this, DETECT_SCROLLABLE_SUBFRAME);
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TabChildHelper)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY(nsITabChild)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END_INHERITING(TabChildBase)

NS_IMPL_ADDREF_INHERITED(TabChildHelper, TabChildBase);
NS_IMPL_RELEASE_INHERITED(TabChildHelper, TabChildBase);

bool
TabChildHelper::InitTabChildGlobal()
{
  if (mTabChildGlobal) {
    return true;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  NS_ENSURE_TRUE(window, false);
  nsCOMPtr<nsIDOMEventTarget> chromeHandler =
    do_QueryInterface(window->GetChromeEventHandler());
  NS_ENSURE_TRUE(chromeHandler, false);

  RefPtr<TabChildGlobal> scope = new TabChildGlobal(this);
  NS_ENSURE_TRUE(scope, false);

  mTabChildGlobal = scope;

  nsISupports* scopeSupports = NS_ISUPPORTS_CAST(nsIDOMEventTarget*, scope);

  // Not sure if
  NS_ENSURE_TRUE(InitChildGlobalInternal(scopeSupports, nsCString("intProcessEmbedChildGlobal")), false);

  scope->Init();

  nsCOMPtr<nsPIWindowRoot> root = do_QueryInterface(chromeHandler);
  NS_ENSURE_TRUE(root,  false);
  root->SetParentTarget(scope);

  return true;
}

bool
TabChildHelper::HasValidInnerSize()
{
  return mHasValidInnerSize;
}

NS_IMETHODIMP
TabChildHelper::Observe(nsISupports* aSubject,
                        const char* aTopic,
                        const char16_t* aData)
{
  if (!strcmp(aTopic, BROWSER_ZOOM_TO_RECT)) {
    nsCOMPtr<nsIDocument> doc(GetDocument());
    uint32_t presShellId;
    ViewID viewId;
    if (APZCCallbackHelper::GetOrCreateScrollIdentifiers(doc->GetDocumentElement(),
                                                         &presShellId, &viewId)) {
      CSSRect rect;
      sscanf(NS_ConvertUTF16toUTF8(aData).get(),
             "{\"x\":%f,\"y\":%f,\"w\":%f,\"h\":%f}",
             &rect.x, &rect.y, &rect.width, &rect.height);
      mView->ZoomToRect(presShellId, viewId, rect);
    }
  } else if (!strcmp(aTopic, BEFORE_FIRST_PAINT)) {
    nsCOMPtr<nsIDocument> subject(do_QueryInterface(aSubject));
    nsCOMPtr<nsIDocument> doc(GetDocument());

    if (SameCOMIdentity(subject, doc)) {
      nsCOMPtr<nsIPresShell> shell(doc->GetShell());
      if (shell) {
        shell->SetIsFirstPaint(true);
      }

      APZCCallbackHelper::InitializeRootDisplayport(shell);

      nsCOMPtr<nsIObserverService> observerService = do_GetService(NS_OBSERVERSERVICE_CONTRACTID);
      if (observerService) {
        observerService->NotifyObservers(aSubject, "embedlite-before-first-paint", nullptr);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
TabChildHelper::HandleEvent(nsIDOMEvent* aEvent)
{
  (void)(aEvent);
  return NS_OK;
}


void TabChildHelper::BeforeUnloadAdded() {
  LOGT();
}

void TabChildHelper::BeforeUnloadRemoved() {
  LOGT();
}

bool
TabChildHelper::UpdateFrame(const FrameMetrics& aFrameMetrics)
{
  return TabChildBase::UpdateFrameHandler(aFrameMetrics);
}

nsIWebNavigation*
TabChildHelper::WebNavigation() const
{
  return mView->WebNavigation();
}

nsIWidget*
TabChildHelper::WebWidget()
{
  nsCOMPtr<nsIDocument> document = GetDocument();
  return nsContentUtils::WidgetForDocument(document);
}

bool
TabChildHelper::DoLoadMessageManagerScript(const nsAString& aURL, bool aRunInGlobalScope)
{
  if (!InitTabChildGlobal())
    // This can happen if we're half-destroyed.  It's not a fatal
    // error.
  {
    return false;
  }

  LoadScriptInternal(aURL, aRunInGlobalScope);
  return true;
}

bool
TabChildHelper::DoSendBlockingMessage(JSContext* aCx,
                                      const nsAString& aMessage,
                                      mozilla::dom::ipc::StructuredCloneData& aData,
                                      JS::Handle<JSObject *> aCpows,
                                      nsIPrincipal* aPrincipal,
                                      nsTArray<mozilla::dom::ipc::StructuredCloneData> *aRetVal,
                                      bool aIsSync)
{
  nsCOMPtr<nsIMessageBroadcaster> globalIMessageManager =
          do_GetService("@mozilla.org/globalmessagemanager;1");
  RefPtr<nsFrameMessageManager> globalMessageManager =
          static_cast<nsFrameMessageManager*>(globalIMessageManager.get());
  RefPtr<nsFrameMessageManager> contentFrameMessageManager =
          static_cast<nsFrameMessageManager*>(mTabChildGlobal->mMessageManager.get());

  nsCOMPtr<nsPIDOMWindowOuter> pwindow = do_GetInterface(WebNavigation());
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(pwindow);
  RefPtr<EmbedFrame> embedFrame = new EmbedFrame();
  embedFrame->mWindow = window;
  embedFrame->mMessageManager = mTabChildGlobal;
  SameProcessCpowHolder cpows(JS::RootingContext::get(aCx), aCpows);

  nsresult globalReceived = globalMessageManager->ReceiveMessage(embedFrame, nullptr, aMessage, aIsSync, &aData, &cpows, aPrincipal, aRetVal);
  nsresult contentFrameReceived = contentFrameMessageManager->ReceiveMessage(embedFrame, nullptr, aMessage, aIsSync, &aData, &cpows, aPrincipal, aRetVal);

  bool globalOk = (globalReceived == NS_OK);
  bool contentFrameReceivedOk = (contentFrameReceived == NS_OK);

  if (!mView->HasMessageListener(aMessage)) {
    LOGE("Message not registered msg:%s\n", NS_ConvertUTF16toUTF8(aMessage).get());
    return (globalOk || contentFrameReceivedOk);
  }

  NS_ENSURE_TRUE(InitTabChildGlobal(), false);
  JSAutoRequest ar(aCx);

  // FIXME: Need callback interface for simple JSON to avoid useless conversion here
  JS::RootedValue rval(aCx);
  JS::StructuredCloneScope scope = JS::StructuredCloneScope::SameProcessSameThread;

  if (aData.DataLength() > 0 && !JS_ReadStructuredClone(aCx, aData.Data(),
                                                        JS_STRUCTURED_CLONE_VERSION,
                                                        scope,
                                                        &rval,
                                                        nullptr, nullptr)) {
    JS_ClearPendingException(aCx);
    return false;
  }

  nsAutoString json;
  NS_ENSURE_TRUE(JS_Stringify(aCx, &rval, nullptr, JS::NullHandleValue, EmbedLiteJSON::JSONCreator, &json), false);
  NS_ENSURE_TRUE(!json.IsEmpty(), false);

  // FIXME : Return value should be written to nsTArray<StructuredCloneData> *aRetVal
  InfallibleTArray<nsString> jsonRetVal;

  bool retValue = false;

  if (aIsSync) {
    retValue = mView->DoSendSyncMessage(nsString(aMessage).get(), json.get(), &jsonRetVal);
  } else {
    retValue = mView->DoCallRpcMessage(nsString(aMessage).get(), json.get(), &jsonRetVal);
  }

  if (retValue && aRetVal) {
    for (uint32_t i = 0; i < jsonRetVal.Length(); i++) {
      mozilla::dom::ipc::StructuredCloneData* cloneData = aRetVal->AppendElement();

      NS_ConvertUTF16toUTF8 data(jsonRetVal[i]);
      if (!cloneData->CopyExternalData(data.get(), data.Length())) {
        return false;
      }
    }
  }

  return (globalOk || contentFrameReceivedOk || retValue);
}

nsresult TabChildHelper::DoSendAsyncMessage(JSContext* aCx,
                                            const nsAString& aMessage,
                                            mozilla::dom::ipc::StructuredCloneData& aData,
                                            JS::Handle<JSObject *> aCpows,
                                            nsIPrincipal* aPrincipal)
{
  nsCOMPtr<nsIMessageBroadcaster> globalIMessageManager =
      do_GetService("@mozilla.org/globalmessagemanager;1");
  RefPtr<nsFrameMessageManager> globalMessageManager =
      static_cast<nsFrameMessageManager*>(globalIMessageManager.get());
  RefPtr<nsFrameMessageManager> contentFrameMessageManager =
      static_cast<nsFrameMessageManager*>(mTabChildGlobal->mMessageManager.get());

  nsCOMPtr<nsPIDOMWindowOuter> pwindow = do_GetInterface(WebNavigation());
  nsCOMPtr<nsIDOMWindow> window = do_QueryInterface(pwindow);
  RefPtr<EmbedFrame> embedFrame = new EmbedFrame();
  embedFrame->mWindow = window;
  embedFrame->mMessageManager = mTabChildGlobal;
  SameProcessCpowHolder cpows(JS::RootingContext::get(aCx), aCpows);

  globalMessageManager->ReceiveMessage(embedFrame, nullptr, aMessage, false, &aData, &cpows, aPrincipal, nullptr);
  contentFrameMessageManager->ReceiveMessage(embedFrame, nullptr, aMessage, false, &aData, &cpows, aPrincipal, nullptr);

  if (!mView->HasMessageListener(aMessage)) {
    LOGW("Message not registered msg:%s\n", NS_ConvertUTF16toUTF8(aMessage).get());
    return NS_OK;
  }

  if (!InitTabChildGlobal()) {
    return NS_ERROR_UNEXPECTED;
  }

  JSAutoRequest ar(aCx);
  JS::RootedValue rval(aCx);
  JS::StructuredCloneScope scope = JS::StructuredCloneScope::SameProcessSameThread;

  if (aData.DataLength() > 0 && !JS_ReadStructuredClone(aCx, aData.Data(),
                                                        JS_STRUCTURED_CLONE_VERSION,
                                                        scope,
                                                        &rval,
                                                        nullptr, nullptr)) {
    JS_ClearPendingException(aCx);
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoString json;
  // Check EmbedLiteJSON::JSONCreator and/or JS_Stringify from Android side
  if (!JS_Stringify(aCx, &rval, nullptr, JS::NullHandleValue, EmbedLiteJSON::JSONCreator, &json))  {
    return NS_ERROR_UNEXPECTED;
  }

  if (json.IsEmpty()) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!mView->DoSendAsyncMessage(nsString(aMessage).get(), json.get())) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

ScreenIntSize
TabChildHelper::GetInnerSize()
{
  return mInnerSize;
}

bool
TabChildHelper::ConvertMutiTouchInputToEvent(const mozilla::MultiTouchInput& aData,
                                             WidgetTouchEvent& aEvent)
{
  nsPoint offset;
  nsCOMPtr<nsIWidget> widget = GetWidget(&offset);
  if (!widget) {
    return false;
  }
  aEvent = aData.ToWidgetTouchEvent(widget);
  return true;
}

nsIWidget*
TabChildHelper::GetWidget(nsPoint* aOffset)
{
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  NS_ENSURE_TRUE(window, nullptr);
  nsIDocShell* docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, nullptr);
  nsCOMPtr<nsIPresShell> presShell = docShell->GetPresShell();
  NS_ENSURE_TRUE(presShell, nullptr);
  nsIFrame* frame = presShell->GetRootFrame();
  if (frame) {
    return frame->GetView()->GetWidget();
  }

  return nullptr;
}

nsPresContext*
TabChildHelper::GetPresContext()
{
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  NS_ENSURE_TRUE(window, nullptr);
  nsIDocShell* docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, nullptr);
  RefPtr<nsPresContext> presContext;
  docShell->GetPresContext(getter_AddRefs(presContext));
  return presContext;
}

bool
TabChildHelper::DoUpdateZoomConstraints(const uint32_t& aPresShellId,
                                        const ViewID& aViewId,
                                        const Maybe<mozilla::layers::ZoomConstraints> &aConstraints)
{
  LOGT();
  return mView->UpdateZoomConstraints(aPresShellId,
                                      aViewId,
                                      aConstraints);
}

void
TabChildHelper::ReportSizeUpdate(const LayoutDeviceIntRect &aRect)
{
  bool initialSizing = !HasValidInnerSize()
                    && (aRect.width != 0 && aRect.height != 0);
  if (initialSizing) {
    mHasValidInnerSize = true;
  }

  LayoutDeviceIntSize size = aRect.Size();
  mInnerSize = ViewAs<ScreenPixel>(size, PixelCastJustification::LayoutDeviceIsScreenForTabDims);
}

mozilla::CSSPoint
TabChildHelper::ApplyPointTransform(const LayoutDevicePoint& aPoint,
                                    const mozilla::layers::ScrollableLayerGuid& aGuid,
                                    bool *ok)
{
  nsCOMPtr<nsIPresShell> presShell = GetPresContext()->GetPresShell();
  if (!presShell) {
    if (ok)
      *ok = false;

    LOGT("Failed to transform layout device point -- no nsIPresShell");
    return mozilla::CSSPoint(0.0f, 0.0f);
  }

  if (!presShell->GetPresContext()) {
    if (ok)
      *ok = false;

    LOGT("Failed to transform layout device point -- no nsPresContext");
    return mozilla::CSSPoint(0.0f, 0.0f);
  }

  if (ok)
    *ok = true;

  mozilla::CSSToLayoutDeviceScale scale = presShell->GetPresContext()->CSSToDevPixelScale();
  return APZCCallbackHelper::ApplyCallbackTransform(aPoint / scale, aGuid);
}

// -- nsITabChild --------------

NS_IMETHODIMP
TabChildHelper::GetMessageManager(nsIContentFrameMessageManager** aResult)
{
  if (mTabChildGlobal) {
    NS_ADDREF(*aResult = mTabChildGlobal);
    return NS_OK;
  }
  *aResult = nullptr;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
TabChildHelper::GetWebBrowserChrome(nsIWebBrowserChrome3** aWebBrowserChrome)
{
  NS_IF_ADDREF(*aWebBrowserChrome = mWebBrowserChrome);
  return NS_OK;
}

NS_IMETHODIMP
TabChildHelper::SetWebBrowserChrome(nsIWebBrowserChrome3* aWebBrowserChrome)
{
  mWebBrowserChrome = aWebBrowserChrome;
  return NS_OK;
}

void
TabChildHelper::SendRequestFocus(bool aCanFocus)
{
  LOGNI();
}

void
TabChildHelper::SendGetTabCount(uint32_t* tabCount)
{
  Unused << tabCount;
  LOGNI();
}

NS_IMETHODIMP
TabChildHelper::RemoteSizeShellTo(int32_t aWidth, int32_t aHeight,
                            int32_t aShellItemWidth, int32_t aShellItemHeight)
{
  LOGNI();
  return NS_OK;
}

NS_IMETHODIMP
TabChildHelper::RemoteDropLinks(uint32_t aLinksCount, nsIDroppedLinkItem** aLinks)
{
  LOGNI();
  return NS_OK;
}

void
TabChildHelper::EnableDisableCommands(const nsAString& aAction,
                                      nsTArray<nsCString>& aEnabledCommands,
                                      nsTArray<nsCString>& aDisabledCommands)
{
  LOGNI();
}

NS_IMETHODIMP
TabChildHelper::GetTabId(uint64_t* aId)
{
  *aId = mView->GetID();
  return NS_OK;
}

// -- end of nsITabChild -------
