#!/usr/bin/env swift
// Renders the macOS Installer sidebar artwork (light and dark appearance) as
// multi-resolution TIFFs. Run from the repository root:
//   swift scripts/generate-installer-art.swift
import AppKit

let outputDirectory = URL(fileURLWithPath: "cmake/macos/resources/installer", isDirectory: true)
// Installer draws the background behind its whole window; the step list and
// the opaque content pane leave only the lower-left sidebar area visible, so
// all artwork stays within about 170 points of the left edge.
let canvas = CGSize(width: 620, height: 418)

struct Palette {
    let title: NSColor
    let subtitle: NSColor
    let glowAlpha: CGFloat
}

let lightPalette = Palette(title: NSColor(calibratedWhite: 0.11, alpha: 1),
                           subtitle: NSColor(calibratedWhite: 0.38, alpha: 1),
                           glowAlpha: 0.20)
let darkPalette = Palette(title: NSColor(calibratedWhite: 0.96, alpha: 1),
                          subtitle: NSColor(calibratedWhite: 0.66, alpha: 1),
                          glowAlpha: 0.30)

let screenGreen = NSColor(srgbRed: 0.10, green: 0.78, blue: 0.42, alpha: 1)
let screenTeal = NSColor(srgbRed: 0.05, green: 0.62, blue: 0.66, alpha: 1)
let screenBlue = NSColor(srgbRed: 0.17, green: 0.45, blue: 0.96, alpha: 1)

func drawGlow(in context: CGContext, palette: Palette) {
    let colors = [screenGreen.withAlphaComponent(palette.glowAlpha).cgColor,
                  screenBlue.withAlphaComponent(0).cgColor] as CFArray
    let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors,
                              locations: [0, 1])!
    context.drawRadialGradient(gradient, startCenter: CGPoint(x: 44, y: 70), startRadius: 0,
                               endCenter: CGPoint(x: 44, y: 70), endRadius: 128, options: [])
}

func drawMark(in context: CGContext, rect: CGRect) {
    let radius = rect.width * 0.235
    let tile = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)

    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: -3), blur: 10,
                      color: NSColor.black.withAlphaComponent(0.28).cgColor)
    context.addPath(tile)
    context.setFillColor(screenTeal.cgColor)
    context.fillPath()
    context.restoreGState()

    context.saveGState()
    context.addPath(tile)
    context.clip()
    let colors = [screenGreen.cgColor, screenTeal.cgColor, screenBlue.cgColor] as CFArray
    let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors,
                              locations: [0, 0.5, 1])!
    context.drawLinearGradient(gradient, start: CGPoint(x: rect.minX, y: rect.maxY),
                               end: CGPoint(x: rect.maxX, y: rect.minY), options: [])

    // Subtle top highlight for depth.
    let highlight = [NSColor.white.withAlphaComponent(0.28).cgColor,
                     NSColor.white.withAlphaComponent(0).cgColor] as CFArray
    let highlightGradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
                                       colors: highlight, locations: [0, 1])!
    context.drawLinearGradient(highlightGradient, start: CGPoint(x: rect.midX, y: rect.maxY),
                               end: CGPoint(x: rect.midX, y: rect.midY), options: [])

    // Presenter silhouette: the foreground a chroma keyer separates.
    let unit = rect.width
    let head = CGRect(x: rect.midX - unit * 0.15, y: rect.minY + unit * 0.46,
                      width: unit * 0.30, height: unit * 0.30)
    let shoulders = CGMutablePath()
    shoulders.move(to: CGPoint(x: rect.minX + unit * 0.16, y: rect.minY))
    shoulders.addCurve(to: CGPoint(x: rect.midX, y: rect.minY + unit * 0.43),
                       control1: CGPoint(x: rect.minX + unit * 0.17, y: rect.minY + unit * 0.27),
                       control2: CGPoint(x: rect.midX - unit * 0.20, y: rect.minY + unit * 0.43))
    shoulders.addCurve(to: CGPoint(x: rect.maxX - unit * 0.16, y: rect.minY),
                       control1: CGPoint(x: rect.midX + unit * 0.20, y: rect.minY + unit * 0.43),
                       control2: CGPoint(x: rect.maxX - unit * 0.17, y: rect.minY + unit * 0.27))
    shoulders.closeSubpath()
    context.setFillColor(NSColor.white.withAlphaComponent(0.96).cgColor)
    context.addPath(shoulders)
    context.fillPath()
    context.fillEllipse(in: head)

    context.restoreGState()
}

func drawText(_ text: String, at point: CGPoint, font: NSFont, color: NSColor, kerning: CGFloat = 0) {
    let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color, .kern: kerning]
    NSAttributedString(string: text, attributes: attributes).draw(at: point)
}

func render(palette: Palette, scale: CGFloat) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(canvas.width * scale),
                               pixelsHigh: Int(canvas.height * scale), bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    rep.size = canvas
    NSGraphicsContext.saveGraphicsState()
    let graphics = NSGraphicsContext(bitmapImageRep: rep)!
    NSGraphicsContext.current = graphics
    let context = graphics.cgContext
    context.clear(CGRect(origin: .zero, size: canvas))
    context.setShouldAntialias(true)
    context.interpolationQuality = .high

    drawGlow(in: context, palette: palette)

    drawMark(in: context, rect: CGRect(x: 22, y: 78, width: 44, height: 44))
    drawText("Smart Chroma Key", at: CGPoint(x: 22, y: 50),
             font: NSFont.systemFont(ofSize: 13, weight: .semibold), color: palette.title)
    drawText("STUDIO KEYER FOR OBS", at: CGPoint(x: 22, y: 34),
             font: NSFont.systemFont(ofSize: 8.5, weight: .medium), color: palette.subtitle, kerning: 0.9)

    // Green-to-blue accent rule.
    let rule = CGRect(x: 22, y: 26, width: 86, height: 1.5)
    context.saveGState()
    context.addPath(CGPath(roundedRect: rule, cornerWidth: 0.75, cornerHeight: 0.75, transform: nil))
    context.clip()
    let accent = [screenGreen.cgColor, screenBlue.cgColor] as CFArray
    let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: accent,
                              locations: [0, 1])!
    context.drawLinearGradient(gradient, start: CGPoint(x: rule.minX, y: 0), end: CGPoint(x: rule.maxX, y: 0),
                               options: [])
    context.restoreGState()

    NSGraphicsContext.restoreGraphicsState()
    return rep
}

func writeTIFF(named name: String, palette: Palette) throws {
    let low = render(palette: palette, scale: 1)
    let high = render(palette: palette, scale: 2)
    // A two-representation TIFF lets Installer choose the Retina image.
    guard let data = NSBitmapImageRep.tiffRepresentationOfImageReps(in: [low, high], using: .lzw,
                                                                    factor: 0) else {
        throw NSError(domain: "art", code: 1)
    }
    try data.write(to: outputDirectory.appendingPathComponent(name))
}

try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true)
try writeTIFF(named: "background.tiff", palette: lightPalette)
try writeTIFF(named: "background-dark.tiff", palette: darkPalette)
print("Wrote installer artwork to \(outputDirectory.path)")

// Reuse the same vector mark for Windows and the offline setup guide.
let windowsDirectory = URL(fileURLWithPath: ".github/scripts/resources/branding", isDirectory: true)
try FileManager.default.createDirectory(at: windowsDirectory, withIntermediateDirectories: true)
func bitmap(width: Int, height: Int, alpha: Bool = true, draw: (CGContext) -> Void) -> NSBitmapImageRep {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
        colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)!
    let context = NSGraphicsContext.current!.cgContext
    context.clear(CGRect(x: 0, y: 0, width: width, height: height))
    context.setShouldAntialias(true)
    draw(context)
    NSGraphicsContext.restoreGraphicsState()
    return rep
}
let logo = bitmap(width: 256, height: 256) { context in
    drawMark(in: context, rect: CGRect(x: 20, y: 20, width: 216, height: 216))
}
let logoPNG = logo.representation(using: .png, properties: [:])!
try logoPNG.write(to: windowsDirectory.appendingPathComponent("logo.png"))
try logoPNG.write(to: outputDirectory.appendingPathComponent("logo.png"))
let sidebar = bitmap(width: 328, height: 628, alpha: false) { context in
    context.scaleBy(x: 2, y: 2)
    context.setFillColor(NSColor(srgbRed: 0.035, green: 0.09, blue: 0.13, alpha: 1).cgColor)
    context.fill(CGRect(x: 0, y: 0, width: 164, height: 314))
    drawGlow(in: context, palette: darkPalette)
    drawMark(in: context, rect: CGRect(x: 31, y: 184, width: 102, height: 102))
    drawText("SMART", at: CGPoint(x: 22, y: 145), font: NSFont.systemFont(ofSize: 17, weight: .bold), color: .white, kerning: 2)
    drawText("CHROMA KEY", at: CGPoint(x: 22, y: 124), font: NSFont.systemFont(ofSize: 16, weight: .bold), color: .white)
    drawText("Classic + AI", at: CGPoint(x: 22, y: 97), font: NSFont.systemFont(ofSize: 12, weight: .medium), color: screenGreen)
    drawText("FOR OBS STUDIO", at: CGPoint(x: 22, y: 31), font: NSFont.systemFont(ofSize: 9, weight: .medium), color: NSColor(white: 0.76, alpha: 1), kerning: 0.7)
}
try sidebar.representation(using: .bmp, properties: [:])!.write(to: windowsDirectory.appendingPathComponent("wizard.bmp"))
try sidebar.representation(using: .png, properties: [:])!.write(to: windowsDirectory.appendingPathComponent("wizard-preview.png"))
let small = bitmap(width: 128, height: 128, alpha: false) { context in
    context.setFillColor(NSColor.white.cgColor)
    context.fill(CGRect(x: 0, y: 0, width: 128, height: 128))
    drawMark(in: context, rect: CGRect(x: 12, y: 12, width: 104, height: 104))
}
try small.representation(using: .bmp, properties: [:])!.write(to: windowsDirectory.appendingPathComponent("wizard-small.bmp"))
// ICO directory with PNG images at all common Windows icon sizes.
let sizes = [16, 32, 48, 64, 128, 256]
let iconPNGs = sizes.map { size in
    bitmap(width: size, height: size) { context in
        let inset = CGFloat(size) * 0.08
        drawMark(in: context, rect: CGRect(x: inset, y: inset, width: CGFloat(size) - 2 * inset, height: CGFloat(size) - 2 * inset))
    }.representation(using: .png, properties: [:])!
}
var ico = Data()
func append16(_ value: UInt16) { var v = value.littleEndian; withUnsafeBytes(of: &v) { ico.append(contentsOf: $0) } }
func append32(_ value: UInt32) { var v = value.littleEndian; withUnsafeBytes(of: &v) { ico.append(contentsOf: $0) } }
append16(0); append16(1); append16(UInt16(sizes.count))
var offset = UInt32(6 + 16 * sizes.count)
for (size, png) in zip(sizes, iconPNGs) {
    ico.append(contentsOf: [UInt8(size % 256), UInt8(size % 256), 0, 0])
    append16(1); append16(32); append32(UInt32(png.count)); append32(offset)
    offset += UInt32(png.count)
}
for png in iconPNGs { ico.append(png) }
try ico.write(to: windowsDirectory.appendingPathComponent("setup.ico"))
print("Wrote Windows wizard, application icon and shared logo")
