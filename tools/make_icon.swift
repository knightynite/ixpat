// Renders the iXpat app icon master (1024x1024 PNG): a pale frosted squircle and a
// soft, top-down Surface Mouse in neutral tones with a faint accent halo. Quiet on
// purpose: no loud strokes, the mouse shape carries it.
// Run:  swift tools/make_icon.swift Resources/AppIcon-1024.png
import AppKit
import CoreGraphics

let args = CommandLine.arguments
guard args.count == 2 else { fatalError("usage: swift tools/make_icon.swift <out.png>") }

let S = 1024, content: CGFloat = 824
let space = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: S, height: S, bitsPerComponent: 8, bytesPerRow: S * 4,
                    space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(red: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255, blue: CGFloat(hex & 0xff) / 255, alpha: a)
}
func gradient(_ a: CGColor, _ b: CGColor) -> CGGradient { CGGradient(colorsSpace: space, colors: [a, b] as CFArray, locations: [0, 1])! }

let rect = CGRect(x: (CGFloat(S) - content) / 2, y: (CGFloat(S) - content) / 2, width: content, height: content)
let radius = content * 0.2237
let plate = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)

// Plate: pale frosted gradient with a soft drop shadow and a hairline rim.
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 26, color: CGColor(gray: 0, alpha: 0.22))
ctx.addPath(plate); ctx.setFillColor(rgb(0xEEF1F6)); ctx.fillPath()
ctx.restoreGState()
ctx.saveGState()
ctx.addPath(plate); ctx.clip()
ctx.drawLinearGradient(gradient(rgb(0xF7F9FC), rgb(0xDCE1E9)), start: CGPoint(x: 0, y: rect.maxY), end: CGPoint(x: 0, y: rect.minY), options: [])
// Faint accent halo behind the mouse: the only colour, and barely there.
let cx = rect.midX, cy = rect.midY
ctx.drawRadialGradient(CGGradient(colorsSpace: space, colors: [rgb(0x4C8DFF, 0.16), rgb(0x4C8DFF, 0)] as CFArray, locations: [0, 1])!,
                       startCenter: CGPoint(x: cx, y: cy + 10), startRadius: 0, endCenter: CGPoint(x: cx, y: cy + 10), endRadius: 330, options: [])
ctx.restoreGState()
ctx.saveGState()
ctx.addPath(CGPath(roundedRect: rect.insetBy(dx: 1.5, dy: 1.5), cornerWidth: radius - 1.5, cornerHeight: radius - 1.5, transform: nil))
ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.7)); ctx.setLineWidth(3); ctx.strokePath()
ctx.restoreGState()

// Mouse body (top-down), slightly larger than before; the Surface shell is one smooth piece.
let mw: CGFloat = 330, mh: CGFloat = 540
func pt(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: cx - mw / 2 + mw * x, y: cy + mh / 2 - mh * y) }   // y: 0 = front
let body = CGMutablePath()
body.move(to: pt(0.5, 0))
body.addCurve(to: pt(0.97, 0.22), control1: pt(0.80, 0), control2: pt(0.97, 0.06))
body.addCurve(to: pt(1.0, 0.70), control1: pt(0.99, 0.40), control2: pt(1.0, 0.55))
body.addCurve(to: pt(0.5, 1.0), control1: pt(1.0, 0.92), control2: pt(0.78, 1.0))
body.addCurve(to: pt(0.0, 0.70), control1: pt(0.22, 1.0), control2: pt(0.0, 0.92))
body.addCurve(to: pt(0.03, 0.22), control1: pt(0.0, 0.55), control2: pt(0.01, 0.40))
body.addCurve(to: pt(0.5, 0), control1: pt(0.03, 0.06), control2: pt(0.20, 0))
body.closeSubpath()

// Shadow: soft and low, like a shell resting on a desk.
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -18), blur: 40, color: CGColor(gray: 0.1, alpha: 0.28))
ctx.addPath(body); ctx.setFillColor(rgb(0xF2F4F7)); ctx.fillPath()
ctx.restoreGState()
// Shell shading: light top, cooler grey toward the tail, plus a broad highlight on the dome.
ctx.saveGState()
ctx.addPath(body); ctx.clip()
ctx.drawLinearGradient(gradient(rgb(0xFCFCFD), rgb(0xD9DEE6)), start: CGPoint(x: cx, y: pt(0.5, 0).y), end: CGPoint(x: cx, y: pt(0.5, 1).y), options: [])
ctx.drawRadialGradient(CGGradient(colorsSpace: space, colors: [CGColor(gray: 1, alpha: 0.85), CGColor(gray: 1, alpha: 0)] as CFArray, locations: [0, 1])!,
                       startCenter: CGPoint(x: cx - 30, y: pt(0.5, 0.42).y), startRadius: 0, endCenter: CGPoint(x: cx - 30, y: pt(0.5, 0.42).y), endRadius: 230, options: [])
// Side falloff so the shell reads as a dome, not a flat cutout.
ctx.drawLinearGradient(CGGradient(colorsSpace: space, colors: [CGColor(gray: 0.55, alpha: 0.18), CGColor(gray: 1, alpha: 0), CGColor(gray: 0.55, alpha: 0.22)] as CFArray, locations: [0, 0.5, 1])!,
                       start: CGPoint(x: pt(0, 0).x, y: cy), end: CGPoint(x: pt(1, 0).x, y: cy), options: [])
ctx.restoreGState()
// Rim: hairline so the white shell separates from the pale plate.
ctx.saveGState()
ctx.addPath(body); ctx.setStrokeColor(CGColor(gray: 0.45, alpha: 0.22)); ctx.setLineWidth(2.5); ctx.strokePath()
ctx.restoreGState()

// Details in graphite: the action pill at the front and the wheel slot below it.
func inset(_ r: CGRect, dark: CGColor, light: CGColor, corner: CGFloat) {
    let p = CGPath(roundedRect: r, cornerWidth: corner, cornerHeight: corner, transform: nil)
    ctx.saveGState()
    ctx.addPath(p); ctx.clip()
    ctx.drawLinearGradient(gradient(dark, light), start: CGPoint(x: r.midX, y: r.maxY), end: CGPoint(x: r.midX, y: r.minY), options: [])
    ctx.restoreGState()
    ctx.saveGState()
    ctx.addPath(CGPath(roundedRect: r.insetBy(dx: 0.75, dy: 0.75), cornerWidth: corner - 0.75, cornerHeight: corner - 0.75, transform: nil))
    ctx.setStrokeColor(CGColor(gray: 0, alpha: 0.10)); ctx.setLineWidth(1.5); ctx.strokePath()
    ctx.restoreGState()
}
inset(CGRect(x: cx - 8, y: pt(0.5, 0.17).y, width: 16, height: 40), dark: rgb(0x8A93A3), light: rgb(0xB9C1CE), corner: 8)
let wheel = CGRect(x: cx - 15, y: pt(0.5, 0.37).y, width: 30, height: 74)
inset(wheel, dark: rgb(0x6F7888), light: rgb(0xA3ACBA), corner: 11)
// Wheel ridges.
ctx.saveGState()
ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.35)); ctx.setLineWidth(2); ctx.setLineCap(.round)
for i in 0..<5 {
    let y = wheel.minY + 12 + CGFloat(i) * 12.5
    ctx.move(to: CGPoint(x: wheel.minX + 7, y: y)); ctx.addLine(to: CGPoint(x: wheel.maxX - 7, y: y))
}
ctx.strokePath()
ctx.restoreGState()

let final = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: final)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[1]))
print("wrote \(args[1])")
