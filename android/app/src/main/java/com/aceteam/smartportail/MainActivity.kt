package com.aceteam.smartportail

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.aceteam.smartportail.ui.theme.SmartPortailTheme
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel

class MainViewModel : ViewModel() {
    var serviceRunning by mutableStateOf(false)
    var forceAdvertising by mutableStateOf(false)
    var isAdvertising by mutableStateOf(false)
}
class MainActivity : ComponentActivity() {

    private val viewModel: MainViewModel by viewModels()

    private val advertisingReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            Log.d("MainActivity", "Broadcast reçu : ${intent.action}")
            if (intent.action == BleMotionService.ACTION_ADVERTISING_STATE) {
                val advertising = intent.getBooleanExtra(
                    BleMotionService.EXTRA_IS_ADVERTISING, false
                )
                Log.d("MainActivity", "isAdvertising: $advertising")
                runOnUiThread {
                    viewModel.isAdvertising = advertising
                    if (!advertising) {
                        viewModel.forceAdvertising = false
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        val filter = IntentFilter(BleMotionService.ACTION_ADVERTISING_STATE)
        registerReceiver(advertisingReceiver, filter, RECEIVER_NOT_EXPORTED)
    }

    override fun onPause() {
        super.onPause()
        unregisterReceiver(advertisingReceiver)
    }

    private fun forceAdvertise() {
        val intent = Intent(this, BleMotionService::class.java).apply {
            action = BleMotionService.ACTION_FORCE_ADVERTISE
        }
        startService(intent)
        viewModel.forceAdvertising = true

        Handler(Looper.getMainLooper()).postDelayed({
            viewModel.forceAdvertising = false
        }, BuildConfig.ADVERTISING_DURATION_MS)
    }

    private val permissionsLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        permissions.forEach { (permission, granted) ->
            Log.d("Permissions", "$permission : ${if (granted) "OK" else "REFUSÉE"}")
        }
        if (permissions.all { it.value }) {
            requestBackgroundLocation()
            startBleService()
        } else {
            Toast.makeText(this, "Ouvre les paramètres pour autoriser Bluetooth", Toast.LENGTH_LONG).show()
            val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.parse("package:$packageName")
            }
            startActivity(intent)
        }
    }

    private val backgroundLocationLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            Log.d("Permissions", "Background location OK")
        } else {
            Toast.makeText(this, "Position en arrière-plan refusée", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        requestBatteryOptimizationExemption()

        setContent {
            SmartPortailTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { innerPadding ->
                    MainScreen(
                        modifier = Modifier.padding(innerPadding),
                        viewModel = viewModel,
                        onStart = { requestPermissions() },
                        onStop = { stopBleService() },
                        onForce = { forceAdvertise() }
                    )
                }
            }
        }
    }

    private fun requestPermissions() {
        permissionsLauncher.launch(arrayOf(
            Manifest.permission.BLUETOOTH_ADVERTISE,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.ACCESS_FINE_LOCATION,
        ))
    }

    private fun requestBackgroundLocation() {
        backgroundLocationLauncher.launch(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
    }

    private fun startBleService() {
        val intent = Intent(this, BleMotionService::class.java)
        startForegroundService(intent)
        viewModel.serviceRunning = true
    }

    private fun stopBleService() {
        val intent = Intent(this, BleMotionService::class.java)
        stopService(intent)
        viewModel.serviceRunning = false
    }

    private fun requestBatteryOptimizationExemption() {
        val pm = getSystemService(POWER_SERVICE) as PowerManager
        if (!pm.isIgnoringBatteryOptimizations(packageName)) {
            val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                data = Uri.parse("package:$packageName")
            }
            startActivity(intent)
        }
    }
}

@Composable
fun MainScreen(
    modifier: Modifier = Modifier,
    viewModel: MainViewModel,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onForce: () -> Unit
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "Smart Portail",
            style = MaterialTheme.typography.headlineMedium
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = when {
                viewModel.forceAdvertising -> "Advertising forcé"
                viewModel.isAdvertising    -> "Advertising automatique en cours"
                viewModel.serviceRunning   -> "BLE actif — en attente de mouvement"
                else                       -> "Service arrêté"
            },
            style = MaterialTheme.typography.bodyMedium,
            color = when {
                viewModel.forceAdvertising -> MaterialTheme.colorScheme.tertiary
                viewModel.isAdvertising    -> MaterialTheme.colorScheme.primary
                viewModel.serviceRunning   -> MaterialTheme.colorScheme.onSurfaceVariant
                else                       -> MaterialTheme.colorScheme.onSurfaceVariant
            }
        )

        Spacer(modifier = Modifier.height(32.dp))

        Button(
            onClick = if (viewModel.serviceRunning) onStop else onStart,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(if (viewModel.serviceRunning) "Arrêter" else "Démarrer")
        }

        Spacer(modifier = Modifier.height(12.dp))

        OutlinedButton(
            onClick = onForce,
            enabled = viewModel.serviceRunning && !viewModel.forceAdvertising && !viewModel.isAdvertising,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(when {
                viewModel.forceAdvertising -> "Advertising forcé en cours..."
                viewModel.isAdvertising    -> "Advertising automatique en cours"
                else                       -> "Forcer advertising"
            })
        }
    }
}