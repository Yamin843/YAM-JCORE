// native/native_methods.h — JNINativeMethod table for ModView.java.
//
// Registers every static native method that ModView.java declares.
// Registration happens once, after the DEX has been loaded and the
// ModView class is resolvable through the DEX class loader.
#ifndef YAMGG_NATIVE_NATIVE_METHODS_H
#define YAMGG_NATIVE_NATIVE_METHODS_H

#include <jni.h>

namespace yamgg {
namespace native {

// Register natives on `mod_view_class`. Returns true if all entries
// registered successfully.
bool register_mod_view_natives(JNIEnv* env, jclass mod_view_class);

} // namespace native
} // namespace yamgg

#endif // YAMGG_NATIVE_NATIVE_METHODS_H
