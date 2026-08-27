plugins {
    id("com.android.application")
    id("kotlin-android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "info.karuru.cariot_mobile"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        applicationId = "info.karuru.cariot_mobile"
        // Cognito Hosted UI（Google OAuth）からのリダイレクトを受けるカスタムスキーム。
        // URIスキームはRFC3986でアンダースコア不可のためapplicationIdとは別文字列。
        // Cognito App Client の callback_urls・AuthService の redirectUri と完全一致させること。
        manifestPlaceholders["appAuthRedirectScheme"] = "info.karuru.cariotmobile"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        // CompanionDeviceManager.startObservingDevicePresence()がAPI31必須のため
        // flutter.minSdkVersion(=24)から明示的に引き上げている（Android 11以下は対象外）。
        minSdk = 31
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    buildTypes {
        release {
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

flutter {
    source = "../.."
}
