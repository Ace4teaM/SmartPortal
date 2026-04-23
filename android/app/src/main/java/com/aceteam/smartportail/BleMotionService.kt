package com.aceteam.smartportail

import android.app.Service
import android.content.Intent
import android.os.IBinder
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.bluetooth.BluetoothAdapter
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.ParcelUuid
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import java.util.UUID
import android.content.pm.PackageManager
import android.util.Log
import androidx.core.app.ActivityCompat
import android.Manifest
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Handler
import android.os.Looper
import com.aceteam.smartportail.BuildConfig

class BleMotionService : Service(), SensorEventListener, LocationListener {
    private val TARGET_LAT    = BuildConfig.TARGET_LAT
    private val TARGET_LNG    = BuildConfig.TARGET_LNG
    private val TARGET_RADIUS = BuildConfig.TARGET_RADIUS
    private val BLE_UUID      = BuildConfig.BLE_UUID

    private val ADVERTISING_DURATION_MS = BuildConfig.ADVERTISING_DURATION_MS

    companion object {
        const val ACTION_FORCE_ADVERTISE = "com.aceteam.smartportail.FORCE_ADVERTISE"
        const val ACTION_ADVERTISING_STATE = "com.aceteam.smartportail.ADVERTISING_STATE"
        const val EXTRA_IS_ADVERTISING = "is_advertising"
    }

    private fun sendAdvertisingState(isAdvertising: Boolean) {
        Log.d("BleMotionService", "sendAdvertisingState: $isAdvertising")
        val intent = Intent(ACTION_ADVERTISING_STATE).apply {
            putExtra(EXTRA_IS_ADVERTISING, isAdvertising)
            setPackage(packageName) // restreint le broadcast à ton app
        }
        sendBroadcast(intent)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_FORCE_ADVERTISE) {
            // Force advertising 10s peu importe position/mouvement
            isMoving = true
            startAdvertising()
            handler.removeCallbacks(stopAdvertisingRunnable)
            handler.postDelayed(stopAdvertisingRunnable, ADVERTISING_DURATION_MS)
        }
        return START_STICKY
    }

    // ── Zone cible ────────────────────────────────────────────────────────

    private lateinit var locationManager: LocationManager
    private var isInZone = false
    private var isMoving = false


    private lateinit var wakeLock: PowerManager.WakeLock

    private lateinit var bluetoothAdvertiser: BluetoothLeAdvertiser
    private lateinit var sensorManager: SensorManager
    private var isAdvertising = false
    private var isStartingAdvertising = false

    // ── Advertising settings ──────────────────────────────────────────────
    private val advertiseSettings = AdvertiseSettings.Builder()
        .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
        .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_MEDIUM)
        .setConnectable(false)
        .build()

    private val advertiseData = AdvertiseData.Builder()
        .addServiceUuid(ParcelUuid.fromString(BLE_UUID))
        .setIncludeDeviceName(false)
        .build()

    private fun startAdvertising() {
        if (ActivityCompat.checkSelfPermission(
                this, Manifest.permission.BLUETOOTH_ADVERTISE
            ) != PackageManager.PERMISSION_GRANTED
        ) return

        if (!isAdvertising && !isStartingAdvertising) {  // vérifie les deux
            isStartingAdvertising = true
            bluetoothAdvertiser.startAdvertising(
                advertiseSettings, advertiseData, advertiseCallback
            )
        }
    }

    private val advertiseCallback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) {
            isAdvertising = true
            isStartingAdvertising = false  // reset
            sendAdvertisingState(true)
        }
        override fun onStartFailure(errorCode: Int) {
            isAdvertising = false
            isStartingAdvertising = false  // reset
            Log.e("BleMotionService", "Advertising failed: $errorCode")
            sendAdvertisingState(false)
        }
    }

    private fun stopAdvertising() {
        if (ActivityCompat.checkSelfPermission(
                this, Manifest.permission.BLUETOOTH_ADVERTISE
            ) != PackageManager.PERMISSION_GRANTED
        ) return

        if (isAdvertising) {
            bluetoothAdvertiser.stopAdvertising(advertiseCallback)
            isAdvertising = false
            isStartingAdvertising = false
            sendAdvertisingState(false)
        }
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────
    override fun onCreate() {
        super.onCreate()

        // init BLE
        val adapter = BluetoothAdapter.getDefaultAdapter()
        bluetoothAdvertiser = adapter.bluetoothLeAdvertiser

        // locationManager en premier
        locationManager = getSystemService(LOCATION_SERVICE) as LocationManager

        // init capteur de mouvement
        sensorManager = getSystemService(SENSOR_SERVICE) as SensorManager
        val accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        sensorManager.registerListener(this, accelerometer,
            SensorManager.SENSOR_DELAY_NORMAL)

        // maintient le téléphone actif
        val powerManager = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK,
            "BleMotionService::WakeLock"
        )
        wakeLock.acquire() // maintient le CPU actif

        startForeground(1, buildNotification())
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacks(stopAdvertisingRunnable)
        locationManager?.removeUpdates(this)
        if (wakeLock.isHeld) wakeLock.release()
        stopAdvertising()
        sensorManager.unregisterListener(this)
    }

    private val handler = Handler(Looper.getMainLooper())
    private val stopAdvertisingRunnable = Runnable {
        isMoving = false
        updateAdvertising()
    }

    // ── Détection mouvement ───────────────────────────────────────────────
    private var lastX = 0f; private var lastY = 0f; private var lastZ = 0f

    override fun onSensorChanged(event: SensorEvent?) {
        event ?: return
        val x = event.values[0]
        val y = event.values[1]
        val z = event.values[2]

        val delta = Math.abs(x - lastX) + Math.abs(y - lastY) + Math.abs(z - lastZ)
        lastX = x; lastY = y; lastZ = z

        if (delta > 2.0f) {
            isMoving = true
            requestSingleLocationUpdate()

            handler.removeCallbacks(stopAdvertisingRunnable)
            handler.postDelayed(stopAdvertisingRunnable, ADVERTISING_DURATION_MS) // plus 10_000L hardcodé
        }

        updateAdvertising()
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}


    // ── Localisation ───────────────────────────────────────────────

    private var lastGpsRequest = 0L

    private fun requestSingleLocationUpdate() {
        val now = System.currentTimeMillis()
        if (now - lastGpsRequest < 3000) return // max 1 requête toutes les 3s
        lastGpsRequest = now

        if (ActivityCompat.checkSelfPermission(
                this, Manifest.permission.ACCESS_FINE_LOCATION
            ) != PackageManager.PERMISSION_GRANTED
        ) return

        locationManager.requestSingleUpdate(
            LocationManager.GPS_PROVIDER,
            this,
            mainLooper
        )
    }

    // ── LocationListener ─────────────────────────────────────────────────
    override fun onLocationChanged(location: Location) {
        val target = Location("target").apply {
            latitude = TARGET_LAT
            longitude = TARGET_LNG
        }
        val distance = location.distanceTo(target)
        isInZone = distance <= TARGET_RADIUS

        Log.d("BleMotionService", "Distance zone: ${distance}m — inZone: $isInZone")
        updateAdvertising()
    }


    // ── BLE ───────────────────────────────────────────────────────────────
    private fun updateAdvertising() {
        if (isMoving && isInZone) {
            startAdvertising()
        } else {
            stopAdvertising()
        }
    }

    // ── Notification (obligatoire foreground service) ─────────────────────
    private fun buildNotification(): Notification {
        val channel = NotificationChannel("ble_channel", "BLE Service",
            NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(channel)

        return NotificationCompat.Builder(this, "ble_channel")
            .setContentTitle("BLE actif")
            .setContentText("Advertising selon mouvement")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .build()
    }

    override fun onBind(intent: Intent?) = null
}