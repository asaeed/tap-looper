plugins {
    kotlin("jvm")
    id("org.jetbrains.compose")
    id("org.jetbrains.kotlin.plugin.compose")
}


dependencies {
    implementation(project(":shared"))
    implementation(compose.desktop.currentOs)
}

// Build the C++ JNI lib via CMake on every assemble.
val buildNativeLib by tasks.registering(Exec::class) {
    val cppDir = file("src/jvmMain/cpp")
    val buildDir = file("$cppDir/build")
    workingDir = cppDir
    outputs.dir(buildDir)
    inputs.files(fileTree(cppDir) { exclude("build/**") })
    commandLine("sh", "-c",
        "cmake -S . -B build && cmake --build build")
}

// Copy the built .dylib into resources so System.loadLibrary picks it up at runtime.
val collectNativeLib by tasks.registering(Copy::class) {
    dependsOn(buildNativeLib)
    val cppBuild = file("src/jvmMain/cpp/build")
    from(cppBuild) {
        include("libtap_looper_bridge.dylib")
        include("libtap_looper_bridge.so")
        include("tap_looper_bridge.dll")
    }
    into(layout.buildDirectory.dir("native-libs"))
}

kotlin {
    jvmToolchain(17)
}

tasks.named("processResources") {
    dependsOn(collectNativeLib)
}

// Add native libs dir to java.library.path when running via gradle
tasks.withType<JavaExec>().configureEach {
    val libDir = file("build/native-libs").absolutePath
    systemProperty("java.library.path", libDir)
    environment("DYLD_LIBRARY_PATH", libDir)
}

compose.desktop {
    application {
        mainClass = "com.taplooper.desktop.MainKt"
        nativeDistributions {
            packageName = "TapLooper"
            packageVersion = "0.1.0"
        }
        // Distribute the native lib with the app
        jvmArgs("-Djava.library.path=" + file("build/native-libs").absolutePath)
    }
}
