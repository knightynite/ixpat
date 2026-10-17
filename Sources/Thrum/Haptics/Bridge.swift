import Foundation
import Network

/// A tiny local HTTP endpoint so app plugins (Photoshop UXP, VS Code, scripts)
/// can ask for a pulse. Loopback only; a per-install token is required.
///
///   POST http://127.0.0.1:47337/haptic   {"token":"…","event":"align","intensity":3}
///   GET  http://127.0.0.1:47337/waveforms  → the device's table
///
/// `event` is a waveform name (hover, collide, align, step, grow, success, error,
/// click) or one of the semantic aliases below. Intensity 1…4, scaled by the
/// master strength. The app decides when; Thrum only plays.
final class HapticBridge {
    var onPlay: ((String, Int) -> Void)?           // (waveform name, intensity) on main
    var waveformsJSON: () -> String = { "[]" }
    private(set) var running = false
    private var listener: NWListener?
    private let queue = DispatchQueue(label: "com.alcybr.thrum.bridge", qos: .utility)
    private var token = ""
    private var port: UInt16 = 0

    private static let aliases: [String: String] = [
        "snap": "align", "guide": "align", "detent": "step", "tick": "step", "boundary": "collide",
        "limit": "collide", "edge": "collide", "done": "success", "complete": "success", "fail": "error",
        "notify": "grow", "progress": "grow", "select": "hover", "focus": "hover", "press": "click",
    ]

    func start(port: UInt16, token: String) {
        stop()
        self.token = token; self.port = port
        guard let p = NWEndpoint.Port(rawValue: port) else { return }
        let params = NWParameters.tcp
        params.requiredInterfaceType = .loopback
        params.allowLocalEndpointReuse = true
        guard let l = try? NWListener(using: params, on: p) else { DebugLog.log("bridge: listener failed on \(port)"); return }
        l.newConnectionHandler = { [weak self] c in self?.accept(c) }
        l.stateUpdateHandler = { [weak self] st in
            if case .ready = st { self?.running = true; DebugLog.log("bridge: listening on 127.0.0.1:\(port)") }
            if case .failed(let e) = st { self?.running = false; DebugLog.log("bridge: failed \(e)") }
        }
        l.start(queue: queue)
        listener = l
    }

    func stop() { listener?.cancel(); listener = nil; running = false }

    private func accept(_ c: NWConnection) {
        c.start(queue: queue)
        read(c, Data())
    }

    private func read(_ c: NWConnection, _ acc: Data) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 16384) { [weak self] data, _, done, err in
            guard let self else { return }
            var buf = acc
            if let data { buf.append(data) }
            if buf.count > 65536 || err != nil { c.cancel(); return }
            if let r = Self.parse(buf) {
                let (status, body) = self.route(r)
                self.respond(c, status, body)
            } else if done { c.cancel() } else { self.read(c, buf) }
        }
    }

    private struct Request { var method: String; var path: String; var body: Data }

    /// Minimal HTTP/1.1: returns nil until the full request (headers + Content-Length body) has arrived.
    private static func parse(_ d: Data) -> Request? {
        guard let hdrEnd = d.range(of: Data("\r\n\r\n".utf8)) else { return nil }
        guard let head = String(data: d[..<hdrEnd.lowerBound], encoding: .utf8) else { return nil }
        let lines = head.split(separator: "\r\n", omittingEmptySubsequences: false)
        let first = lines.first.map { $0.split(separator: " ") } ?? []
        guard first.count >= 2 else { return nil }
        var length = 0
        for l in lines.dropFirst() {
            let parts = l.split(separator: ":", maxSplits: 1)
            if parts.count == 2, parts[0].trimmingCharacters(in: .whitespaces).lowercased() == "content-length" {
                length = Int(parts[1].trimmingCharacters(in: .whitespaces)) ?? 0
            }
        }
        let bodyStart = hdrEnd.upperBound
        guard d.count - bodyStart >= length else { return nil }
        return Request(method: String(first[0]), path: String(first[1]), body: d[bodyStart..<(bodyStart + length)])
    }

    private func route(_ r: Request) -> (Int, String) {
        switch (r.method, r.path) {
        case ("OPTIONS", _): return (204, "")
        case ("GET", "/ping"): return (200, #"{"ok":true,"app":"Thrum"}"#)
        case ("GET", "/waveforms"): return (200, waveformsJSON())
        case ("POST", "/haptic"):
            guard let obj = try? JSONSerialization.jsonObject(with: r.body) as? [String: Any] else { return (400, #"{"error":"bad json"}"#) }
            guard token.isEmpty || (obj["token"] as? String) == token else { return (401, #"{"error":"bad token"}"#) }
            guard var name = (obj["event"] as? String)?.lowercased() else { return (400, #"{"error":"missing event"}"#) }
            name = Self.aliases[name] ?? name
            let intensity = max(1, min(4, (obj["intensity"] as? Int) ?? 3))
            DispatchQueue.main.async { [weak self] in self?.onPlay?(name, intensity) }
            return (200, #"{"ok":true,"event":"\#(name)","intensity":\#(intensity)}"#)
        default: return (404, #"{"error":"not found"}"#)
        }
    }

    private func respond(_ c: NWConnection, _ status: Int, _ body: String) {
        let reason = [200: "OK", 204: "No Content", 400: "Bad Request", 401: "Unauthorized", 404: "Not Found"][status] ?? "OK"
        let head = "HTTP/1.1 \(status) \(reason)\r\nContent-Type: application/json\r\nContent-Length: \(body.utf8.count)\r\n"
            + "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: content-type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nConnection: close\r\n\r\n"
        c.send(content: Data((head + body).utf8), completion: .contentProcessed { _ in c.cancel() })
    }
}
