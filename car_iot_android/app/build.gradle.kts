plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.kotlin.plugin.serialization")
    id("com.google.devtools.ksp")
}

android {
    namespace = "info.karuru.cariot"
    compileSdk = 36

    defaultConfig {
        applicationId = "info.karuru.cariot"
        // CompanionDeviceManager.startObservingDevicePresence(ObservingDevicePresenceRequest)
        // + CompanionDeviceService.onDevicePresenceEvent()の新APIがAPI36必須
        // （startObservingDevicePresence(String)/onDeviceAppeared()はAndroid16で非推奨化されたため
        // 新APIのみ実装する方針、docs/car_iot_android_plan.md Phase7参照）
        minSdk = 36
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    sourceSets["main"].kotlin.srcDirs("src/main/kotlin")
    sourceSets["test"].kotlin.srcDirs("src/test/kotlin")
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2025.09.00")
    implementation(composeBom)
    androidTestImplementation(composeBom)

    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    // タブのアイコン表示（Phase8、NavigationBarItem用）。Bluetooth/BatteryFull等は
    // material-icons-coreの基本セットに含まれないためextendedを使う
    implementation("androidx.compose.material:material-icons-extended")
    // BleConnectionManagerの状態(StateFlow)をComposeで購読するために使う
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    // Cognito Hosted UI(Google連携)のOAuth/OIDCクライアント
    implementation("net.openid:appauth:0.11.1")
    // org.json.JSONObjectはAndroid Unit Test環境でstub化され動作しないため、
    // IDトークン(JWT)ペイロードのJSONパースにはこちらを使う
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.11.0")

    // OBDアップロードキュー・Service稼働時間ログの永続化（Phase5、docs/car_iot_android_plan.md）
    implementation("androidx.room:room-runtime:2.8.4")
    implementation("androidx.room:room-ktx:2.8.4")
    ksp("androidx.room:room-compiler:2.8.4")

    // アップロードのHTTP POST（Phase5）
    // 5.5.0はcompileSdk37を要求しAGP8.11.1推奨の36と衝突するため5.4.0を使う
    implementation("com.squareup.okhttp3:okhttp:5.4.0")

    // 位置情報取得（Phase6）。標準のLocationManagerより省電力・高精度なFusedLocationProviderClientを使う
    implementation("com.google.android.gms:play-services-location:21.4.0")

    testImplementation("junit:junit:4.13.2")
}
