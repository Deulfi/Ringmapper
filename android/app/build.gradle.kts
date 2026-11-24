plugins {
    id("com.android.application")
}

android {
    namespace = "io.github.deulfi.ringmapper"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.github.deulfi.ringmapper"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
        ndk { abiFilters += "arm64-v8a" }   // the daemon is only built for arm64
    }

    buildFeatures {
        aidl = true
        buildConfig = true
    }

    // The daemon ships as lib/arm64-v8a/libringmapper.so. Legacy packaging makes
    // Android extract it to nativeLibraryDir, where the privileged side copies it from.
    packaging { jniLibs { useLegacyPackaging = true } }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
}

// Copy the NDK-built daemon (module/ring_mapper, built by daemon/build.sh) into jniLibs.
val copyDaemon by tasks.registering(Copy::class) {
    from(rootProject.file("../module/ring_mapper"))
    into(layout.projectDirectory.dir("src/main/jniLibs/arm64-v8a"))
    rename { "libringmapper.so" }
}
tasks.named("preBuild") { dependsOn(copyDaemon) }
