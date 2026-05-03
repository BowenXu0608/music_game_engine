plugins {
    id("com.android.application")
}

android {
    namespace = "com.musicgame.player"
    compileSdk = 36
    buildToolsVersion = "37.0.0"  // use already-installed version
    ndkVersion = "27.2.12479018"  // NDK r27c

    defaultConfig {
        applicationId = "com.musicgame.player"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
        debug {
            isDebuggable = true
            // Compile native code with -O2 even for the debug APK so on-device
            // gameplay doesn't crawl. Java side stays debuggable; only the C++
            // optimization level changes.
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Release"
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../engine/src/android/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
