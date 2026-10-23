var METHOD_SPECS = {
    24: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    25: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    26: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    27: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    28: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    29: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    30: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    31: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    32: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    33: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    34: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    35: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 },
    36: { size: 32, jniCode:  8, quickCode: 16, accessFlags:  4, interpreterCode: -1 }
};

var THREAD_SPECS = {
    24: { self: 168, exception:  88, throwLocation: 104, topHandleScope: 144, managedStack: 120, isExceptionReportedToInstrumentation: 136 },
    25: { self: 168, exception:  88, throwLocation: 104, topHandleScope: 144, managedStack: 120, isExceptionReportedToInstrumentation: 136 },
    26: { self: 168, exception:  88, throwLocation: 104, topHandleScope: 144, managedStack: 120, isExceptionReportedToInstrumentation: 136 },
    27: { self: 168, exception:  88, throwLocation: 104, topHandleScope: 144, managedStack: 120, isExceptionReportedToInstrumentation: 136 },
    28: { self: 176, exception:  88, throwLocation: 104, topHandleScope: 152, managedStack: 120, isExceptionReportedToInstrumentation: 144 },
    29: { self: 176, exception:  88, throwLocation: 104, topHandleScope: 152, managedStack: 120, isExceptionReportedToInstrumentation: 144 },
    30: { self: 176, exception:  88, throwLocation: 104, topHandleScope: 152, managedStack: 120, isExceptionReportedToInstrumentation: 144 },
    31: { self: 184, exception:  88, throwLocation: 104, topHandleScope: 160, managedStack: 120, isExceptionReportedToInstrumentation: 152 },
    32: { self: 184, exception:  88, throwLocation: 104, topHandleScope: 160, managedStack: 120, isExceptionReportedToInstrumentation: 152 },
    33: { self: 184, exception:  88, throwLocation: 104, topHandleScope: 160, managedStack: 120, isExceptionReportedToInstrumentation: 152 },
    34: { self: 192, exception:  88, throwLocation: 104, topHandleScope: 168, managedStack: 120, isExceptionReportedToInstrumentation: 160 },
    35: { self: 192, exception:  88, throwLocation: 104, topHandleScope: 168, managedStack: 120, isExceptionReportedToInstrumentation: 160 },
    36: { self: 192, exception:  88, throwLocation: 104, topHandleScope: 168, managedStack: 120, isExceptionReportedToInstrumentation: 160 }
};

var CLASS_SPECS = {
    24: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    25: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    26: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    27: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    28: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    29: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    30: { ifields:  92, methods: 112, sfields: 116, copiedMethodsOffset: 120 },
    31: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 },
    32: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 },
    33: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 },
    34: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 },
    35: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 },
    36: { ifields: 100, methods: 120, sfields: 124, copiedMethodsOffset: 128 }
};

var RUNTIME_SPECS = {
    24: { heap: 200, threadList: 192, internTable: 184, classLinker: 176, jniIdManager: 0 },
    25: { heap: 200, threadList: 192, internTable: 184, classLinker: 176, jniIdManager: 0 },
    26: { heap: 200, threadList: 192, internTable: 184, classLinker: 176, jniIdManager: 0 },
    27: { heap: 200, threadList: 192, internTable: 184, classLinker: 176, jniIdManager: 0 },
    28: { heap: 208, threadList: 200, internTable: 192, classLinker: 184, jniIdManager: 0 },
    29: { heap: 208, threadList: 200, internTable: 192, classLinker: 184, jniIdManager: 0 },
    30: { heap: 216, threadList: 208, internTable: 200, classLinker: 192, jniIdManager: 0 },
    31: { heap: 216, threadList: 208, internTable: 200, classLinker: 192, jniIdManager: 240 },
    32: { heap: 216, threadList: 208, internTable: 200, classLinker: 192, jniIdManager: 240 },
    33: { heap: 224, threadList: 216, internTable: 208, classLinker: 200, jniIdManager: 248 },
    34: { heap: 224, threadList: 216, internTable: 208, classLinker: 200, jniIdManager: 248 },
    35: { heap: 224, threadList: 216, internTable: 208, classLinker: 200, jniIdManager: 248 },
    36: { heap: 224, threadList: 216, internTable: 208, classLinker: 200, jniIdManager: 248 }
};

var CLASS_LINKER_SPECS = {
    24: { quickResolutionTrampoline: 40, quickImtConflictTrampoline: 48, quickGenericJniTrampoline: 56, quickToInterpreterBridgeTrampoline: 64 },
    25: { quickResolutionTrampoline: 40, quickImtConflictTrampoline: 48, quickGenericJniTrampoline: 56, quickToInterpreterBridgeTrampoline: 64 },
    26: { quickResolutionTrampoline: 40, quickImtConflictTrampoline: 48, quickGenericJniTrampoline: 56, quickToInterpreterBridgeTrampoline: 64 },
    27: { quickResolutionTrampoline: 40, quickImtConflictTrampoline: 48, quickGenericJniTrampoline: 56, quickToInterpreterBridgeTrampoline: 64 },
    28: { quickResolutionTrampoline: 48, quickImtConflictTrampoline: 56, quickGenericJniTrampoline: 64, quickToInterpreterBridgeTrampoline: 72 },
    29: { quickResolutionTrampoline: 48, quickImtConflictTrampoline: 56, quickGenericJniTrampoline: 64, quickToInterpreterBridgeTrampoline: 72 },
    30: { quickResolutionTrampoline: 56, quickImtConflictTrampoline: 64, quickGenericJniTrampoline: 72, quickToInterpreterBridgeTrampoline: 80 },
    31: { quickResolutionTrampoline: 56, quickImtConflictTrampoline: 64, quickGenericJniTrampoline: 72, quickToInterpreterBridgeTrampoline: 80 },
    32: { quickResolutionTrampoline: 56, quickImtConflictTrampoline: 64, quickGenericJniTrampoline: 72, quickToInterpreterBridgeTrampoline: 80 },
    33: { quickResolutionTrampoline: 64, quickImtConflictTrampoline: 72, quickGenericJniTrampoline: 80, quickToInterpreterBridgeTrampoline: 88 },
    34: { quickResolutionTrampoline: 64, quickImtConflictTrampoline: 72, quickGenericJniTrampoline: 80, quickToInterpreterBridgeTrampoline: 88 },
    35: { quickResolutionTrampoline: 64, quickImtConflictTrampoline: 72, quickGenericJniTrampoline: 80, quickToInterpreterBridgeTrampoline: 88 },
    36: { quickResolutionTrampoline: 64, quickImtConflictTrampoline: 72, quickGenericJniTrampoline: 80, quickToInterpreterBridgeTrampoline: 88 }
};

function specFor(table, api) {
    if (table[api]) return table[api];
    var closest = null, minDiff = Infinity;
    for (var k in table) {
        if (!table.hasOwnProperty(k)) continue;
        var kn = parseInt(k, 10);
        var d = Math.abs(kn - api);
        if (d < minDiff) { minDiff = d; closest = kn; }
    }
    return closest !== null ? table[closest] : null;
}

handlers["art_spec.method"] = function () {
    var api = bridgeInfo.apiLevel || 34;
    var spec = specFor(METHOD_SPECS, api);
    if (!spec) throw new Error("no method spec for api " + api);
    return { api: api, spec: spec };
};

handlers["art_spec.thread"] = function () {
    var api = bridgeInfo.apiLevel || 34;
    var spec = specFor(THREAD_SPECS, api);
    if (!spec) throw new Error("no thread spec for api " + api);
    return { api: api, spec: spec };
};

handlers["art_spec.class"] = function () {
    var api = bridgeInfo.apiLevel || 34;
    var spec = specFor(CLASS_SPECS, api);
    if (!spec) throw new Error("no class spec for api " + api);
    return { api: api, spec: spec };
};

handlers["art_spec.runtime"] = function () {
    var api = bridgeInfo.apiLevel || 34;
    var spec = specFor(RUNTIME_SPECS, api);
    if (!spec) throw new Error("no runtime spec for api " + api);
    return { api: api, spec: spec };
};

handlers["art_spec.classLinker"] = function () {
    var api = bridgeInfo.apiLevel || 34;
    var spec = specFor(CLASS_LINKER_SPECS, api);
    if (!spec) throw new Error("no class linker spec for api " + api);
    return { api: api, spec: spec };
};
