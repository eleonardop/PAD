plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.eleonardop.pad"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.eleonardop.pad"
        minSdk = 26          // Android 8.0: AAudio disponível (Oboe usa OpenSL ES só como reserva)
        targetSdk = 35
        versionCode = 6
        versionName = "0.6.0-aveludado"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
                cppFlags += listOf("-std=c++17")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Assinado com a chave de debug para poder instalar direto do GitHub Actions.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        prefab = true
    }

    // Samples (FLAC, ~27 MB) e créditos ficam em <repo>/assets, compartilhados com os testes
    // do motor no computador. FLAC já é comprimido: guardar sem nova compressão no APK
    // deixa a leitura mais rápida.
    sourceSets["main"].assets.srcDirs("../../assets")
    androidResources {
        noCompress += "flac"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("com.google.oboe:oboe:1.9.3")
}
