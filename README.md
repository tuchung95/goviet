# GoViet — Bộ gõ Tiếng Việt cho macOS

**GoViet** là bộ gõ tiếng Việt cho macOS 14 trở lên, giao diện SwiftUI, dùng engine
của [OpenKey](https://github.com/tuyenvm/OpenKey) (© Tuyen Mai, GPL v3).

## Tính năng

- Telex/VNI, 5 bảng mã, kiểm tra chính tả, gõ tắt, chuyển mã, lịch sử clipboard.
- Nhớ chế độ gõ theo từng ứng dụng; loại trừ hẳn ứng dụng không muốn gõ tiếng Việt.
- Icon VI/EN trên thanh menu, cửa sổ cài đặt kiểu System Settings.
- Tự cập nhật (Sparkle): khi có bản mới, menu hiện nút **"Cập nhật lên GoViet x.y.z…"**.
- Build bằng **Command Line Tools**, không cần Xcode (`scripts/build.sh`).
- Cài đặt nằm ở bundle ID `vn.goviet.app`, lịch sử clipboard ở
  `~/Library/Application Support/GoViet`, gõ tắt đồng bộ qua thư mục `goviet` trên iCloud Drive.

## Tải về

Tải `GoViet-x.y.z.zip` ở [Releases](https://github.com/tuchung95/goviet/releases), giải nén
và kéo `GoViet.app` vào **Applications**. App chưa được Apple notarize, nên lần mở đầu
tiên macOS sẽ chặn: chuột phải vào app → **Open**, hoặc vào System Settings → Privacy &
Security → **Open Anyway**. Các bản sau cập nhật ngay trong app.

## Build & cài đặt

Yêu cầu: macOS 14+ và Command Line Tools (`xcode-select --install`).

```bash
scripts/setup-signing.sh    # chạy một lần: tạo chứng chỉ ký "GoViet Local Signing"
scripts/build.sh            # build GoViet.app vào ~/Library/Caches/GoViet-build
scripts/build.sh install    # build, cài vào /Applications và mở
```

| Biến môi trường | Mặc định | Ý nghĩa |
|---|---|---|
| `CONFIG` | `release` | `debug` để build không tối ưu, có debug symbol |
| `ARCHS` | kiến trúc máy hiện tại | `"arm64 x86_64"` để build universal |
| `SIGN_IDENTITY` | `GoViet Local Signing` nếu có, không thì `-` (ad-hoc) | Tên chứng chỉ ký code |
| `SDK` | tự chọn | Đường dẫn tới macOS SDK |
| `BUILD_DIR` | `~/Library/Caches/GoViet-build` | Phải nằm trên ổ APFS (xem bên dưới) |

## Phát hành bản mới

```bash
scripts/release.sh 1.1.0 [release-notes.md]
```

Script tăng phiên bản trong `Info.plist`, build bản universal, ký file zip, release notes
và `appcast.xml` bằng khóa EdDSA của Sparkle, rồi commit, tag `v1.1.0`, push và tạo
GitHub Release. App đã cài sẽ thấy bản mới qua
`https://github.com/tuchung95/goviet/releases/latest/download/appcast.xml`.
Không có file release notes thì script dùng danh sách commit kể từ tag trước.

Cần có trong login keychain của máy phát hành:
- Chứng chỉ **"GoViet Local Signing"**. Mọi bản phát hành phải ký cùng một chứng chỉ,
  nếu không Sparkle sẽ từ chối bản cập nhật và macOS đòi cấp lại quyền Trợ năng.
- Khóa EdDSA của Sparkle (tài khoản `vn.goviet.app`). Khóa công khai nằm trong `Info.plist`.

**Hãy sao lưu cả hai.** Mất khóa thì các bản đã cài không thể cập nhật lên bản mới nữa.
Xuất ra file để lưu trữ an toàn:
`~/Library/Caches/GoViet-build/Sparkle-2.9.6/bin/generate_keys --account vn.goviet.app -x goviet-sparkle.key`
và xuất chứng chỉ bằng Keychain Access (định dạng .p12).

Sinh lại icon: `swift scripts/make_icon.swift Sources/Support/Assets.xcassets/AppIcon.appiconset`.

Nếu đã cài Xcode và [XcodeGen](https://github.com/yonaskolb/XcodeGen), có thể dùng
`xcodegen generate` rồi mở `GoViet.xcodeproj`.

## Lưu ý

- **Chỉ chạy một bộ gõ.** Nếu GoViet chạy cùng EVKey, OpenKey, Unikey… và các bộ gõ đều
  có quyền Trợ năng, phím sẽ bị xử lý hai lần. Hãy thoát bộ gõ kia và tắt nó trong
  System Settings → Privacy & Security → Accessibility.
- **Quyền Trợ năng khi cài lại.** macOS gắn quyền với chứng chỉ đã ký app. Khi ký bằng
  "GoViet Local Signing" (tạo bởi `scripts/setup-signing.sh`), các bản build sau giữ
  nguyên quyền, không phải bật lại. Nếu không có chứng chỉ, app được ký ad-hoc: chữ ký
  đổi theo từng bản build nên phải bật lại quyền mỗi lần cài. Khi định danh ký thay đổi,
  `build.sh install` tự xoá quyền cũ để macOS hỏi lại. Chứng chỉ chỉ dùng trên máy
  này; xoá bằng `security delete-identity -c "GoViet Local Signing"`.
- **SDK macOS 27.** Trong SDK này, `@State` của SwiftUI là macro, và plugin của macro
  chỉ có trong Xcode. Khi chỉ có Command Line Tools, script tự chuyển sang SDK 26.x
  (đi kèm Command Line Tools). App vẫn chạy bình thường trên macOS 27.
- **Ổ exFAT.** Ổ exFAT sinh file `._*` cho từng file, làm `codesign` báo lỗi. Vì vậy
  thư mục build mặc định nằm trên ổ trong của máy, và script bỏ qua các file `._*`
  khi gom source.

## Cấu trúc

```
goviet/
├── project.yml              # đặc tả XcodeGen (tuỳ chọn, cần Xcode)
├── scripts/build.sh         # build bằng Command Line Tools
├── scripts/release.sh       # phát hành bản mới lên GitHub Releases
├── scripts/setup-signing.sh # tạo chứng chỉ ký cố định (chạy một lần)
├── scripts/make_icon.swift  # sinh app icon bằng CoreGraphics
└── Sources/
    ├── Engine/              # engine C++ nguyên gốc từ OpenKey (GPL v3)
    ├── Platform/            # glue ObjC++: event tap, bridge engine ↔ Swift
    ├── App/                 # SwiftUI: MenuBarExtra, Settings, AppState
    └── Support/             # Info.plist, entitlements, bridging header, assets
```

## Giấy phép

Engine © Tuyen Mai (OpenKey). Một phần mã nguồn giao diện và lớp cầu nối © Anh Tuấn.
Toàn bộ dự án phát hành theo **GPL v3** (xem `LICENSE`). Khi phân phối GoViet, bạn phải
giữ thông tin bản quyền và công bố mã nguồn.
