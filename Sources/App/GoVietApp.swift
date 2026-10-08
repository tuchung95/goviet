//
//  GoVietApp.swift
//  goviet
//
//  Menu-bar Vietnamese input method for macOS 26, built on the OpenKey
//  engine with a SwiftUI interface.
//

import AppKit
import SwiftUI

@main
struct GoVietApp: App {
    @NSApplicationDelegateAdaptor(GoVietAppDelegate.self) private var appDelegate
    @StateObject private var state = AppState.shared

    var body: some Scene {
        MenuBarExtra {
            MenuContent()
                .environmentObject(state)
        } label: {
            MenuBarLabel()
                .environmentObject(state)
        }

        Window("GoViet — Bộ gõ Tiếng Việt", id: "settings") {
            SettingsRootView()
                .environmentObject(state)
                .background(WindowAttachment(id: "settings"))
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .defaultSize(width: 820, height: 560)

        Window("Chào mừng đến với GoViet", id: "welcome") {
            WelcomePage()
                .environmentObject(state)
                .background(WindowAttachment(id: "welcome"))
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .defaultSize(width: 480, height: 450)
    }
}

/// Lives permanently in the menu bar, so it is the one SwiftUI view that can
/// reliably receive the "open settings" request from the AppKit delegate.
struct MenuBarLabel: View {
    @EnvironmentObject private var state: AppState
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Image(nsImage: StatusIcon.image(vietnamese: state.isVietnamese, gray: state.grayIcon, excluded: state.currentAppExcluded))
            .onReceive(NotificationCenter.default.publisher(for: .gvOpenSettingsWindow)) { _ in
                AppWindowPresenter.shared.open("settings", using: openWindow)
            }
            .onReceive(NotificationCenter.default.publisher(for: .gvOpenWelcomeWindow)) { _ in
                AppWindowPresenter.shared.open("welcome", using: openWindow)
            }
    }
}

struct MenuContent: View {
    @EnvironmentObject private var state: AppState
    @ObservedObject private var updater = UpdateChecker.shared
    @ObservedObject private var clipboard = ClipboardManager.shared
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        if let version = updater.availableVersion {
            Button("Cập nhật lên GoViet \(version)…") { updater.checkForUpdates() }
            Divider()
        }

        if !state.accessibilityGranted {
            Button("Cấp quyền Trợ năng…") {
                openWelcomeWindow()
            }
            Button("Kiểm tra cập nhật…") { updater.checkForUpdates() }
                .disabled(!updater.canCheckForUpdates)
            Divider()
            Button("Thoát GoViet") {
                NSApp.terminate(nil)
            }
            .keyboardShortcut("q")
        } else {
            Toggle("Tiếng Việt", isOn: $state.isVietnamese)
                .dynamicShortcut(state.switchKeyStatus)
                .disabled(state.currentAppExcluded)

            if let bundleID = state.frontAppBundleID {
                Toggle(isOn: Binding(
                    get: { state.currentAppExcluded },
                    set: { _ in state.toggleExcluded(bundleID: bundleID) }
                )) {
                    Text("Tắt goviet cho \(excludeTargetName)")
                }
            }

            Divider()

            Picker("Kiểu gõ", selection: $state.inputType) {
                ForEach(AppState.inputTypeNames.indices, id: \.self) { i in
                    Text(AppState.inputTypeNames[i]).tag(i)
                }
            }

            Picker("Bảng mã", selection: $state.codeTable) {
                ForEach(AppState.codeTableNames.indices, id: \.self) { i in
                    Text(AppState.codeTableNames[i]).tag(i)
                }
            }

            Divider()

            Button("Chuyển mã nhanh") {
                GVBridge.engineRequestsQuickConvert()
            }
            .dynamicShortcut(state.convertHotKey)

            Button("Công cụ chuyển mã…") { open(.convert) }
            Button("Gõ tắt…") { open(.macro) }

            if clipboard.enabled {
                Button("Lịch sử Clipboard") {
                    ClipboardManager.shared.togglePicker()
                }
                .dynamicShortcut(clipboard.hotKey)
            }

            Divider()

            Button("Bảng điều khiển…") { open(.typing) }
            Button("Giới thiệu GoViet") { open(.about) }
            Button("Kiểm tra cập nhật…") { updater.checkForUpdates() }
                .disabled(!updater.canCheckForUpdates)

            Divider()

            Button("Thoát GoViet") {
                NSApp.terminate(nil)
            }
            .keyboardShortcut("q")
        }
    }

    /// Short, friendly name of the app the exclude toggle acts on.
    private var excludeTargetName: String {
        if let name = state.frontAppName, !name.isEmpty { return name }
        if let bid = state.frontAppBundleID {
            return bid.components(separatedBy: ".").last ?? bid
        }
        return "ứng dụng này"
    }

    private func openWelcomeWindow() {
        AppWindowPresenter.shared.open("welcome", using: openWindow)
    }

    private func open(_ page: SettingsPage) {
        state.selectedPage = page
        AppWindowPresenter.shared.open("settings", using: openWindow)
    }
}

@MainActor
final class GoVietAppDelegate: NSObject, NSApplicationDelegate {
    private var permissionTimer: Timer?

    func applicationWillFinishLaunching(_ notification: Notification) {
        AppState.registerDefaultSettings()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let state = AppState.shared
        NSApp.setActivationPolicy(state.showIconOnDock ? .regular : .accessory)

        // Seed the frontmost-app tracking so the menu-bar exclude item works
        // before the first app switch.
        if let app = NSWorkspace.shared.frontmostApplication,
           app.bundleIdentifier != Bundle.main.bundleIdentifier {
            state.updateFrontApp(bundleID: app.bundleIdentifier, name: app.localizedName)
        }

        registerWorkspaceNotifications()
        observeQuickConvert()

        // clipboard history runs independently from the engine
        ClipboardManager.shared.startIfEnabled()

        // Sparkle owns scheduled checks, signed downloads and installation.
        UpdateChecker.shared.start()

        // banner "Mở Cài đặt hệ thống" button asks us to (re-)register for AX
        NotificationCenter.default.addObserver(forName: .gvRequestAccessibility,
                                               object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                self?.askForAccessibility()
            }
        }

        if AXIsProcessTrusted() {
            startEngine()
        } else {
            state.accessibilityGranted = false
            askForAccessibility()
        }

        if state.showUIOnStartup || !state.accessibilityGranted {
            // delay until the MenuBarExtra label is installed and can route the request
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
                self?.openSettingsWindow()
            }
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag { openSettingsWindow() }
        return true
    }

    // MARK: Engine

    private func startEngine() {
        AppState.shared.accessibilityGranted = true
        if !GVBridge.startEventTap() {
            // tap creation failed although AX is granted (e.g. permission revoked mid-flight)
            AppState.shared.accessibilityGranted = false
            askForAccessibility()
        }
    }

    private func askForAccessibility() {
        // show the system prompt, then poll until granted
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
        AXIsProcessTrustedWithOptions(options)

        permissionTimer?.invalidate()
        // .common mode so the poll keeps firing while a modal sheet / menu tracking is up.
        let timer = Timer(timeInterval: 1.5, repeats: true) { [weak self] timer in
            if AXIsProcessTrusted() {
                timer.invalidate()
                Task { @MainActor in self?.startEngine() }
            }
        }
        RunLoop.main.add(timer, forMode: .common)
        permissionTimer = timer
    }

    // MARK: Notifications

    private func registerWorkspaceNotifications() {
        let center = NSWorkspace.shared.notificationCenter
        center.addObserver(self, selector: #selector(receiveWake), name: NSWorkspace.didWakeNotification, object: nil)
        center.addObserver(self, selector: #selector(receiveSleep), name: NSWorkspace.willSleepNotification, object: nil)
        center.addObserver(self, selector: #selector(spaceChanged), name: NSWorkspace.activeSpaceDidChangeNotification, object: nil)
        center.addObserver(self, selector: #selector(activeAppChanged), name: NSWorkspace.didActivateApplicationNotification, object: nil)
    }

    private func observeQuickConvert() {
        NotificationCenter.default.addObserver(forName: .GVQuickConvertDidRun,
                                               object: nil, queue: .main) { note in
            let success = (note.object as? NSNumber)?.boolValue ?? false
            Task { @MainActor in
                guard GVBridge.convertAlertWhenCompleted || !success else { return }
                let alert = NSAlert()
                alert.messageText = success ? "Chuyển mã thành công!" : "Không có dữ liệu trong clipboard!"
                alert.informativeText = success ? "Kết quả đã được lưu trong clipboard." : "Hãy sao chép một đoạn văn bản để chuyển đổi."
                alert.addButton(withTitle: "OK")
                alert.window.level = .statusBar
                alert.runModal()
            }
        }
    }

    @objc private func receiveWake(_ note: Notification) {
        _ = GVBridge.startEventTap()
    }

    @objc private func receiveSleep(_ note: Notification) {
        _ = GVBridge.stopEventTap()
    }

    @objc private func spaceChanged(_ note: Notification) {
        GVBridge.requestNewSession()
    }

    @objc private func activeAppChanged(_ note: Notification) {
        let tapRunning = GVBridge.isEventTapRunning()
        // Track the frontmost non-GoViet app so the menu-bar "exclude this app"
        // item and the icon's excluded state stay current. Refresh the engine's
        // exclude flag too (independent of smart switch).
        if let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication,
           app.bundleIdentifier != Bundle.main.bundleIdentifier {
            AppState.shared.updateFrontApp(bundleID: app.bundleIdentifier, name: app.localizedName)
            if tapRunning { GVBridge.frontMostAppChanged() }
        }
        if vUseSmartSwitchKey != 0 && tapRunning {
            GVBridge.activeAppChanged()
        }
    }

    // MARK: Window

    private func openSettingsWindow() {
        let state = AppState.shared
        if state.accessibilityGranted {
            if let welcomeWindow = NSApp.windows.first(where: { $0.identifier?.rawValue == "welcome" }) {
                welcomeWindow.close()
            }
            NotificationCenter.default.post(name: .gvOpenSettingsWindow, object: nil)
        } else {
            if let settingsWindow = NSApp.windows.first(where: { $0.identifier?.rawValue == "settings" }) {
                settingsWindow.close()
            }
            NotificationCenter.default.post(name: .gvOpenWelcomeWindow, object: nil)
        }
    }
}

extension Notification.Name {
    static let gvOpenSettingsWindow = Notification.Name("GVOpenSettingsWindow")
    static let gvOpenWelcomeWindow = Notification.Name("GVOpenWelcomeWindow")
    static let gvRequestAccessibility = Notification.Name("GVRequestAccessibility")
}

extension View {
    @ViewBuilder
    func dynamicShortcut(_ status: Int32) -> some View {
        if let shortcut = ShortcutParser.parse(status) {
            self.keyboardShortcut(shortcut.key, modifiers: shortcut.modifiers)
        } else {
            self
        }
    }
}

private struct ShortcutParser {
    static func parse(_ status: Int32) -> (key: KeyEquivalent, modifiers: EventModifiers)? {
        let value = UInt32(bitPattern: status)
        let char = UInt8((value >> 24) & 0xFF)
        guard char != 0xFE && char != 0 else { return nil }
        
        let key: KeyEquivalent
        if char == 49 || char == 32 {
            key = .space
        } else {
            let letter = String(UnicodeScalar(char)).lowercased()
            if let c = letter.first {
                key = KeyEquivalent(c)
            } else {
                key = KeyEquivalent(" ")
            }
        }
        
        var modifiers: EventModifiers = []
        if value & 0x100 != 0 { _ = modifiers.insert(.control) }
        if value & 0x200 != 0 { _ = modifiers.insert(.option) }
        if value & 0x400 != 0 { _ = modifiers.insert(.command) }
        if value & 0x800 != 0 { _ = modifiers.insert(.shift) }
        
        return (key, modifiers)
    }
}
