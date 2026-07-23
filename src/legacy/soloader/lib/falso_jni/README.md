<h1 align="center"><img alt="FalsoJNI" src="https://raw.githubusercontent.com/gist/v-atamanenko/6c869223e8e676bf00a054381d008143/raw/5664b2ef1838ad2675e8e0969c85494f138397c7/falsojni_logo.svg"></h1>
<p align="center">
  <a href="#setup">Setup</a> •
  <a href="#implementing-methods">Implementing Methods</a> •
  <a href="#implementing-fields">Implementing Fields</a> •
  <a href="#tips">Tips</a> •
  <a href="#todo">TODO</a> •
  <a href="#credits">Credits</a> •
  <a href="#license">License</a>
</p>

FalsoJNI (*falso* as in *fake* from Italian) is a simple, zero-dependency fake
JVM/JNI interface written in C.

It is created mainly to make JNI-heavy Android→PSVita ports easier, but probably
could be used for other purposes as well.

## Setup

Since there are no dependencies, FalsoJNI is not supplied with a Makefile of its
own, so to get started just include in your own Makefile/CMakeLists.txt
all the source files:
```
FalsoJNI/FalsoJni.c
FalsoJNI/FalsoJni_ImplBridge.c
FalsoJNI/FalsoJni_Logger.c
FalsoJNI/converter.c
```

Second thing you need to do, is to create your own `FalsoJNI_Impl` file. You
will use it later to provide implementations for custom JNI **Methods** (the
ones called with `jni->CallVoidMethodV` and similars) and **Fields**.

To do this, from `FalsoJNI_ImplSample.h` copy the definitions between
`COPY STARTING FROM HERE!` and `COPY UP TO HERE!` to your project in any `.c`
file (you could also split it up into several files if you need to).

After that, you already init FalsoJNI and supply `JNIEnv` and `JavaVM` objects
to your client application, like this:

```c
#include "FalsoJNI.h"

int main() {
    // ...
    
    jni_init(); // Initializes jvm and jni objects

    int (*JNI_OnLoad)(JavaVM* jvm) = (void*)so_symbol(&so_mod,"JNI_OnLoad");
    JNI_OnLoad(&jvm);

    // ...
}
```

That's it for the basic setup. In a theoretical situation where your client
application doesn't use any **Methods** or **Fields**, you're done here.
Otherwise, read on.

## Implementing Methods

### Step 1. Create functions

The easiest way to figure out which methods you need to implement is to
run the app as-is and look for FalsoJNI's errors in logs, particularly with
GetMethodID / GetStaticMethodID functions:

```
[ERROR][/tmp/soloader/FalsoJNI.c:295][GetMethodID] [JNI] GetMethodID(env, 0x83561570, "SetShiftEnabled", "(Z)V"): not found
[ERROR][/tmp/soloader/FalsoJNI.c:295][GetMethodID] [JNI] GetMethodID(env, 0x83561570, "Shutdown", "()V"): not found
```

Two important things you get from this log are the method name
(`"SetShiftEnabled"`) and the method signature (`"(Z)V"`).

You can learn what each symbol in Java type signature means [here](https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/types.html#wp16437).

To cut on the long details, here are a few self-explanatory examples of how
Java method signatures are translated into FalsoJNI-compatible implementations:

```c

// FalsoJNI always passes arguments as a va_list to be able to make single
// function implementation no matter how it is called (i.e. CallMethod,
// CallMethodV, or CallMethodA ).

// "SetShiftEnabled", "(Z)V"
void SetShiftEnabled(jmethodID id, va_list args) { // V (ret type) is a void
    jboolean arg = va_arg(args, jboolean); // Z is a boolean
    // do something
}

// "GetDisplayOrientationLock", "()I"
jint GetDisplayOrientationLock(jmethodID id, va_list args) { // I (ret type) is an integer
    // no arguments here
    return 0;
}

// "GetUsername", "(Ljava/lang/String;)Ljava/lang/String;"
jstring GetUsername(jmethodID id, va_list args) { // Ljava/lang/String; (ret type) is a jstring
    jstring _email = va_arg(args, jstring);
    
    // If you want to work with Java strings, always use respective JNI methods!
    // They are NOT c-strings.
    const char * email = jni->GetStringUTFChars(&jni, _email, NULL);
    const char * username = MyCoolFunctionToLookupUsername(_email);
    jni->ReleaseStringUTFChars(&jni, _email, email);
    
    return jni->NewStringUTF(&jni, username);
}

// "read", "([BII)I"
jint InputStream_read(jmethodID id, va_list args) { // I (ret type) is an integer
    jbyteArray _b = va_arg(args, char*); // [B is a byte array.
    jint off = va_arg(args, int); // I is an int
    jint len = va_arg(args, int); // I is an int

    // Before accessing/changing the array elements, we have to do the following:
    JavaDynArray * jda = (JavaDynArray *) _b;
    if (!jda) {
        log_error("[java.io.InputStream.read()] Provided buffer is not a valid JDA.");
        return 0;
    }

    char * b = jda->array; // Now this array we can work with
}
```

**Pay great attention to the last example.** Java arrays are notably different
from C arrays by always having the array size information with them, so
FalsoJNI mimics Java arrays behavior with a special struct, **JavaDynArray**
(or *jda* in short).

Every time you receive an array of any kind as an argument, you have to get 
the "real", underlying array from it like shown in the example. You can also
use `jda_sizeof(JavaDynArr *)` function to get the length of the array you
are operating on.

If you need to return an array in Java method implementation, — likewise.
Work with `jda->array`, return `jda`.

Also notice the second-to-last example to see how you can work with Java strings.

### Step 2. Put them in relevant arrays

Now that you have your implementations in place, the only thing left to do
to allow the client application to use them is to fill in the arrays in the
implementation file you copied from `FalsoJNI_ImplSample.h` earlier.

You just need to figure out the return types for your methods and come up with
any (unique!) method IDs you like. Example of filling the arrays for methods
from Step 1:

```c
NameToMethodID nameToMethodId[] = {
    { 100, "SetShiftEnabled", METHOD_TYPE_VOID },
    { 101, "GetDisplayOrientationLock", METHOD_TYPE_INT },
    { 102, "read", METHOD_TYPE_INT },
};

MethodsVoid methodsVoid[] = {
    { 100, SetShiftEnabled }
};

MethodsInt methodsInt[] = {
    { 101, GetDisplayOrientationLock },
    { 102, InputStream_read }
};
```

## Implementing Fields

With Fields, it's basically the same thing. Run your app, look for the errors
in `GetFieldID`, `GetStaticFieldID` to figure out the needed Fields names and
signatures (well, just *types* in this case).

When you know them, fill in the arrays in the same fashion:

```c
NameToFieldID nameToFieldId[] = {
    { 8, "screenWidth",            FIELD_TYPE_INT },
    { 9, "screenHeight",           FIELD_TYPE_INT },
    { 10, "is_licensed",           FIELD_TYPE_BOOLEAN }
};

FieldsBoolean fieldsBoolean[] = {
    { 10, JNI_TRUE }
};

FieldsInt fieldsInt[] = {
    { 8, 960 },
    { 9, 544 },
};
```

Everything else will be taken care of by FalsoJNI.

## Tips

1. There is a very verbose logging in this lib to debug difficult situations.
Either define `FALSOJNI_DEBUGLEVEL` or edit `FalsoJNI.h` if you need to change
the verbosity level:
```c
#define FALSOJNI_DEBUG_NO    4
#define FALSOJNI_DEBUG_ERROR 3
#define FALSOJNI_DEBUG_WARN  2
#define FALSOJNI_DEBUG_INFO  1
#define FALSOJNI_DEBUG_ALL   0

#ifndef FALSOJNI_DEBUGLEVEL
#define FALSOJNI_DEBUGLEVEL FALSOJNI_DEBUG_WARN
#endif
```

2. There are things in JNI that can not be implemented without some terrible
overengineering. If you come across one of them, the library will throw
a warning-level log at you.

3. I tried to keep the code as clean and self-explanatory as possible, but
didn't have time yet to write a proper documentation. As a direction for
further info, look at `FalsoJNI_ImplBridge.h` header for common type definitions
and JDA functions.

4. [Oracle JNI spec](https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/functions.html) is your friend.

## TODO

1. Exception handling. They are completely ignored now.
2. GetArrayLength for ObjectField values. (if needed?)
3. MonitorEnter/MonitorExit (per-javaobject semaphores).
4. DirectByteBuffers.
5. Keep track of references and destroy objects when there aren't any left.
6. Dry Run mode that would record methods/fields definitions to
`FalsoJNI_Impl.c` for you.

## Credits
* **TheFloW and Rinnegatamante** for fake JNI interfaces implementations
in [gtasa_vita](https://github.com/TheOfficialFloW/gtasa_vita/) that
served as inspiration and basis for this lib.

## License

This software may be modified and distributed under the terms of
the MIT license. See the [LICENSE](LICENSE) file for details.

Contains parts of Dalvik implementation of JNI interfaces,
copyright (C) 2008 The Android Open Source Project,\
licensed under the Apache License, Version 2.0.

Includes `converter.c` and `converter.h`,
copyright (C) 2015 Jonathan Bennett <jon@autoitscript.com>,
licensed under the Apache License, Version 2.0.
