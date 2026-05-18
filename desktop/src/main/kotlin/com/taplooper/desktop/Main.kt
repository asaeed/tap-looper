package com.taplooper.desktop

import androidx.compose.runtime.*
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.taplooper.domain.AppViewModel
import com.taplooper.ui.TapLooperScreen

fun main() = application {
    val vm = remember { AppViewModel() }
    DisposableEffect(Unit) { onDispose { vm.dispose() } }

    val state = rememberWindowState(size = DpSize(360.dp, 720.dp))
    Window(
        onCloseRequest = ::exitApplication,
        title = "Tap Looper",
        state = state,
    ) {
        TapLooperScreen(vm)
    }
}
