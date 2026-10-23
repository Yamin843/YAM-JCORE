// bridge/type_converter.cpp
#include "bridge/type_converter.h"

#include <cctype>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace yamgg {
namespace bridge {

namespace {

// ── Exact-match lookup table ────────────────────────────────
// Anything that is not a primitive, an array, or a name that varies
// across vendors is looked up here first. Order matters only for
// readability.
const std::unordered_map<std::string, JavaKind>& exact_map() {
    static const std::unordered_map<std::string, JavaKind> m = {
        // Primitives
        {"void",    JavaKind::Void},
        {"boolean", JavaKind::Boolean},
        {"byte",    JavaKind::Byte},
        {"char",    JavaKind::Char},
        {"short",   JavaKind::Short},
        {"int",     JavaKind::Int},
        {"long",    JavaKind::Long},
        {"float",   JavaKind::Float},
        {"double",  JavaKind::Double},

        // Boxed
        {"java.lang.Boolean",   JavaKind::BoxedBoolean},
        {"java.lang.Byte",      JavaKind::BoxedByte},
        {"java.lang.Character", JavaKind::BoxedChar},
        {"java.lang.Short",     JavaKind::BoxedShort},
        {"java.lang.Integer",   JavaKind::BoxedInt},
        {"java.lang.Long",      JavaKind::BoxedLong},
        {"java.lang.Float",     JavaKind::BoxedFloat},
        {"java.lang.Double",    JavaKind::BoxedDouble},
        {"java.lang.Number",    JavaKind::BoxedDouble},

        // Strings
        {"java.lang.String",       JavaKind::String},
        {"java.lang.CharSequence", JavaKind::CharSequence},
        {"java.lang.StringBuilder",JavaKind::StringBuilder},
        {"java.lang.StringBuffer", JavaKind::StringBuffer},

        // Primitive arrays
        {"[B", JavaKind::PrimitiveByteArray},
        {"[I", JavaKind::PrimitiveIntArray},
        {"[J", JavaKind::PrimitiveLongArray},
        {"[C", JavaKind::PrimitiveCharArray},
        {"[S", JavaKind::PrimitiveShortArray},
        {"[Z", JavaKind::PrimitiveBooleanArray},
        {"[F", JavaKind::PrimitiveFloatArray},
        {"[D", JavaKind::PrimitiveDoubleArray},

        // Object arrays
        {"[Ljava.lang.String;", JavaKind::StringArray},
        {"[Ljava.lang.Object;", JavaKind::ObjectArray},

        // NIO
        {"java.nio.ByteBuffer",   JavaKind::ByteBuffer},
        {"java.nio.CharBuffer",   JavaKind::CharBuffer},
        {"java.nio.IntBuffer",    JavaKind::IntBuffer},
        {"java.nio.LongBuffer",   JavaKind::LongBuffer},
        {"java.nio.ShortBuffer",  JavaKind::ShortBuffer},
        {"java.nio.FloatBuffer",  JavaKind::FloatBuffer},
        {"java.nio.DoubleBuffer", JavaKind::DoubleBuffer},

        // Collections — interface names
        {"java.util.Collection",  JavaKind::Collection},
        {"java.util.List",        JavaKind::List},
        {"java.util.Set",         JavaKind::Set},
        {"java.util.SortedSet",   JavaKind::SortedSet},
        {"java.util.NavigableSet",JavaKind::NavigableSet},
        {"java.util.Queue",       JavaKind::Queue},
        {"java.util.Deque",       JavaKind::Deque},
        {"java.util.Map",         JavaKind::Map},
        {"java.util.SortedMap",   JavaKind::SortedMap},
        {"java.util.NavigableMap",JavaKind::NavigableMap},
        {"java.lang.Iterable",    JavaKind::Collection},

        // Collections — concrete
        {"java.util.ArrayList",              JavaKind::ArrayList},
        {"java.util.LinkedList",             JavaKind::LinkedList},
        {"java.util.Vector",                 JavaKind::Vector},
        {"java.util.Stack",                  JavaKind::Stack},
        {"java.util.ArrayDeque",             JavaKind::ArrayDeque},
        {"java.util.PriorityQueue",          JavaKind::PriorityQueue},
        {"java.util.HashSet",                JavaKind::HashSet},
        {"java.util.LinkedHashSet",          JavaKind::LinkedHashSet},
        {"java.util.TreeSet",                JavaKind::TreeSet},
        {"java.util.EnumSet",                JavaKind::EnumSet},
        {"java.util.HashMap",                JavaKind::HashMap},
        {"java.util.LinkedHashMap",          JavaKind::LinkedHashMap},
        {"java.util.TreeMap",                JavaKind::TreeMap},
        {"java.util.EnumMap",                JavaKind::EnumMap},
        {"java.util.concurrent.ConcurrentHashMap", JavaKind::ConcurrentHashMap},
        {"java.util.concurrent.ConcurrentMap",     JavaKind::ConcurrentHashMap},
        {"java.util.concurrent.CopyOnWriteArrayList", JavaKind::ArrayList},

        // Android collections
        {"android.util.ArrayMap",           JavaKind::ArrayMap},
        {"android.util.SparseArray",        JavaKind::SparseArray},
        {"android.util.SparseIntArray",     JavaKind::SparseIntArray},
        {"android.util.SparseLongArray",    JavaKind::SparseLongArray},
        {"android.util.SparseBooleanArray", JavaKind::SparseBooleanArray},
        {"androidx.collection.SparseArrayCompat", JavaKind::SparseArrayCompat},
        {"android.util.LongSparseArray",    JavaKind::LongSparseArray},

        // Bundles
        {"android.os.Bundle",              JavaKind::Bundle},
        {"android.os.BaseBundle",          JavaKind::BaseBundle},
        {"android.os.PersistableBundle",   JavaKind::PersistableBundle},

        // org.json
        {"org.json.JSONObject",    JavaKind::JsonObject},
        {"org.json.JSONArray",     JavaKind::JsonArray},
        {"org.json.JSONStringer",  JavaKind::JsonStringer},
        {"org.json.JSONTokener",   JavaKind::JsonTokener},
        {"org.json.JSONObject$Null", JavaKind::JsonObjectNull},

        // Android framework
        {"android.content.Intent",   JavaKind::Intent},
        {"android.net.Uri",          JavaKind::Uri},
        {"android.graphics.Bitmap",  JavaKind::Bitmap},
        {"android.graphics.drawable.Drawable", JavaKind::Drawable},
        {"android.view.View",        JavaKind::View},
        {"android.view.ViewGroup",   JavaKind::ViewGroup},
        {"android.widget.TextView",  JavaKind::TextView},
        {"android.widget.Button",    JavaKind::Button},
        {"android.widget.EditText",  JavaKind::EditText},
        {"android.widget.ImageView", JavaKind::ImageView},
        {"android.app.Activity",     JavaKind::Activity},
        {"android.content.Context",  JavaKind::Context},

        // Reflection
        {"java.lang.Class",       JavaKind::ClassObject},
        {"java.lang.Throwable",   JavaKind::Throwable},
        {"java.lang.Exception",   JavaKind::Exception},
        {"java.lang.Error",       JavaKind::Error},

        // Utility
        {"java.util.Date",         JavaKind::Date},
        {"java.util.Calendar",     JavaKind::Calendar},
        {"java.util.Optional",     JavaKind::Optional},
        {"java.util.UUID",         JavaKind::UUID},
        {"java.net.URL",           JavaKind::URL},
        {"java.net.URI",           JavaKind::URI},
        {"java.io.File",           JavaKind::File},
        {"java.util.Locale",       JavaKind::Locale},
        {"java.util.TimeZone",     JavaKind::TimeZone},
        {"java.nio.charset.Charset",JavaKind::Charset},

        // Parcelable/Serializable interfaces
        {"android.os.Parcelable",  JavaKind::Parcelable},
        {"java.io.Serializable",   JavaKind::Serializable},

        // Object catch-all
        {"java.lang.Object",       JavaKind::Object},
    };
    return m;
}

// ── Heuristic prefix lookup ────────────────────────────────
// Some types only appear as superclass or interface names in the UI;
// the concrete type is unknown. Anything implementing Parcelable or
// Serializable is treated as that interface for UI purposes.
JavaKind classify_by_prefix(const std::string& t) {
    // Nested primitive arrays: [[I, [[[B, etc.
    if (t.size() >= 3 && t[0] == '[' && t[1] == '[') {
        char inner = t[2];
        if (inner == 'L') return JavaKind::NestedObjectArray;
        // Check for primitives directly after the inner [
        switch (inner) {
            case 'B': case 'I': case 'J': case 'C':
            case 'S': case 'Z': case 'F': case 'D':
                return JavaKind::NestedPrimitiveArray;
        }
        return JavaKind::NestedObjectArray;
    }
    return JavaKind::Unknown;
}

bool is_primitive_kind(JavaKind k) {
    switch (k) {
        case JavaKind::Void:
        case JavaKind::Boolean:
        case JavaKind::Byte:
        case JavaKind::Char:
        case JavaKind::Short:
        case JavaKind::Int:
        case JavaKind::Long:
        case JavaKind::Float:
        case JavaKind::Double:
            return true;
        default:
            return false;
    }
}

bool is_array_kind(JavaKind k) {
    switch (k) {
        case JavaKind::PrimitiveByteArray:
        case JavaKind::PrimitiveIntArray:
        case JavaKind::PrimitiveLongArray:
        case JavaKind::PrimitiveCharArray:
        case JavaKind::PrimitiveShortArray:
        case JavaKind::PrimitiveBooleanArray:
        case JavaKind::PrimitiveFloatArray:
        case JavaKind::PrimitiveDoubleArray:
        case JavaKind::StringArray:
        case JavaKind::ObjectArray:
        case JavaKind::NestedPrimitiveArray:
        case JavaKind::NestedObjectArray:
            return true;
        default:
            return false;
    }
}

bool is_collection_kind(JavaKind k) {
    switch (k) {
        case JavaKind::Collection:
        case JavaKind::List:
        case JavaKind::ArrayList:
        case JavaKind::LinkedList:
        case JavaKind::Vector:
        case JavaKind::Stack:
        case JavaKind::Queue:
        case JavaKind::Deque:
        case JavaKind::ArrayDeque:
        case JavaKind::PriorityQueue:
        case JavaKind::Set:
        case JavaKind::HashSet:
        case JavaKind::LinkedHashSet:
        case JavaKind::TreeSet:
        case JavaKind::EnumSet:
        case JavaKind::SortedSet:
        case JavaKind::NavigableSet:
            return true;
        default:
            return false;
    }
}

bool is_map_kind(JavaKind k) {
    switch (k) {
        case JavaKind::Map:
        case JavaKind::HashMap:
        case JavaKind::LinkedHashMap:
        case JavaKind::TreeMap:
        case JavaKind::ConcurrentHashMap:
        case JavaKind::EnumMap:
        case JavaKind::SortedMap:
        case JavaKind::NavigableMap:
        case JavaKind::ArrayMap:
        case JavaKind::Bundle:
        case JavaKind::BaseBundle:
        case JavaKind::PersistableBundle:
        case JavaKind::JsonObject:
            return true;
        default:
            return false;
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════
//  classify
// ═══════════════════════════════════════════════════════════════

JavaKind TypeConverter::classify(const std::string& t) {
    if (t.empty()) return JavaKind::Unknown;

    // Strip generic arguments: "java.util.List<java.lang.String>"
    // becomes "java.util.List" before classification.
    std::string base = t;
    auto lt = base.find('<');
    if (lt != std::string::npos) {
        base = base.substr(0, lt);
        // Trim trailing whitespace.
        while (!base.empty() && (base.back() == ' ' || base.back() == '\t')) {
            base.pop_back();
        }
    }

    // Exact match first.
    auto& m = exact_map();
    auto it = m.find(base);
    if (it != m.end()) return it->second;

    // Array notation without exact match: check prefix heuristics.
    JavaKind pref = classify_by_prefix(t);
    if (pref != JavaKind::Unknown) return pref;

    // Any other single-dimension object array: [Lcom.foo.Bar;
    if (t.size() > 2 && t[0] == '[' && t[1] == 'L') {
        return JavaKind::ObjectArray;
    }
    // Single-character array markers that were not matched exactly:
    if (t.size() == 2 && t[0] == '[') {
        switch (t[1]) {
            case 'B': return JavaKind::PrimitiveByteArray;
            case 'I': return JavaKind::PrimitiveIntArray;
            case 'J': return JavaKind::PrimitiveLongArray;
            case 'C': return JavaKind::PrimitiveCharArray;
            case 'S': return JavaKind::PrimitiveShortArray;
            case 'Z': return JavaKind::PrimitiveBooleanArray;
            case 'F': return JavaKind::PrimitiveFloatArray;
            case 'D': return JavaKind::PrimitiveDoubleArray;
            default:  break;
        }
    }

    return JavaKind::Unknown;
}

JavaKind TypeConverter::classify_with_nullability(const std::string& typeName,
                                                  bool& nullable) {
    JavaKind k = classify(typeName);
    nullable = !is_primitive_kind(k) && (k != JavaKind::Void);
    return k;
}

// ═══════════════════════════════════════════════════════════════
//  label
// ═══════════════════════════════════════════════════════════════

const char* TypeConverter::label(JavaKind k) {
    switch (k) {
        case JavaKind::Void:      return "void";
        case JavaKind::Boolean:   return "boolean";
        case JavaKind::Byte:      return "byte";
        case JavaKind::Char:      return "char";
        case JavaKind::Short:     return "short";
        case JavaKind::Int:       return "int";
        case JavaKind::Long:      return "long";
        case JavaKind::Float:     return "float";
        case JavaKind::Double:    return "double";

        case JavaKind::BoxedBoolean: return "Boolean";
        case JavaKind::BoxedByte:    return "Byte";
        case JavaKind::BoxedChar:    return "Character";
        case JavaKind::BoxedShort:   return "Short";
        case JavaKind::BoxedInt:     return "Integer";
        case JavaKind::BoxedLong:    return "Long";
        case JavaKind::BoxedFloat:   return "Float";
        case JavaKind::BoxedDouble:  return "Double";

        case JavaKind::String:       return "String";
        case JavaKind::CharSequence: return "CharSequence";
        case JavaKind::StringBuilder:return "StringBuilder";
        case JavaKind::StringBuffer: return "StringBuffer";

        case JavaKind::PrimitiveByteArray:    return "byte[]";
        case JavaKind::PrimitiveIntArray:     return "int[]";
        case JavaKind::PrimitiveLongArray:    return "long[]";
        case JavaKind::PrimitiveCharArray:    return "char[]";
        case JavaKind::PrimitiveShortArray:   return "short[]";
        case JavaKind::PrimitiveBooleanArray: return "boolean[]";
        case JavaKind::PrimitiveFloatArray:   return "float[]";
        case JavaKind::PrimitiveDoubleArray:  return "double[]";

        case JavaKind::StringArray:  return "String[]";
        case JavaKind::ObjectArray:  return "Object[]";
        case JavaKind::NestedPrimitiveArray: return "primitive[][]";
        case JavaKind::NestedObjectArray:    return "Object[][]";

        case JavaKind::ByteBuffer:   return "ByteBuffer";
        case JavaKind::CharBuffer:   return "CharBuffer";
        case JavaKind::IntBuffer:    return "IntBuffer";
        case JavaKind::LongBuffer:   return "LongBuffer";
        case JavaKind::ShortBuffer:  return "ShortBuffer";
        case JavaKind::FloatBuffer:  return "FloatBuffer";
        case JavaKind::DoubleBuffer: return "DoubleBuffer";

        case JavaKind::Collection:  return "Collection";
        case JavaKind::List:        return "List";
        case JavaKind::ArrayList:   return "ArrayList";
        case JavaKind::LinkedList:  return "LinkedList";
        case JavaKind::Vector:      return "Vector";
        case JavaKind::Stack:       return "Stack";
        case JavaKind::Queue:       return "Queue";
        case JavaKind::Deque:       return "Deque";
        case JavaKind::ArrayDeque:  return "ArrayDeque";
        case JavaKind::PriorityQueue: return "PriorityQueue";
        case JavaKind::Set:         return "Set";
        case JavaKind::HashSet:     return "HashSet";
        case JavaKind::LinkedHashSet: return "LinkedHashSet";
        case JavaKind::TreeSet:     return "TreeSet";
        case JavaKind::EnumSet:     return "EnumSet";
        case JavaKind::SortedSet:   return "SortedSet";
        case JavaKind::NavigableSet:return "NavigableSet";

        case JavaKind::Map:                return "Map";
        case JavaKind::HashMap:            return "HashMap";
        case JavaKind::LinkedHashMap:      return "LinkedHashMap";
        case JavaKind::TreeMap:            return "TreeMap";
        case JavaKind::ConcurrentHashMap:  return "ConcurrentHashMap";
        case JavaKind::EnumMap:            return "EnumMap";
        case JavaKind::SortedMap:          return "SortedMap";
        case JavaKind::NavigableMap:       return "NavigableMap";

        case JavaKind::ArrayMap:            return "ArrayMap";
        case JavaKind::SparseArray:         return "SparseArray";
        case JavaKind::SparseIntArray:      return "SparseIntArray";
        case JavaKind::SparseLongArray:     return "SparseLongArray";
        case JavaKind::SparseBooleanArray:  return "SparseBooleanArray";
        case JavaKind::SparseArrayCompat:   return "SparseArrayCompat";
        case JavaKind::LongSparseArray:     return "LongSparseArray";

        case JavaKind::Bundle:              return "Bundle";
        case JavaKind::BaseBundle:          return "BaseBundle";
        case JavaKind::PersistableBundle:   return "PersistableBundle";

        case JavaKind::JsonObject:     return "JSONObject";
        case JavaKind::JsonArray:      return "JSONArray";
        case JavaKind::JsonStringer:   return "JSONStringer";
        case JavaKind::JsonTokener:    return "JSONTokener";
        case JavaKind::JsonObjectNull: return "JSONObject.NULL";

        case JavaKind::Intent:      return "Intent";
        case JavaKind::Uri:         return "Uri";
        case JavaKind::Bitmap:      return "Bitmap";
        case JavaKind::Drawable:    return "Drawable";
        case JavaKind::View:        return "View";
        case JavaKind::ViewGroup:   return "ViewGroup";
        case JavaKind::TextView:    return "TextView";
        case JavaKind::Button:      return "Button";
        case JavaKind::EditText:    return "EditText";
        case JavaKind::ImageView:   return "ImageView";
        case JavaKind::Activity:    return "Activity";
        case JavaKind::Context:     return "Context";

        case JavaKind::ClassObject: return "Class";
        case JavaKind::Throwable:   return "Throwable";
        case JavaKind::Exception:   return "Exception";
        case JavaKind::Error:       return "Error";

        case JavaKind::Date:        return "Date";
        case JavaKind::Calendar:    return "Calendar";
        case JavaKind::Optional:    return "Optional";
        case JavaKind::UUID:        return "UUID";
        case JavaKind::URL:         return "URL";
        case JavaKind::URI:         return "URI";
        case JavaKind::File:        return "File";
        case JavaKind::Locale:      return "Locale";
        case JavaKind::TimeZone:    return "TimeZone";
        case JavaKind::Charset:     return "Charset";

        case JavaKind::Parcelable:  return "Parcelable";
        case JavaKind::Serializable:return "Serializable";

        case JavaKind::Object:      return "Object";
        case JavaKind::Unknown:     return "?";
    }
    return "?";
}

// ═══════════════════════════════════════════════════════════════
//  editor_kind
// ═══════════════════════════════════════════════════════════════

const char* TypeConverter::editor_kind(JavaKind k) {
    switch (k) {
        case JavaKind::Void:
            return "void";

        case JavaKind::Boolean:
        case JavaKind::Byte:
        case JavaKind::Char:
        case JavaKind::Short:
        case JavaKind::Int:
        case JavaKind::Long:
        case JavaKind::Float:
        case JavaKind::Double:
        case JavaKind::BoxedBoolean:
        case JavaKind::BoxedByte:
        case JavaKind::BoxedChar:
        case JavaKind::BoxedShort:
        case JavaKind::BoxedInt:
        case JavaKind::BoxedLong:
        case JavaKind::BoxedFloat:
        case JavaKind::BoxedDouble:
            return "primitive";

        case JavaKind::String:
        case JavaKind::CharSequence:
        case JavaKind::StringBuilder:
        case JavaKind::StringBuffer:
        case JavaKind::UUID:
        case JavaKind::URL:
        case JavaKind::URI:
        case JavaKind::File:
        case JavaKind::Locale:
        case JavaKind::TimeZone:
        case JavaKind::Charset:
            return "string";

        case JavaKind::PrimitiveByteArray:
        case JavaKind::ByteBuffer:
            return "bytes";

        case JavaKind::PrimitiveIntArray:
        case JavaKind::PrimitiveLongArray:
        case JavaKind::PrimitiveCharArray:
        case JavaKind::PrimitiveShortArray:
        case JavaKind::PrimitiveBooleanArray:
        case JavaKind::PrimitiveFloatArray:
        case JavaKind::PrimitiveDoubleArray:
        case JavaKind::CharBuffer:
        case JavaKind::IntBuffer:
        case JavaKind::LongBuffer:
        case JavaKind::ShortBuffer:
        case JavaKind::FloatBuffer:
        case JavaKind::DoubleBuffer:
        case JavaKind::StringArray:
        case JavaKind::ObjectArray:
        case JavaKind::NestedPrimitiveArray:
        case JavaKind::NestedObjectArray:
        case JavaKind::JsonArray:
            return "array";

        case JavaKind::Collection:
        case JavaKind::List:
        case JavaKind::ArrayList:
        case JavaKind::LinkedList:
        case JavaKind::Vector:
        case JavaKind::Stack:
        case JavaKind::Queue:
        case JavaKind::Deque:
        case JavaKind::ArrayDeque:
        case JavaKind::PriorityQueue:
        case JavaKind::Set:
        case JavaKind::HashSet:
        case JavaKind::LinkedHashSet:
        case JavaKind::TreeSet:
        case JavaKind::EnumSet:
        case JavaKind::SortedSet:
        case JavaKind::NavigableSet:
            return "array";

        case JavaKind::Map:
        case JavaKind::HashMap:
        case JavaKind::LinkedHashMap:
        case JavaKind::TreeMap:
        case JavaKind::ConcurrentHashMap:
        case JavaKind::EnumMap:
        case JavaKind::SortedMap:
        case JavaKind::NavigableMap:
        case JavaKind::ArrayMap:
        case JavaKind::SparseArray:
        case JavaKind::SparseIntArray:
        case JavaKind::SparseLongArray:
        case JavaKind::SparseBooleanArray:
        case JavaKind::SparseArrayCompat:
        case JavaKind::LongSparseArray:
        case JavaKind::Bundle:
        case JavaKind::BaseBundle:
        case JavaKind::PersistableBundle:
        case JavaKind::JsonObject:
            return "map";

        default:
            return "object";
    }
}

// ═══════════════════════════════════════════════════════════════
//  validate
// ═══════════════════════════════════════════════════════════════

bool TypeConverter::validate(const nlohmann::json& value,
                             JavaKind kind,
                             std::string& error) {
    if (value.is_null()) return true;

    switch (kind) {
        case JavaKind::Void:
            error = "cannot pass a value for a void parameter";
            return false;

        case JavaKind::Boolean:
        case JavaKind::BoxedBoolean:
            if (value.is_boolean()) return true;
            if (value.is_string()) {
                const std::string& s = value.get_ref<const std::string&>();
                if (s == "true" || s == "false" || s == "1" || s == "0")
                    return true;
            }
            if (value.is_number_integer()) return true;
            error = "expected boolean";
            return false;

        case JavaKind::Byte:
        case JavaKind::Short:
        case JavaKind::Int:
        case JavaKind::Long:
        case JavaKind::BoxedByte:
        case JavaKind::BoxedShort:
        case JavaKind::BoxedInt:
        case JavaKind::BoxedLong:
            if (value.is_number_integer() || value.is_number_unsigned()) return true;
            if (value.is_string()) return true;
            error = "expected integer";
            return false;

        case JavaKind::Float:
        case JavaKind::Double:
        case JavaKind::BoxedFloat:
        case JavaKind::BoxedDouble:
            if (value.is_number() || value.is_string()) return true;
            error = "expected number";
            return false;

        case JavaKind::Char:
        case JavaKind::BoxedChar:
            if (value.is_string() && value.get_ref<const std::string&>().size() <= 2)
                return true;
            if (value.is_number_integer()) return true;
            error = "expected single character";
            return false;

        case JavaKind::String:
        case JavaKind::CharSequence:
        case JavaKind::StringBuilder:
        case JavaKind::StringBuffer:
        case JavaKind::UUID:
        case JavaKind::URL:
        case JavaKind::URI:
        case JavaKind::File:
        case JavaKind::Locale:
        case JavaKind::TimeZone:
        case JavaKind::Charset:
        case JavaKind::JsonStringer:
        case JavaKind::JsonTokener:
            if (value.is_string()) return true;
            if (value.is_number() || value.is_boolean()) return true;  // coerced
            error = "expected string";
            return false;

        case JavaKind::PrimitiveByteArray:
        case JavaKind::ByteBuffer:
            if (value.is_string()) return true;
            if (value.is_array())  return true;
            if (value.is_object() && value.contains("hex")) return true;
            error = "expected byte array (hex string, JSON array, or {hex: ...})";
            return false;

        case JavaKind::PrimitiveIntArray:
        case JavaKind::PrimitiveLongArray:
        case JavaKind::PrimitiveShortArray:
        case JavaKind::PrimitiveFloatArray:
        case JavaKind::PrimitiveDoubleArray:
        case JavaKind::CharBuffer:
        case JavaKind::IntBuffer:
        case JavaKind::LongBuffer:
        case JavaKind::ShortBuffer:
        case JavaKind::FloatBuffer:
        case JavaKind::DoubleBuffer:
            if (value.is_array())  return true;
            if (value.is_string()) return true;  // hex or JSON
            error = "expected numeric array";
            return false;

        case JavaKind::PrimitiveCharArray:
        case JavaKind::PrimitiveBooleanArray:
        case JavaKind::StringArray:
        case JavaKind::ObjectArray:
        case JavaKind::NestedPrimitiveArray:
        case JavaKind::NestedObjectArray:
        case JavaKind::JsonArray:
            if (value.is_array())  return true;
            if (value.is_string()) return true;  // JSON
            error = "expected array";
            return false;

        case JavaKind::Collection:
        case JavaKind::List:
        case JavaKind::ArrayList:
        case JavaKind::LinkedList:
        case JavaKind::Vector:
        case JavaKind::Stack:
        case JavaKind::Queue:
        case JavaKind::Deque:
        case JavaKind::ArrayDeque:
        case JavaKind::PriorityQueue:
        case JavaKind::Set:
        case JavaKind::HashSet:
        case JavaKind::LinkedHashSet:
        case JavaKind::TreeSet:
        case JavaKind::EnumSet:
        case JavaKind::SortedSet:
        case JavaKind::NavigableSet:
            if (value.is_array())  return true;
            if (value.is_string()) return true;  // JSON
            error = "expected array";
            return false;

        case JavaKind::Map:
        case JavaKind::HashMap:
        case JavaKind::LinkedHashMap:
        case JavaKind::TreeMap:
        case JavaKind::ConcurrentHashMap:
        case JavaKind::EnumMap:
        case JavaKind::SortedMap:
        case JavaKind::NavigableMap:
        case JavaKind::ArrayMap:
        case JavaKind::SparseArray:
        case JavaKind::SparseIntArray:
        case JavaKind::SparseLongArray:
        case JavaKind::SparseBooleanArray:
        case JavaKind::SparseArrayCompat:
        case JavaKind::LongSparseArray:
        case JavaKind::Bundle:
        case JavaKind::BaseBundle:
        case JavaKind::PersistableBundle:
        case JavaKind::JsonObject:
            if (value.is_object()) return true;
            if (value.is_string()) return true;  // JSON
            error = "expected object";
            return false;

        case JavaKind::Date:
        case JavaKind::Calendar:
            if (value.is_number_integer() || value.is_number_unsigned()) return true;
            if (value.is_string()) return true;
            error = "expected timestamp (ms since epoch) or ISO date string";
            return false;

        case JavaKind::Optional:
            // any value is acceptable; null means empty
            return true;

        case JavaKind::JsonObjectNull:
            if (value.is_null()) return true;
            error = "JSONObject.NULL must be represented as JSON null";
            return false;

        default:
            // Object and everything else: any JSON is acceptable, JS side
            // decides whether it can build a wrapper.
            return true;
    }
}

// ═══════════════════════════════════════════════════════════════
//  display
// ═══════════════════════════════════════════════════════════════


std::string TypeConverter::display(const nlohmann::json& v) {
    // No truncation. The UI layer decides how to render the returned
    // string (e.g. as a virtualised multi-line text field). Returning
    // the complete value guarantees that no data is lost.
    if (v.is_null())    return "null";
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_string())  return v.get<std::string>();
    if (v.is_number_integer())  return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) {
        std::ostringstream os;
        os.precision(17);   // full double precision, round-trippable
        os << v.get<double>();
        return os.str();
    }

    if (v.is_object()) {
        if (v.contains("__type") && v["__type"].is_string()) {
            const std::string t = v["__type"].get<std::string>();
            if (t == "JavaObject") {
                const std::string cls = v.value("className", std::string());
                const std::string str = v.value("toString", std::string());
                if (!cls.empty()) {
                    if (str.empty()) return "<" + cls + ">";
                    return "<" + cls + ": " + str + ">";
                }
                return str.empty() ? "<object>" : str;
            }
            if (t == "ArrayBuffer") {
                const std::string hex = v.value("hex", std::string());
                const size_t n = v.value("length", (size_t)0);
                std::ostringstream os;
                os << "[bytes len=" << n << " hex=" << hex << "]";
                return os.str();
            }
            if (t == "NativePointer" || t == "Int64" || t == "UInt64") {
                return v.value("value", std::string());
            }
            if (t == "Bundle") {
                const size_t n = v.value("size", (size_t)0);
                std::ostringstream os;
                os << "Bundle(size=" << n << ")\n";
                // Show all entries — no truncation.
                if (v.contains("entries") && v["entries"].is_object()) {
                    for (auto it = v["entries"].begin(); it != v["entries"].end(); ++it) {
                        os << "  " << it.key() << " = "
                           << display(it.value()) << "\n";
                    }
                }
                return os.str();
            }
            if (t == "JSONObject") {
                const size_t n = v.value("size", (size_t)0);
                std::ostringstream os;
                os << "JSONObject(size=" << n << ")\n";
                if (v.contains("entries") && v["entries"].is_object()) {
                    for (auto it = v["entries"].begin(); it != v["entries"].end(); ++it) {
                        os << "  " << it.key() << " = "
                           << display(it.value()) << "\n";
                    }
                }
                return os.str();
            }
            if (t == "JSONArray") {
                const size_t n = v.value("length", (size_t)0);
                std::ostringstream os;
                os << "JSONArray(length=" << n << ")\n";
                if (v.contains("items") && v["items"].is_array()) {
                    const auto& items = v["items"];
                    for (size_t i = 0; i < items.size(); i++) {
                        os << "  [" << i << "] = "
                           << display(items[i]) << "\n";
                    }
                }
                return os.str();
            }
            if (t == "Collection") {
                const size_t n = v.value("length", (size_t)0);
                const std::string cls = v.value("className", std::string());
                std::ostringstream os;
                os << cls << "(length=" << n << ")\n";
                if (v.contains("items") && v["items"].is_array()) {
                    const auto& items = v["items"];
                    for (size_t i = 0; i < items.size(); i++) {
                        os << "  [" << i << "] = "
                           << display(items[i]) << "\n";
                    }
                }
                return os.str();
            }
            if (t == "Map") {
                const size_t n = v.value("size", (size_t)0);
                const std::string cls = v.value("className", std::string());
                std::ostringstream os;
                os << cls << "(size=" << n << ")\n";
                if (v.contains("entries") && v["entries"].is_object()) {
                    for (auto it = v["entries"].begin(); it != v["entries"].end(); ++it) {
                        os << "  " << it.key() << " = "
                           << display(it.value()) << "\n";
                    }
                }
                return os.str();
            }
        }
        try {
            return v.dump();   // full JSON, no truncation
        } catch (...) {
            return "<object>";
        }
    }

    if (v.is_array()) {
        std::ostringstream os;
        os << "[";
        const size_t n = v.size();
        for (size_t i = 0; i < n; i++) {
            if (i) os << ", ";
            os << display(v[i]);
        }
        os << "]";
        return os.str();
    }

    return "<unknown>";
}

// ═══════════════════════════════════════════════════════════════
//  encode_user_input
// ═══════════════════════════════════════════════════════════════

namespace {

bool parse_json_or_fail(const std::string& text,
                        nlohmann::json& out,
                        std::string& error,
                        const char* what) {
    out = nlohmann::json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = std::string("expected JSON ") + what;
        return false;
    }
    return true;
}

bool is_hex_char(char c) {
    return std::isxdigit((unsigned char)c) != 0;
}

std::string strip_hex_separators(const std::string& text) {
    std::string hex;
    hex.reserve(text.size());
    for (char c : text) {
        if (c == ' ' || c == ':' || c == '-' || c == '\t') continue;
        if (!is_hex_char(c)) return {};
        hex += c;
    }
    return hex;
}

} // namespace

nlohmann::json TypeConverter::encode_user_input(const std::string& typeName,
                                                const std::string& text,
                                                std::string& error) {
    JavaKind kind = classify(typeName);

    // For nullable reference types, treat "null" as null.
    bool nullable = false;
    classify_with_nullability(typeName, nullable);
    if (nullable && text == "null") return nlohmann::json();

    switch (kind) {
        case JavaKind::Void:
            error = "cannot encode a value for void";
            return nlohmann::json();

        case JavaKind::Boolean:
        case JavaKind::BoxedBoolean:
            if (text == "true"  || text == "1") return true;
            if (text == "false" || text == "0" || text.empty()) return false;
            error = "expected true/false/1/0";
            return nlohmann::json();

        case JavaKind::Byte:
        case JavaKind::Short:
        case JavaKind::Int:
        case JavaKind::BoxedByte:
        case JavaKind::BoxedShort:
        case JavaKind::BoxedInt:
            try {
                return (long long)std::stoll(text);
            } catch (...) {
                error = "expected integer";
                return nlohmann::json();
            }

        case JavaKind::Long:
        case JavaKind::BoxedLong:
            if (text.empty()) { error = "expected long"; return nlohmann::json(); }
            // Sent as string; JS converts with Int64(text) when needed.
            return text;

        case JavaKind::Float:
        case JavaKind::Double:
        case JavaKind::BoxedFloat:
        case JavaKind::BoxedDouble:
            try {
                return std::stod(text);
            } catch (...) {
                error = "expected number";
                return nlohmann::json();
            }

        case JavaKind::Char:
        case JavaKind::BoxedChar:
            if (text.empty()) { error = "expected character"; return nlohmann::json(); }
            return text.substr(0, 1);

        case JavaKind::String:
        case JavaKind::CharSequence:
        case JavaKind::StringBuilder:
        case JavaKind::StringBuffer:
        case JavaKind::UUID:
        case JavaKind::URL:
        case JavaKind::URI:
        case JavaKind::File:
        case JavaKind::Locale:
        case JavaKind::TimeZone:
        case JavaKind::Charset:
        case JavaKind::JsonStringer:
        case JavaKind::JsonTokener:
            return text;

        case JavaKind::PrimitiveByteArray:
        case JavaKind::ByteBuffer: {
            std::string hex = strip_hex_separators(text);
            if (hex.empty() && !text.empty()) {
                // Try JSON array of bytes.
                nlohmann::json arr;
                if (parse_json_or_fail(text, arr, error, "array") && arr.is_array()) {
                    return nlohmann::json{{"__type", "ArrayBuffer"}, {"array", arr}};
                }
                return nlohmann::json();
            }
            if (hex.size() % 2 != 0) {
                error = "hex string must have even length";
                return nlohmann::json();
            }
            return nlohmann::json{{"__type", "ArrayBuffer"}, {"hex", hex}};
        }

        case JavaKind::PrimitiveIntArray:
        case JavaKind::PrimitiveLongArray:
        case JavaKind::PrimitiveShortArray:
        case JavaKind::PrimitiveFloatArray:
        case JavaKind::PrimitiveDoubleArray:
        case JavaKind::PrimitiveBooleanArray:
        case JavaKind::PrimitiveCharArray:
        case JavaKind::StringArray:
        case JavaKind::ObjectArray:
        case JavaKind::NestedPrimitiveArray:
        case JavaKind::NestedObjectArray:
        case JavaKind::JsonArray:
        case JavaKind::Collection:
        case JavaKind::List:
        case JavaKind::ArrayList:
        case JavaKind::LinkedList:
        case JavaKind::Vector:
        case JavaKind::Stack:
        case JavaKind::Queue:
        case JavaKind::Deque:
        case JavaKind::ArrayDeque:
        case JavaKind::PriorityQueue:
        case JavaKind::Set:
        case JavaKind::HashSet:
        case JavaKind::LinkedHashSet:
        case JavaKind::TreeSet:
        case JavaKind::EnumSet:
        case JavaKind::SortedSet:
        case JavaKind::NavigableSet: {
            nlohmann::json arr;
            if (!parse_json_or_fail(text, arr, error, "array")) return nlohmann::json();
            if (!arr.is_array()) { error = "expected JSON array"; return nlohmann::json(); }
            return arr;
        }

        case JavaKind::Map:
        case JavaKind::HashMap:
        case JavaKind::LinkedHashMap:
        case JavaKind::TreeMap:
        case JavaKind::ConcurrentHashMap:
        case JavaKind::EnumMap:
        case JavaKind::SortedMap:
        case JavaKind::NavigableMap:
        case JavaKind::ArrayMap:
        case JavaKind::SparseArray:
        case JavaKind::SparseIntArray:
        case JavaKind::SparseLongArray:
        case JavaKind::SparseBooleanArray:
        case JavaKind::SparseArrayCompat:
        case JavaKind::LongSparseArray:
        case JavaKind::Bundle:
        case JavaKind::BaseBundle:
        case JavaKind::PersistableBundle:
        case JavaKind::JsonObject: {
            nlohmann::json obj;
            if (!parse_json_or_fail(text, obj, error, "object")) return nlohmann::json();
            if (!obj.is_object()) { error = "expected JSON object"; return nlohmann::json(); }
            return obj;
        }

        case JavaKind::Date:
        case JavaKind::Calendar:
            try {
                return (long long)std::stoll(text);
            } catch (...) {
                // Allow ISO date strings; JS side handles Date.parse.
                return text;
            }

        case JavaKind::Optional:
        case JavaKind::JsonObjectNull:
        case JavaKind::Object:
        case JavaKind::Unknown:
        case JavaKind::Throwable:
        case JavaKind::Exception:
        case JavaKind::Error:
        case JavaKind::ClassObject:
        case JavaKind::Intent:
        case JavaKind::Uri:
        case JavaKind::Bitmap:
        case JavaKind::Drawable:
        case JavaKind::View:
        case JavaKind::ViewGroup:
        case JavaKind::TextView:
        case JavaKind::Button:
        case JavaKind::EditText:
        case JavaKind::ImageView:
        case JavaKind::Activity:
        case JavaKind::Context:
        case JavaKind::Parcelable:
        case JavaKind::Serializable:
        case JavaKind::CharBuffer:
        case JavaKind::IntBuffer:
        case JavaKind::LongBuffer:
        case JavaKind::ShortBuffer:
        case JavaKind::FloatBuffer:
        case JavaKind::DoubleBuffer:
        default: {
            // Try JSON; on failure return the raw string.
            nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
            if (!parsed.is_discarded()) return parsed;
            return text;
        }
    }
}

} // namespace bridge
} // namespace yamgg
