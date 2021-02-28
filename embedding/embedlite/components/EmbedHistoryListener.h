/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EmbedHistoryListener_H_
#define EmbedHistoryListener_H_

#include "mozilla/IHistory.h"
#include "nsDataHashtable.h"
#include "nsTPriorityQueue.h"
#include "nsIRunnable.h"
#include "nsIEmbedAppService.h"
#include "nsIObserver.h"
#include "nsIURI.h"
#include "nsITimer.h"

#define NS_EMBEDLITEHISTORY_CID \
{ 0xec7cf1e2, \
  0x6e88, \
  0x11e2, \
  { 0xa7, 0x9a, 0xfb, 0x19, 0xfe, 0x29, 0x97 }}

// Max size of History::mRecentlyVisitedURIs
#define RECENTLY_VISITED_URI_SIZE 8

// Max size of History::mEmbedURIs
#define EMBED_URI_SIZE 128

class EmbedHistoryListener : public mozilla::IHistory
                           , public nsIRunnable
                           , public nsIObserver
                           , public nsITimerCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_IHISTORY
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK

  nsresult Init() { return NS_OK; }

  /**
   * Obtains a pointer that has had AddRef called on it.  Used by the service
   * manager only.
   */
  static already_AddRefed<EmbedHistoryListener> GetSingleton();

  EmbedHistoryListener();

private:
  virtual ~EmbedHistoryListener() {}
  nsIEmbedAppService* GetService();

  static EmbedHistoryListener* sHistory;

  // Will mimic the value of the places.history.enabled preference.
  bool mHistoryEnabled;

  void LoadPrefs();
  bool ShouldRecordHistory();
  nsresult CanAddURI(nsIURI* aURI, bool* canAdd);

  /**
   * We need to manage data used to determine a:visited status.
   */
  nsDataHashtable<nsStringHashKey, nsTArray<mozilla::dom::Link *> *> mListeners;
  nsTPriorityQueue<nsString> mPendingLinkURIs;

  /**
   * Redirection (temporary and permanent) flags are sent with the redirected
   * URI, not the original URI. Since we want to ignore the original URI, we
   * need to cache the pending visit and make sure it doesn't redirect.
   */
  RefPtr<nsITimer> mTimer;
  typedef AutoTArray<nsCOMPtr<nsIURI>, RECENTLY_VISITED_URI_SIZE> PendingVisitArray;
  PendingVisitArray mPendingVisitURIs;

  bool RemovePendingVisitURI(nsIURI* aURI);
  void SaveVisitURI(nsIURI* aURI);

  /**
   * mRecentlyVisitedURIs remembers URIs which are recently added to the DB,
   * to avoid saving these locations repeatedly in a short period.
   */
  typedef AutoTArray<nsCOMPtr<nsIURI>, RECENTLY_VISITED_URI_SIZE> RecentlyVisitedArray;
  RecentlyVisitedArray mRecentlyVisitedURIs;
  RecentlyVisitedArray::index_type mRecentlyVisitedURIsNextIndex;

  void AppendToRecentlyVisitedURIs(nsIURI* aURI);
  bool IsRecentlyVisitedURI(nsIURI* aURI);

  /**
   * mEmbedURIs remembers URIs which are explicitly not added to the DB,
   * to avoid wasting time on these locations.
   */
  typedef AutoTArray<nsCOMPtr<nsIURI>, EMBED_URI_SIZE> EmbedArray;
  EmbedArray::index_type mEmbedURIsNextIndex;
  EmbedArray mEmbedURIs;

  void AppendToEmbedURIs(nsIURI* aURI);
  bool IsEmbedURI(nsIURI* aURI);

  nsCOMPtr<nsIEmbedAppService> mService;
};

#define NS_EMBED_HISTORY_CONTRACTID "@mozilla.org/embed-history-component;1"
#define NS_EMBED_HISTORY_SERVICE_CLASSNAME "Embed History Listener Component"
#define NS_EMBED_HISTORY_SERVICE_CID NS_EMBEDLITEHISTORY_CID

#endif /*EmbedHistoryListener_H_*/
