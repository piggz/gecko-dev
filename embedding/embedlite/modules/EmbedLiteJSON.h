/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EmbedLiteJSON_H_
#define EmbedLiteJSON_H_

#include "nsIEmbedLiteJSON.h"
#include "mozilla/ModuleUtils.h"               // for NS_GENERIC_FACTORY_CONSTRUCTOR

class EmbedLiteJSON : public nsIEmbedLiteJSON
{
public:
  EmbedLiteJSON();
  NS_DECL_ISUPPORTS
  NS_DECL_NSIEMBEDLITEJSON

  static bool JSONCreator(const char16_t* aBuf, uint32_t aLen, void* aData);

protected:
  virtual ~EmbedLiteJSON();
};

#define NS_EMBED_LITE_JSON_CONTRACTID "@mozilla.org/embedlite-json;1"
#define NS_EMBED_LITE_JSON_SERVICE_CLASSNAME "EmbedLite JSON Component"
#define NS_EMBED_LITE_JSON_SERVICE_CID NS_IEMBEDLITEJSON_IID

#endif /* EmbedLiteJSON_H_ */

