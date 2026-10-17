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
