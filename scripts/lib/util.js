var handlers = {};


// ═══════════════════════════════════════════════════════════════════
//  Generic-aware collection and map construction
// ═══════════════════════════════════════════════════════════════════
//
// Java generics are erased at runtime. To build a value that satisfies
// a method expecting e.g. List<String>, the JS side must know the
// element type. We get it from a `genericTypes` hint that the C++ side
// extracts from the method's declared parameter types via reflection
// (see bridge/class_browser.cpp). The hint is a string like
// "java.util.List<java.lang.String>" or "java.util.Map<K,V>".

function parseGenericArgs(typeName) {
    // "java.util.Map<K, V>" → ["K", "V"]
    // "java.util.List<String>" → ["String"]
    // "java.util.List" → []
    var lt = typeName.indexOf("<");
    if (lt === -1) return { base: typeName.trim(), args: [] };
    var base = typeName.substring(0, lt).trim();
    // Handle nested generics by counting angle brackets.
    var depth = 0;
    var start = lt + 1;
    var args = [];
    for (var i = lt; i < typeName.length; i++) {
        var c = typeName.charAt(i);
        if (c === "<") { depth++; continue; }
        if (c === ">") {
            depth--;
            if (depth === 0) {
                args.push(typeName.substring(start, i).trim());
                break;
            }
            continue;
        }
        if (c === "," && depth === 1) {
            args.push(typeName.substring(start, i).trim());
            start = i + 1;
        }
    }
    return { base: base, args: args };
}

function buildTypedCollection(elements, baseType, argTypes) {
    // Choose concrete class from baseType.
    var concrete = baseType;
    if (baseType === "java.util.Collection" ||
        baseType === "java.util.List" ||
        baseType === "java.util.Queue" ||
        baseType === "java.util.Deque") {
        concrete = "java.util.ArrayList";
    } else if (baseType === "java.util.Set" ||
               baseType === "java.util.SortedSet" ||
               baseType === "java.util.NavigableSet") {
        concrete = "java.util.LinkedHashSet";
    } else if (baseType === "java.util.SortedMap" ||
               baseType === "java.util.NavigableMap") {
        concrete = "java.util.TreeMap";
    }

    var C = Java.use(concrete);
    var result = C.$new();

    // If no element type is known, fall back to raw insertion.
    if (!argTypes || argTypes.length === 0) {
        for (var i = 0; i < elements.length; i++) {
            result.add(toJavaForCollection(elements[i], null, null));
        }
        return result;
    }

    var elemType = argTypes[0];
    var parsedElem = parseGenericArgs(elemType);

    for (var j = 0; j < elements.length; j++) {
        var converted = toJavaForCollection(elements[j], parsedElem.base, parsedElem.args);
        result.add(converted);
    }
    return result;
}

function buildTypedMap(entries, baseType, argTypes) {
    var concrete = baseType;
    if (baseType === "java.util.Map" ||
        baseType === "java.util.SortedMap" ||
        baseType === "java.util.NavigableMap") {
        concrete = "java.util.LinkedHashMap";
    }

    var C = Java.use(concrete);
    var result = C.$new();

    if (!argTypes || argTypes.length < 2) {
        for (var key in entries) {
            if (!entries.hasOwnProperty(key)) continue;
            result.put(key, toJavaForCollection(entries[key], null, null));
        }
        return result;
    }

    var keyType   = parseGenericArgs(argTypes[0]);
    var valueType = parseGenericArgs(argTypes[1]);

    for (var k in entries) {
        if (!entries.hasOwnProperty(k)) continue;
        var convertedKey   = toJavaForCollection(k, keyType.base, keyType.args);
        var convertedValue = toJavaForCollection(entries[k], valueType.base, valueType.args);
        result.put(convertedKey, convertedValue);
    }
    return result;
}

// Helper for collection/map element conversion. Falls back to the
// existing fromJsonForJava for primitive types and Java objects.
function toJavaForCollection(value, baseType, argTypes) {
    if (value === null || value === undefined) return null;

    // Recurse into nested collections.
    if (baseType === "java.util.List" ||
        baseType === "java.util.ArrayList" ||
        baseType === "java.util.Collection" ||
        baseType === "java.util.Set") {
        if (!Array.isArray(value)) return fromJsonForJava(value, baseType, Java.vm.getEnv());
        return buildTypedCollection(value, baseType, argTypes);
    }

    if (baseType === "java.util.Map" ||
        baseType === "java.util.HashMap" ||
        baseType === "java.util.LinkedHashMap") {
        if (typeof value !== "object" || Array.isArray(value)) {
            return fromJsonForJava(value, baseType, Java.vm.getEnv());
        }
        return buildTypedMap(value, baseType, argTypes);
    }

    // Primitive or simple type.
    return fromJsonForJava(value, baseType, Java.vm.getEnv());
}

