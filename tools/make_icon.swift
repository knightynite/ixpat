// Renders the Thrum app icon master (1024x1024 PNG): a dark squircle, a mouse
// silhouette, and haptic ripple arcs on both sides.
// Run:  swift tools/make_icon.swift Resources/AppIcon-1024.png
import AppKit
import CoreGraphics

let args = CommandLine.arguments
guard args.count == 2 else { fatalError("usage: swift tools/make_icon.swift <out.png>") }

let S = 1024, content: CGFloat = 824
let ctx = CGContext(data: nil, width: S, height: S, bitsPerComponent: 8, bytesPerRow: S * 4,
                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
let rect = CGRect(x: (CGFloat(S) - content) / 2, y: (CGFloat(S) - content) / 2, width: content, height: content)
let radius = content * 0.2237
let plate = CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)

ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -12), blur: 28, color: CGColor(gray: 0, alpha: 0.30))
ctx.addPath(plate); ctx.setFillColor(CGColor(gray: 0.1, alpha: 1)); ctx.fillPath()
ctx.restoreGState()

ctx.saveGState()
ctx.addPath(plate); ctx.clip()
let grad = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB)!,
                      colors: [CGColor(red: 0.26, green: 0.27, blue: 0.32, alpha: 1),
                               CGColor(red: 0.07, green: 0.07, blue: 0.09, alpha: 1)] as CFArray,
                      locations: [0, 1])!
ctx.drawLinearGradient(grad, start: CGPoint(x: 0, y: rect.maxY), end: CGPoint(x: 0, y: rect.minY), options: [])

// Mouse body (top-down), centered.
let cx = rect.midX, cy = rect.midY
let mw: CGFloat = 290, mh: CGFloat = 490
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
ctx.saveGState()
ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: CGColor(gray: 0, alpha: 0.45))
ctx.addPath(body); ctx.setFillColor(CGColor(red: 0.93, green: 0.93, blue: 0.95, alpha: 1)); ctx.fillPath()
ctx.restoreGState()
// Action button (front pill) and wheel below it, in accent blue; no seam on the real shell.
let accent = CGColor(red: 0.04, green: 0.52, blue: 1.0, alpha: 1)
ctx.setFillColor(accent)
ctx.addPath(CGPath(roundedRect: CGRect(x: cx - 9, y: pt(0.5, 0.16).y, width: 18, height: 44), cornerWidth: 9, cornerHeight: 9, transform: nil)); ctx.fillPath()
ctx.addPath(CGPath(roundedRect: CGRect(x: cx - 16, y: pt(0.5, 0.36).y, width: 32, height: 70), cornerWidth: 11, cornerHeight: 11, transform: nil)); ctx.fillPath()
// Haptic ripples.
ctx.setStrokeColor(accent); ctx.setLineCap(.round)
for (i, r) in [70, 120, 170].enumerated() {
    ctx.setLineWidth(CGFloat(22 - i * 4))
    ctx.setAlpha(CGFloat(0.95 - Double(i) * 0.28))
    for side in [-1.0, 1.0] {
        let ox = cx + side * (mw / 2 + 40)
        let p = CGMutablePath()
        let a0 = side > 0 ? -0.45 : CGFloat.pi - 0.45, a1 = side > 0 ? 0.45 : CGFloat.pi + 0.45
        p.addArc(center: CGPoint(x: ox - side * 40, y: cy), radius: CGFloat(r), startAngle: a0, endAngle: a1, clockwise: false)
        ctx.addPath(p); ctx.strokePath()
    }
}
ctx.restoreGState()

let final = ctx.makeImage()!
let rep = NSBitmapImageRep(cgImage: final)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: args[1]))
print("wrote \(args[1])")
