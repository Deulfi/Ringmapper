import java.util.Properties

plugins {
    id("com.android.application")
}

// Release signing comes from android/keystore.properties (git-ignored):
//   storeFile=/path/to/ringmapper-release.jks
//   storePassword=...   keyAlias=ringmapper   keyPassword=...
// Without that file, release builds fall back to the debug key (fine for forks/testing,
// but users can't update between APKs signed with different keys).
val ks = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "io.github.deulfi.ringmapper"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.github.deulfi.ringmapper"
        minSdk = 26
        targetSdk = 36
        versionCode = 2
        versionName = "1.1.1"
        ndk { abiFilters += "arm64-v8a" }   // the daemon is only built for arm64
    }

    buildFeatures {
        aidl = true
        buildConfig = true
    }

    // The daemon ships as lib/arm64-v8a/libringmapper.so. Legacy packaging makes
    // Android extract it to nativeLibraryDir, where the privileged side copies it from.
    packaging { jniLibs { useLegacyPackaging = true } }

    signingConfigs {
        if (!ks.isEmpty) create("release") {
            storeFile = file(ks.getProperty("storeFile"))
            storePassword = ks.getProperty("storePassword")
            keyAlias = ks.getProperty("keyAlias")
            keyPassword = ks.getProperty("keyPassword")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
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
