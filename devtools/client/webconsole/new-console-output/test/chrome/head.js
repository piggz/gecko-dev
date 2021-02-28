/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* exported Task, browserRequire */

"use strict";

var { require } = ChromeUtils.import("resource://devtools/shared/Loader.jsm", {});
var { BrowserLoader } = ChromeUtils.import("resource://devtools/client/shared/browser-loader.js", {});
var { Task } = require("devtools/shared/task");

var { require: browserRequire } = BrowserLoader({
  baseURI: "resource://devtools/client/webconsole/",
  window
});