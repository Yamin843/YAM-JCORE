// scripts/bootstrap.js — entrypoint for the bootstrap agent.
//
// This file is NOT loaded directly. scripts/build_bootstrap.py
// concatenates it with the files in scripts/lib/ (in dependency order)
// and writes the result to Generated/bootstrap_js.h.
//
// The marker {{LIBS}} is replaced by the concatenation of:
//
//   util.js         — handlers map + helpers (safeString, toJsonSafe, …)
//   send_auto.js    — chunked send wrapper
//   bridge_probe.js — bridgeInfo
//   handle_mgr.js   — retained objects, castHandle
//   hook_mgr.js     — installed hooks registry
//   art_info.js     — art_spec handlers
//   jvmti_ops.js    — jvmti handlers
//   heap_ops.js     — heap.choose / visit_objects
//   backtrace_ops.js— backtrace handlers
//   class_ops.js    — bridge.class.*
//   hook_ops.js     — bridge.hook.*
//   call_ops.js     — bridge.call.*
//   field_ops.js    — bridge.field.*
//   handle_ops.js   — bridge.handle.*
//   convert_ops.js  — bridge.convert.* / bridge.type.*
//   dispatcher.js   — recv("cmd") + capabilities event
//
// The concatenation happens inside a single IIFE so that all libs share
// scope and cannot be seen from outside the bootstrap runtime.
(function () {
"use strict";

{{LIBS}}

})();
