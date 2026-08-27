package info.karuru.cariot.location

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.os.Looper
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationCallback
import com.google.android.gms.location.LocationRequest
import com.google.android.gms.location.LocationResult
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority

private const val UPDATE_INTERVAL_MS = 1000L

// mobile/lib/services/location_service.dartのKotlin移植。GPS位置情報を継続取得し、
// 直近の既知の位置をキャッシュしておくだけの薄いクラス。権限リクエストは呼び出し側
// (MainActivity.blePermissions())に集約するため、ここでは行わない（許可済み前提でstart()する）。
class LocationTracker(context: Context) {
  private val client: FusedLocationProviderClient = LocationServices.getFusedLocationProviderClient(context)
  private var callback: LocationCallback? = null

  @Volatile
  var lastLocation: Location? = null
    private set

  @SuppressLint("MissingPermission") // 呼び出し側(CarIotForegroundService)で実行時権限を確認済み前提
  fun start() {
    if (callback != null) return
    val request = LocationRequest.Builder(Priority.PRIORITY_HIGH_ACCURACY, UPDATE_INTERVAL_MS).build()
    val cb = object : LocationCallback() {
      override fun onLocationResult(result: LocationResult) {
        result.lastLocation?.let { lastLocation = it }
      }
    }
    callback = cb
    client.requestLocationUpdates(request, cb, Looper.getMainLooper())
  }

  fun stop() {
    callback?.let { client.removeLocationUpdates(it) }
    callback = null
    lastLocation = null
  }
}
