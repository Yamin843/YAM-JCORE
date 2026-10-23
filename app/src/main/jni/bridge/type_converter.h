// bridge/type_converter.h — classify, validate, encode, and render
// values of every Java type the project recognises.
//
// The classification drives the UI: which editor widget to render,
// which error messages to show, and which JSON-to-Java conversion the
// JS side must perform. The conversion itself happens in JS (see
// Generated/bootstrap_js.h, function fromJsonForJava / toJsonSafe);
// the C++ side validates, displays, and encodes.
#ifndef YAMGG_BRIDGE_TYPE_CONVERTER_H
#define YAMGG_BRIDGE_TYPE_CONVERTER_H

#include <string>

#include <json.hpp>

namespace yamgg {
namespace bridge {

enum class JavaKind {
    // ── Primitives ─────────────────────────────────────────
    Void,
    Boolean, Byte, Char, Short, Int, Long, Float, Double,

    // ── Boxed primitives ───────────────────────────────────
    BoxedBoolean, BoxedByte, BoxedChar, BoxedShort,
    BoxedInt, BoxedLong, BoxedFloat, BoxedDouble,

    // ── Strings ────────────────────────────────────────────
    String, CharSequence, StringBuilder, StringBuffer,

    // ── Primitive arrays ───────────────────────────────────
    PrimitiveByteArray, PrimitiveIntArray, PrimitiveLongArray,
    PrimitiveCharArray, PrimitiveShortArray, PrimitiveBooleanArray,
    PrimitiveFloatArray, PrimitiveDoubleArray,

    // ── Object arrays ──────────────────────────────────────
    StringArray, ObjectArray, NestedPrimitiveArray, NestedObjectArray,

    // ── NIO buffers ────────────────────────────────────────
    ByteBuffer, CharBuffer, IntBuffer, LongBuffer,
    ShortBuffer, FloatBuffer, DoubleBuffer,

    // ── Collections ────────────────────────────────────────
    Collection, List, ArrayList, LinkedList, Vector, Stack,
    Queue, Deque, ArrayDeque, PriorityQueue,
    Set, HashSet, LinkedHashSet, TreeSet, EnumSet,
    Map, HashMap, LinkedHashMap, TreeMap, ConcurrentHashMap,
    EnumMap, SortedMap, NavigableMap, SortedSet, NavigableSet,

    // ── Android collections ────────────────────────────────
    ArrayMap, SparseArray, SparseIntArray, SparseLongArray,
    SparseBooleanArray, SparseArrayCompat, LongSparseArray,

    // ── Android Bundle / Persistable ───────────────────────
    Bundle, BaseBundle, PersistableBundle,

    // ── org.json ───────────────────────────────────────────
    JsonObject, JsonArray, JsonStringer, JsonTokener,
    JsonObjectNull,     // org.json.JSONObject.NULL

    // ── Android framework objects ──────────────────────────
    Intent, Uri, Bitmap, Drawable, View, ViewGroup,
    TextView, Button, EditText, ImageView, Activity, Context,

    // ── Reflection & exceptions ────────────────────────────
    ClassObject, Throwable, Exception, Error,

    // ── Utility ────────────────────────────────────────────
    Date, Calendar, Optional, UUID, URL, URI, File,
    Locale, TimeZone, Charset,

    // ── Parcelable / Serializable ──────────────────────────
    Parcelable, Serializable,

    // ── Object catch-all ───────────────────────────────────
    Object,
    Unknown
};

class TypeConverter {
public:
    // ── Classification ─────────────────────────────────────
    // Maps a class name (as Java's Class.getName() returns it) to a
    // JavaKind. Handles primitive names, arrays (`[B`, `[I`, `[[I`,
    // `[Ljava.lang.String;`), fully-qualified class names, and the
    // common interface names the UI displays.
    static JavaKind classify(const std::string& typeName);

    // Same as classify but additionally reports whether the value is a
    // nullable reference type (i.e. not a primitive). Useful for UI
    // controls that show a "null" affordance.
    static JavaKind classify_with_nullability(const std::string& typeName,
                                              bool& nullable);

    // ── Labels ─────────────────────────────────────────────
    // Short one-word label for display (e.g. "int", "byte[]", "Bundle").
    static const char* label(JavaKind kind);

    // Editor category — drives the UI widget choice:
    //   "primitive" — numeric/boolean text field
    //   "string"    — single-line text field
    //   "bytes"     — hex editor
    //   "array"     — list of element inputs
    //   "map"       — key/value table
    //   "object"    — handle input with "null" affordance
    //   "void"      — no input
    static const char* editor_kind(JavaKind kind);

    // ── Validation ─────────────────────────────────────────
    // Checks that a JSON value is compatible with the Java kind.
    // `value` is null, boolean, number, string, array, or object.
    static bool validate(const nlohmann::json& value,
                         JavaKind kind,
                         std::string& error);

    // ── Display ────────────────────────────────────────────
    // Renders a JSON value as a display string. Truncates arrays and
    // hex data to keep the UI responsive.
    static std::string display(const nlohmann::json& value);

    // ── Encoding (C++ → JSON) ──────────────────────────────
    // Encodes a value the user typed in the UI into a JSON shape the
    // JS bridge can convert to a Java value. For primitives it parses
    // the text; for arrays it parses JSON; for objects it passes the
    // text through if it looks like JSON, otherwise returns the raw
    // string (the JS side treats string as the value's toString()).
    //
    //   encode_user_input("int", "42")              → 42
    //   encode_user_input("long", "9223372036854")  → "9223372036854"
    //   encode_user_input("byte[]", "a1b2c3")       → {"__type":"ArrayBuffer","hex":"a1b2c3"}
    //   encode_user_input("java.util.List", "[1,2]")→ [1, 2]
    //   encode_user_input("android.os.Bundle", "{\"k\":1}") → {"k":1}
    static nlohmann::json encode_user_input(const std::string& typeName,
                                            const std::string& text,
                                            std::string& error);

private:
    TypeConverter() = delete;
};

} // namespace bridge
} // namespace yamgg

#endif // YAMGG_BRIDGE_TYPE_CONVERTER_H
