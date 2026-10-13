// The Java shim of aud_dsp_sampler: a content provider that hands the
// AssetManager of the app to the native library of the sampler when the app
// process starts (ticket 27). Flutter builds this library into every Android
// app that depends on the package.

group = "com.audanika.aud_dsp_sampler"
version = "1.0"

buildscript {
    repositories {
        google()
        mavenCentral()
    }

    dependencies {
        classpath("com.android.tools.build:gradle:9.1.0")
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

plugins {
    id("com.android.library")
}

android {
    namespace = "com.audanika.aud_dsp_sampler"

    compileSdk = 36

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        minSdk = 24
        // R8 must keep the shim: the native library reaches it through JNI
        // by name.
        consumerProguardFiles("consumer-rules.pro")
    }
}
