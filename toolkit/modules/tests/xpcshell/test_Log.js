/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-disable block-spacing */

ChromeUtils.import("resource://gre/modules/NetUtil.jsm");
ChromeUtils.import("resource://gre/modules/osfile.jsm");

ChromeUtils.import("resource://gre/modules/Log.jsm");

var testFormatter = {
  format: function format(message) {
    return message.loggerName + "\t" +
      message.levelDesc + "\t" +
      message.message;
  }
};

function MockAppender(formatter) {
  Log.Appender.call(this, formatter);
  this.messages = [];
}
MockAppender.prototype = {
  __proto__: Log.Appender.prototype,

  doAppend: function DApp_doAppend(message) {
    this.messages.push(message);
  }
};

add_task(function test_Logger() {
  let log = Log.repository.getLogger("test.logger");
  let appender = new MockAppender(new Log.BasicFormatter());

  log.level = Log.Level.Debug;
  appender.level = Log.Level.Info;
  log.addAppender(appender);
  log.info("info test");
  log.debug("this should be logged but not appended.");

  Assert.equal(appender.messages.length, 1);

  let msgRe = /\d+\ttest.logger\t\INFO\tinfo test/;
  Assert.ok(msgRe.test(appender.messages[0]));
});

add_task(function test_Logger_parent() {
  // Check whether parenting is correct
  let grandparentLog = Log.repository.getLogger("grandparent");
  let childLog = Log.repository.getLogger("grandparent.parent.child");
  Assert.equal(childLog.parent.name, "grandparent");

  Log.repository.getLogger("grandparent.parent");
  Assert.equal(childLog.parent.name, "grandparent.parent");

  // Check that appends are exactly in scope
  let gpAppender = new MockAppender(new Log.BasicFormatter());
  gpAppender.level = Log.Level.Info;
  grandparentLog.addAppender(gpAppender);
  childLog.info("child info test");
  Log.repository.rootLogger.info("this shouldn't show up in gpAppender");

  Assert.equal(gpAppender.messages.length, 1);
  Assert.ok(gpAppender.messages[0].indexOf("child info test") > 0);
});

add_test(function test_LoggerWithMessagePrefix() {
  let log = Log.repository.getLogger("test.logger.prefix");
  let appender = new MockAppender(new Log.MessageOnlyFormatter());
  log.addAppender(appender);

  let prefixed = Log.repository.getLoggerWithMessagePrefix(
    "test.logger.prefix", "prefix: ");

  log.warn("no prefix");
  prefixed.warn("with prefix");

  Assert.equal(appender.messages.length, 2, "2 messages were logged.");
  Assert.deepEqual(appender.messages, [
    "no prefix",
    "prefix: with prefix",
  ], "Prefix logger works.");

  run_next_test();
});

/*
 * A utility method for checking object equivalence.
 * Fields with a reqular expression value in expected will be tested
 * against the corresponding value in actual. Otherwise objects
 * are expected to have the same keys and equal values.
 */
function checkObjects(expected, actual) {
  Assert.ok(expected instanceof Object);
  Assert.ok(actual instanceof Object);
  for (let key in expected) {
    Assert.notEqual(actual[key], undefined);
    if (expected[key] instanceof RegExp) {
      Assert.ok(expected[key].test(actual[key].toString()));
    } else if (expected[key] instanceof Object) {
      checkObjects(expected[key], actual[key]);
    } else {
      Assert.equal(expected[key], actual[key]);
    }
  }

  for (let key in actual) {
    Assert.notEqual(expected[key], undefined);
  }
}

add_task(function test_StructuredLogCommands() {
  let appender = new MockAppender(new Log.StructuredFormatter());
  let logger = Log.repository.getLogger("test.StructuredOutput");
  logger.addAppender(appender);
  logger.level = Log.Level.Info;

  logger.logStructured("test_message", {_message: "message string one"});
  logger.logStructured("test_message", {_message: "message string two",
                                        _level: "ERROR",
                                        source_file: "test_Log.js"});
  logger.logStructured("test_message");
  logger.logStructured("test_message", {source_file: "test_Log.js",
                                        message_position: 4});

  let messageOne = {"_time": /\d+/,
                    "_namespace": "test.StructuredOutput",
                    "_level": "INFO",
                    "_message": "message string one",
                    "action": "test_message"};

  let messageTwo = {"_time": /\d+/,
                    "_namespace": "test.StructuredOutput",
                    "_level": "ERROR",
                    "_message": "message string two",
                    "action": "test_message",
                    "source_file": "test_Log.js"};

  let messageThree = {"_time": /\d+/,
                      "_namespace": "test.StructuredOutput",
                      "_level": "INFO",
                      "action": "test_message"};

  let messageFour = {"_time": /\d+/,
                     "_namespace": "test.StructuredOutput",
                     "_level": "INFO",
                     "action": "test_message",
                     "source_file": "test_Log.js",
                     "message_position": 4};

  checkObjects(messageOne, JSON.parse(appender.messages[0]));
  checkObjects(messageTwo, JSON.parse(appender.messages[1]));
  checkObjects(messageThree, JSON.parse(appender.messages[2]));
  checkObjects(messageFour, JSON.parse(appender.messages[3]));

  let errored = false;
  try {
    logger.logStructured("", {_message: "invalid message"});
  } catch (e) {
    errored = true;
    Assert.equal(e, "An action is required when logging a structured message.");
  } finally {
    Assert.ok(errored);
  }

  errored = false;
  try {
    logger.logStructured("message_action", "invalid params");
  } catch (e) {
    errored = true;
    Assert.equal(e, "The params argument is required to be an object.");
  } finally {
    Assert.ok(errored);
  }

  // Logging with unstructured interface should produce the same messages
  // as the structured interface for these cases.
  appender = new MockAppender(new Log.StructuredFormatter());
  logger = Log.repository.getLogger("test.StructuredOutput1");
  messageOne._namespace = "test.StructuredOutput1";
  messageTwo._namespace = "test.StructuredOutput1";
  logger.addAppender(appender);
  logger.level = Log.Level.All;
  logger.info("message string one", {action: "test_message"});
  logger.error("message string two", {action: "test_message",
                                      source_file: "test_Log.js"});

  checkObjects(messageOne, JSON.parse(appender.messages[0]));
  checkObjects(messageTwo, JSON.parse(appender.messages[1]));
});

add_task(function test_StorageStreamAppender() {
  let appender = new Log.StorageStreamAppender(testFormatter);
  Assert.equal(appender.getInputStream(), null);

  // Log to the storage stream and verify the log was written and can be
  // read back.
  let logger = Log.repository.getLogger("test.StorageStreamAppender");
  logger.addAppender(appender);
  logger.info("OHAI");
  let inputStream = appender.getInputStream();
  let data = NetUtil.readInputStreamToString(inputStream,
                                             inputStream.available());
  Assert.equal(data, "test.StorageStreamAppender\tINFO\tOHAI\n");

  // We can read it again even.
  let sndInputStream = appender.getInputStream();
  let sameData = NetUtil.readInputStreamToString(sndInputStream,
                                                 sndInputStream.available());
  Assert.equal(data, sameData);

  // Reset the appender and log some more.
  appender.reset();
  Assert.equal(appender.getInputStream(), null);
  logger.debug("wut?!?");
  inputStream = appender.getInputStream();
  data = NetUtil.readInputStreamToString(inputStream,
                                         inputStream.available());
  Assert.equal(data, "test.StorageStreamAppender\tDEBUG\twut?!?\n");
});

function fileContents(path) {
  let decoder = new TextDecoder();
  return OS.File.read(path).then(array => {
    return decoder.decode(array);
  });
}

add_task(async function test_FileAppender() {
  // This directory does not exist yet
  let dir = OS.Path.join(do_get_profile().path, "test_Log");
  Assert.equal(false, await OS.File.exists(dir));
  let path = OS.Path.join(dir, "test_FileAppender");
  let appender = new Log.FileAppender(path, testFormatter);
  let logger = Log.repository.getLogger("test.FileAppender");
  logger.addAppender(appender);

  // Logging to a file that can't be created won't do harm.
  Assert.equal(false, await OS.File.exists(path));
  logger.info("OHAI!");

  await OS.File.makeDir(dir);
  logger.info("OHAI");
  await appender._lastWritePromise;

  Assert.equal((await fileContents(path)),
               "test.FileAppender\tINFO\tOHAI\n");

  logger.info("OHAI");
  await appender._lastWritePromise;

  Assert.equal((await fileContents(path)),
               "test.FileAppender\tINFO\tOHAI\n" +
               "test.FileAppender\tINFO\tOHAI\n");

  // Reset the appender and log some more.
  await appender.reset();
  Assert.equal(false, await OS.File.exists(path));

  logger.debug("O RLY?!?");
  await appender._lastWritePromise;
  Assert.equal((await fileContents(path)),
               "test.FileAppender\tDEBUG\tO RLY?!?\n");

  await appender.reset();
  logger.debug("1");
  logger.info("2");
  logger.info("3");
  logger.info("4");
  logger.info("5");
  // Waiting on only the last promise should account for all of these.
  await appender._lastWritePromise;

  // Messages ought to be logged in order.
  Assert.equal((await fileContents(path)),
               "test.FileAppender\tDEBUG\t1\n" +
               "test.FileAppender\tINFO\t2\n" +
               "test.FileAppender\tINFO\t3\n" +
               "test.FileAppender\tINFO\t4\n" +
               "test.FileAppender\tINFO\t5\n");
});

add_task(async function test_BoundedFileAppender() {
  let dir = OS.Path.join(do_get_profile().path, "test_Log");

  if (!(await OS.File.exists(dir))) {
    await OS.File.makeDir(dir);
  }

  let path = OS.Path.join(dir, "test_BoundedFileAppender");
  // This appender will hold about two lines at a time.
  let appender = new Log.BoundedFileAppender(path, testFormatter, 40);
  let logger = Log.repository.getLogger("test.BoundedFileAppender");
  logger.addAppender(appender);

  logger.info("ONE");
  logger.info("TWO");
  await appender._lastWritePromise;

  Assert.equal((await fileContents(path)),
               "test.BoundedFileAppender\tINFO\tONE\n" +
               "test.BoundedFileAppender\tINFO\tTWO\n");

  logger.info("THREE");
  logger.info("FOUR");

  Assert.notEqual(appender._removeFilePromise, undefined);
  await appender._removeFilePromise;
  await appender._lastWritePromise;

  Assert.equal((await fileContents(path)),
               "test.BoundedFileAppender\tINFO\tTHREE\n" +
               "test.BoundedFileAppender\tINFO\tFOUR\n");

  await appender.reset();
  logger.info("ONE");
  logger.info("TWO");
  logger.info("THREE");
  logger.info("FOUR");

  Assert.notEqual(appender._removeFilePromise, undefined);
  await appender._removeFilePromise;
  await appender._lastWritePromise;

  Assert.equal((await fileContents(path)),
               "test.BoundedFileAppender\tINFO\tTHREE\n" +
               "test.BoundedFileAppender\tINFO\tFOUR\n");

});

/*
 * Test parameter formatting.
 */
add_task(async function log_message_with_params() {
  let formatter = new Log.BasicFormatter();

  function formatMessage(text, params) {
    let full = formatter.format(new Log.LogMessage("test.logger", Log.Level.Warn, text, params));
    return full.split("\t")[3];
  }

  // Strings are substituted directly.
  Assert.equal(formatMessage("String is ${foo}", {foo: "bar"}),
               "String is bar");

  // Numbers are substituted.
  Assert.equal(formatMessage("Number is ${number}", {number: 47}),
               "Number is 47");

  // The entire params object is JSON-formatted and substituted.
  Assert.equal(formatMessage("Object is ${}", {foo: "bar"}),
               'Object is {"foo":"bar"}');

  // An object nested inside params is JSON-formatted and substituted.
  Assert.equal(formatMessage("Sub object is ${sub}", {sub: {foo: "bar"}}),
                 'Sub object is {"foo":"bar"}');

  // The substitution field is missing from params. Leave the placeholder behind
  // to make the mistake obvious.
  Assert.equal(formatMessage("Missing object is ${missing}", {}),
               "Missing object is ${missing}");

  // Make sure we don't treat the parameter name 'false' as a falsey value.
  Assert.equal(formatMessage("False is ${false}", {false: true}),
               "False is true");

  // If an object has a .toJSON method, the formatter uses it.
  let ob = function() {};
  ob.toJSON = function() {return {sneaky: "value"};};
  Assert.equal(formatMessage("JSON is ${sub}", {sub: ob}),
               'JSON is {"sneaky":"value"}');

  // Fall back to .toSource() if JSON.stringify() fails on an object.
  ob = function() {};
  ob.toJSON = function() {throw "oh noes JSON";};
  Assert.equal(formatMessage("Fail is ${sub}", {sub: ob}),
               "Fail is (function() {})");

  // Fall back to .toString if both .toJSON and .toSource fail.
  ob.toSource = function() {throw "oh noes SOURCE";};
  Assert.equal(formatMessage("Fail is ${sub}", {sub: ob}),
               "Fail is function() {}");

  // Fall back to '[object]' if .toJSON, .toSource and .toString fail.
  ob.toString = function() {throw "oh noes STRING";};
  Assert.equal(formatMessage("Fail is ${sub}", {sub: ob}),
               "Fail is [object]");

  // If params are passed but there are no substitution in the text
  // we JSON format and append the entire parameters object.
  Assert.equal(formatMessage("Text with no subs", {a: "b", c: "d"}),
               'Text with no subs: {"a":"b","c":"d"}');

  // If we substitute one parameter but not the other,
  // we ignore any params that aren't substituted.
  Assert.equal(formatMessage("Text with partial sub ${a}", {a: "b", c: "d"}),
               "Text with partial sub b");

  // We don't format internal fields stored in params.
  Assert.equal(formatMessage("Params with _ ${}", {a: "b", _c: "d", _level: 20, _message: "froo",
                                                   _time: 123456, _namespace: "here.there"}),
               'Params with _ {"a":"b","_c":"d"}');

  // Don't print an empty params holder if all params are internal.
  Assert.equal(formatMessage("All params internal", {_level: 20, _message: "froo",
                                                     _time: 123456, _namespace: "here.there"}),
               "All params internal");

  // Format params with null and undefined values.
  Assert.equal(formatMessage("Null ${n} undefined ${u}", {n: null, u: undefined}),
               "Null null undefined undefined");

  // Format params with number, bool, and String type.
  Assert.equal(formatMessage("number ${n} boolean ${b} boxed Boolean ${bx} String ${s}",
                             {n: 45, b: false, bx: Boolean(true), s: String("whatevs")}),
               "number 45 boolean false boxed Boolean true String whatevs");

  /*
   * Check that errors get special formatting if they're formatted directly as
   * a named param or they're the only param, but not if they're a field in a
   * larger structure.
   */
  let err = Components.Exception("test exception", Cr.NS_ERROR_FAILURE);
  let str = formatMessage("Exception is ${}", err);
  Assert.ok(str.includes('Exception is [Exception... "test exception"'));
  Assert.ok(str.includes("(NS_ERROR_FAILURE)"));
  str = formatMessage("Exception is", err);
  Assert.ok(str.includes('Exception is: [Exception... "test exception"'));
  str = formatMessage("Exception is ${error}", {error: err});
  Assert.ok(str.includes('Exception is [Exception... "test exception"'));
  str = formatMessage("Exception is", {_error: err});
  info(str);
  // Exceptions buried inside objects are formatted badly.
  Assert.ok(str.includes('Exception is: {"_error":{}'));
  // If the message text is null, the message contains only the formatted params object.
  str = formatMessage(null, err);
  Assert.ok(str.startsWith('[Exception... "test exception"'));
  // If the text is null and 'params' is a string, the message is exactly that string.
  str = formatMessage(null, "String in place of params");
  Assert.equal(str, "String in place of params");

  // We use object.valueOf() internally; make sure a broken valueOf() method
  // doesn't cause the logger to fail.
  /* eslint-disable object-shorthand */
  let vOf = {a: 1, valueOf: function() {throw "oh noes valueOf";}};
  Assert.equal(formatMessage("Broken valueOf ${}", vOf),
               'Broken valueOf ({a:1, valueOf:(function() {throw "oh noes valueOf";})})');
  /* eslint-enable object-shorthand */

  // Test edge cases of bad data to formatter:
  // If 'params' is not an object, format it as a basic type.
  Assert.equal(formatMessage("non-object no subst", 1),
               "non-object no subst: 1");
  Assert.equal(formatMessage("non-object all subst ${}", 2),
               "non-object all subst 2");
  Assert.equal(formatMessage("false no subst", false),
               "false no subst: false");
  Assert.equal(formatMessage("null no subst", null),
               "null no subst: null");
  // If 'params' is undefined and there are no substitutions expected,
  // the message should still be output.
  Assert.equal(formatMessage("undefined no subst", undefined),
               "undefined no subst");
  // If 'params' is not an object, no named substitutions can succeed;
  // therefore we leave the placeholder and append the formatted params.
  Assert.equal(formatMessage("non-object named subst ${junk} space", 3),
               "non-object named subst ${junk} space: 3");
  // If there are no params, we leave behind the placeholders in the text.
  Assert.equal(formatMessage("no params ${missing}", undefined),
               "no params ${missing}");
  // If params doesn't contain any of the tags requested in the text,
  // we leave them all behind and append the formatted params.
  Assert.equal(formatMessage("object missing tag ${missing} space", {mising: "not here"}),
               'object missing tag ${missing} space: {"mising":"not here"}');
  // If we are given null text and no params, the resulting formatted message is empty.
  Assert.equal(formatMessage(null), "");
});

/*
 * If we call a log function with a non-string object in place of the text
 * argument, and no parameters, treat that the same as logging empty text
 * with the object argument as parameters. This makes the log useful when the
 * caller does "catch(err) {logger.error(err)}"
 */
add_task(async function test_log_err_only() {
  let log = Log.repository.getLogger("error.only");
  let mockFormatter = { format: msg => msg };
  let appender = new MockAppender(mockFormatter);
  log.addAppender(appender);

  /*
   * Check that log.error(err) is treated the same as
   * log.error(null, err) by the logMessage constructor; the formatMessage()
   * tests above ensure that the combination of null text and an error object
   * is formatted correctly.
   */
  try {
    // eslint-disable-next-line no-eval
    eval("javascript syntax error");
  } catch (e) {
    log.error(e);
    let msg = appender.messages.pop();
    Assert.equal(msg.message, null);
    Assert.equal(msg.params, e);
  }
});

/*
 * Test logStructured() messages through basic formatter.
 */
add_task(async function test_structured_basic() {
  let log = Log.repository.getLogger("test.logger");
  let appender = new MockAppender(new Log.BasicFormatter());

  log.level = Log.Level.Info;
  appender.level = Log.Level.Info;
  log.addAppender(appender);

  // A structured entry with no _message is treated the same as log./level/(null, params)
  // except the 'action' field is added to the object.
  log.logStructured("action", {data: "structure"});
  Assert.equal(appender.messages.length, 1);
  Assert.ok(appender.messages[0].includes('{"data":"structure","action":"action"}'));

  // A structured entry with _message and substitution is treated the same as
  // log./level/(null, params).
  log.logStructured("action", {_message: "Structured sub ${data}", data: "structure"});
  Assert.equal(appender.messages.length, 2);
  info(appender.messages[1]);
  Assert.ok(appender.messages[1].includes("Structured sub structure"));
});

/*
 * Test that all the basic logger methods pass the message and params through to the appender.
 */
add_task(async function log_message_with_params() {
  let log = Log.repository.getLogger("error.logger");
  let mockFormatter = { format: msg => msg };
  let appender = new MockAppender(mockFormatter);
  log.addAppender(appender);

  let testParams = {a: 1, b: 2};
  log.fatal("Test fatal", testParams);
  log.error("Test error", testParams);
  log.warn("Test warn", testParams);
  log.info("Test info", testParams);
  log.config("Test config", testParams);
  log.debug("Test debug", testParams);
  log.trace("Test trace", testParams);
  Assert.equal(appender.messages.length, 7);
  for (let msg of appender.messages) {
    Assert.ok(msg.params === testParams);
    Assert.ok(msg.message.startsWith("Test "));
  }
});

/*
 * Check that we format JS Errors reasonably.
 * This needs to stay a generator to exercise Task.jsm's stack rewriting.
 */
add_task(function* format_errors() {
  let pFormat = new Log.ParameterFormatter();

  // Test that subclasses of Error are recognized as errors.
  let err = new ReferenceError("Ref Error", "ERROR_FILE", 28);
  let str = pFormat.format(err);
  Assert.ok(str.includes("ReferenceError"));
  Assert.ok(str.includes("ERROR_FILE:28"));
  Assert.ok(str.includes("Ref Error"));

  // Test that JS-generated Errors are recognized and formatted.
  try {
    yield Promise.resolve(); // Scrambles the stack
    // eslint-disable-next-line no-eval
    eval("javascript syntax error");
  } catch (e) {
    str = pFormat.format(e);
    Assert.ok(str.includes("SyntaxError: unexpected token"));
    // Make sure we identified it as an Error and formatted the error location as
    // lineNumber:columnNumber.
    Assert.ok(str.includes(":1:11)"));
    // Make sure that we use human-readable stack traces
    // Check that the error doesn't contain any reference to "Promise.jsm" or "Task.jsm"
    Assert.ok(!str.includes("Promise.jsm"));
    Assert.ok(!str.includes("Task.jsm"));
    Assert.ok(str.includes("format_errors"));
  }
});

/*
 * Check that automatic support for setting the log level via preferences
 * works.
 */
add_test(function test_prefs() {
  let log = Log.repository.getLogger("error.logger");
  log.level = Log.Level.Debug;
  Services.prefs.setStringPref("logger.test", "Error");
  log.manageLevelFromPref("logger.test");
  // check initial pref value is set up.
  equal(log.level, Log.Level.Error);
  Services.prefs.setStringPref("logger.test", "Error");
  // check changing the pref causes the level to change.
  Services.prefs.setStringPref("logger.test", "Trace");
  equal(log.level, Log.Level.Trace);
  // invalid values cause nothing to happen.
  Services.prefs.setStringPref("logger.test", "invalid-level-value");
  equal(log.level, Log.Level.Trace);
  Services.prefs.setIntPref("logger.test", 123);
  equal(log.level, Log.Level.Trace);
  // setting a "sub preference" shouldn't impact it.
  Services.prefs.setStringPref("logger.test.foo", "Debug");
  equal(log.level, Log.Level.Trace);
  // clearing the level param should cause it to use the parent level.
  Log.repository.getLogger("error").level = Log.Level.All;
  Services.prefs.setStringPref("logger.test", "");
  equal(log.level, Log.Level.All);

  run_next_test();
});