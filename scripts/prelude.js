// scripts/prelude.js — intentionally empty.
//
// The C++ bootstrap path signals prelude_ready from the load
// callback, so the JS side does not need to send anything. Keeping
// prelude empty eliminates any dependency on send() and on the
// Java bridge during early initialization.
(function () {
    "use strict";
    // no-op
})();
