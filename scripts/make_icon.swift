// Renders the GoViet app icon into AppIcon.appiconset.
// The glyph is a bold "V" with an acute accent (dấu sắc) over a red gradient.
// Usage: swift scripts/make_icon.swift <output-appiconset-dir>

import AppKit
import Foundation

let outDir = CommandLine.arguments.count > 1
    ? CommandLine.arguments[1]
    : "Sources/Support/Assets.xcassets/AppIcon.appiconset"

try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

/// Stroke path through points given in unit space (0…1, y-up) of `frame`.
func strokePath(in frame: NSRect, points: [NSPoint], width: CGFloat) -> NSBezierPath {
    let path = NSBezierPath()
    for (i, p) in points.enumerated() {
        let converted = NSPoint(x: frame.minX + p.x * frame.width, y: frame.minY + p.y * frame.height)
        if i == 0 { path.move(to: converted) } else { path.line(to: converted) }
    }
    path.lineWidth = width * frame.width
    path.lineCapStyle = .round
    path.lineJoinStyle = .round
    return path
}

func renderIcon(pixels: Int) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
                               bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                               isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

    let s = CGFloat(pixels)
    // macOS icon convention: content inset ~10%, continuous-corner rect
    let inset = s * 0.10
    let rect = NSRect(x: inset, y: inset, width: s - inset * 2, height: s - inset * 2)
    let radius = rect.width * 0.225
    let badge = NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius)

    // warm red gradient
    let gradient = NSGradient(colors: [
        NSColor(srgbRed: 0.90, green: 0.25, blue: 0.24, alpha: 1.0),
        NSColor(srgbRed: 0.62, green: 0.08, blue: 0.11, alpha: 1.0),
    ])!
    gradient.draw(in: badge, angle: -90)

    // subtle inner highlight
    let highlight = NSBezierPath(roundedRect: rect.insetBy(dx: rect.width * 0.012, dy: rect.width * 0.012),
                                 xRadius: radius * 0.96, yRadius: radius * 0.96)
    NSColor.white.withAlphaComponent(0.14).setStroke()
    highlight.lineWidth = max(1, s * 0.008)
    highlight.stroke()

    NSGraphicsContext.current?.cgContext.setShadow(offset: CGSize(width: 0, height: -s * 0.01),
                                                   blur: s * 0.03,
                                                   color: NSColor.black.withAlphaComponent(0.35).cgColor)

    // the "V"
    NSColor.white.setStroke()
    strokePath(in: rect,
               points: [NSPoint(x: 0.25, y: 0.68), NSPoint(x: 0.50, y: 0.22), NSPoint(x: 0.75, y: 0.68)],
               width: 0.135).stroke()

    // dấu sắc
    NSColor(srgbRed: 1.0, green: 0.84, blue: 0.04, alpha: 1.0).setStroke()
    strokePath(in: rect,
               points: [NSPoint(x: 0.45, y: 0.78), NSPoint(x: 0.60, y: 0.88)],
               width: 0.085).stroke()

    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let sizes: [(name: String, points: Int, scale: Int)] = [
    ("icon_16x16", 16, 1), ("icon_16x16@2x", 16, 2),
    ("icon_32x32", 32, 1), ("icon_32x32@2x", 32, 2),
    ("icon_128x128", 128, 1), ("icon_128x128@2x", 128, 2),
    ("icon_256x256", 256, 1), ("icon_256x256@2x", 256, 2),
    ("icon_512x512", 512, 1), ("icon_512x512@2x", 512, 2),
]

var images: [[String: String]] = []
for entry in sizes {
    let px = entry.points * entry.scale
    let rep = renderIcon(pixels: px)
    let data = rep.representation(using: .png, properties: [:])!
    let filename = "\(entry.name).png"
    try! data.write(to: URL(fileURLWithPath: "\(outDir)/\(filename)"))
    images.append([
        "filename": filename,
        "idiom": "mac",
        "scale": "\(entry.scale)x",
        "size": "\(entry.points)x\(entry.points)",
    ])
}

let contents: [String: Any] = [
    "images": images,
    "info": ["author": "xcode", "version": 1],
]
let json = try! JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
try! json.write(to: URL(fileURLWithPath: "\(outDir)/Contents.json"))
print("AppIcon written to \(outDir)")
