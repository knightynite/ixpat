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
/// master strength. The app decides when; iXpat only plays.
///
/// Threads: `start`/`stop` and `running` on main; everything else on `queue`.
final class HapticBridge {
    var onPlay: ((String, Int) -> Void)?           // (waveform name, intensity) on main
    var waveformsJSON: () -> String = { "[]" }
    var running: Bool { _running.value }
    private let _running = Locked(false)
    private var listener: NWListener?
    private let queue = DispatchQueue(label: "com.alcybr.ixpat.bridge", qos: .utility)
    private let token = Locked("")
    private var port: UInt16 = 0
    private var generation = 0

    // Resource bounds for unauthenticated callers (everything before the token check).
    private static let maxRequestBytes = 65536
    private static let maxBodyBytes = 16384
    private static let maxConnections = 16
    private static let requestDeadline: TimeInterval = 5
    private var open = 0                            // queue only

    private static let aliases: [String: String] = [
        "snap": "align", "guide": "align", "detent": "step", "tick": "step", "boundary": "collide",
        "limit": "collide", "edge": "collide", "done": "success", "complete": "success", "fail": "error",
        "notify": "grow", "progress": "grow", "select": "hover", "focus": "hover", "press": "click",
    ]

    func start(port: UInt16, token: String) {
        stop()
        self.token.value = token; self.port = port
        guard let p = NWEndpoint.Port(rawValue: port) else { return }
        let params = NWParameters.tcp
        params.requiredInterfaceType = .loopback
        params.allowLocalEndpointReuse = true
        guard let l = try? NWListener(using: params, on: p) else { DebugLog.log("bridge: listener failed on \(port)"); return }
        generation += 1
        let gen = generation
        l.newConnectionHandler = { [weak self] c in self?.accept(c, gen) }
        l.stateUpdateHandler = { [weak self] st in
            guard let self, gen == self.generation else { return }
            if case .ready = st { self._running.value = true; DebugLog.log("bridge: listening on 127.0.0.1:\(port)") }
            if case .failed(let e) = st { self._running.value = false; DebugLog.log("bridge: failed \(e)") }
        }
        l.start(queue: queue)
        listener = l
    }

    func stop() {
        generation += 1                              // connections of the old listener are ignored from here on
        listener?.cancel(); listener = nil
        _running.value = false
    }

    private func accept(_ c: NWConnection, _ gen: Int) {
        guard open < Self.maxConnections else { c.cancel(); return }
        open += 1
        var finished = false
        let finish = { [weak self] in
            guard !finished else { return }
            finished = true
            self?.open -= 1
            c.cancel()
        }
        queue.asyncAfter(deadline: .now() + Self.requestDeadline) { finish() }
        c.start(queue: queue)
        read(c, Data(), gen, finish)
    }

    private func read(_ c: NWConnection, _ acc: Data, _ gen: Int, _ finish: @escaping () -> Void) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 16384) { [weak self] data, _, done, err in
            guard let self, gen == self.generation else { finish(); return }
            var buf = acc
            if let data { buf.append(data) }
            if buf.count > Self.maxRequestBytes || err != nil { finish(); return }
            switch Self.parse(buf) {
            case .request(let r):
                let (status, body) = self.route(r)
                self.respond(c, status, body, finish)
            case .invalid:
                self.respond(c, 400, Self.json(["error": "bad request"]), finish)
            case .incomplete:
                if done { finish() } else { self.read(c, buf, gen, finish) }
            }
        }
    }

    private struct Request { var method: String; var path: String; var body: Data }
    private enum Parse { case incomplete, invalid, request(Request) }

    /// Minimal HTTP/1.1: incomplete until headers + Content-Length body have arrived.
    private static func parse(_ d: Data) -> Parse {
        guard let hdrEnd = d.range(of: Data("\r\n\r\n".utf8)) else { return .incomplete }
        guard let head = String(data: d[..<hdrEnd.lowerBound], encoding: .utf8) else { return .invalid }
        let lines = head.split(separator: "\r\n", omittingEmptySubsequences: false)
        let first = lines.first.map { $0.split(separator: " ") } ?? []
        guard first.count >= 2 else { return .invalid }
        var length = 0
        for l in lines.dropFirst() {
            let parts = l.split(separator: ":", maxSplits: 1)
            guard parts.count == 2, parts[0].trimmingCharacters(in: .whitespaces).lowercased() == "content-length" else { continue }
            guard let n = Int(parts[1].trimmingCharacters(in: .whitespaces)), n >= 0, n <= maxBodyBytes else { return .invalid }
            length = n
        }
        let bodyStart = hdrEnd.upperBound
        guard d.count - bodyStart >= length else { return .incomplete }
        return .request(Request(method: String(first[0]), path: String(first[1]), body: d[bodyStart..<(bodyStart + length)]))
    }

    private static func json(_ obj: [String: Any]) -> String {
        (try? JSONSerialization.data(withJSONObject: obj, options: [.sortedKeys])).flatMap { String(data: $0, encoding: .utf8) } ?? "{}"
    }

    private func route(_ r: Request) -> (Int, String) {
        switch (r.method, r.path) {
        case ("OPTIONS", _): return (204, "")
        case ("GET", "/ping"): return (200, Self.json(["ok": true, "app": "iXpat"]))
        case ("GET", "/waveforms"): return (200, waveformsJSON())
        case ("POST", "/haptic"):
            guard let obj = try? JSONSerialization.jsonObject(with: r.body) as? [String: Any] else { return (400, Self.json(["error": "bad json"])) }
            let expected = token.value
            guard expected.isEmpty || (obj["token"] as? String) == expected else { return (401, Self.json(["error": "bad token"])) }
            guard var name = (obj["event"] as? String)?.lowercased(), name.count <= 32,
                  name.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "_" || $0 == "-" }) else { return (400, Self.json(["error": "missing or bad event"])) }
            name = Self.aliases[name] ?? name
            let intensity = max(1, min(4, (obj["intensity"] as? Int) ?? 3))
            DispatchQueue.main.async { [weak self] in self?.onPlay?(name, intensity) }
            return (200, Self.json(["ok": true, "event": name, "intensity": intensity]))
        default: return (404, Self.json(["error": "not found"]))
        }
    }

    private func respond(_ c: NWConnection, _ status: Int, _ body: String, _ finish: @escaping () -> Void) {
        let reason = [200: "OK", 204: "No Content", 400: "Bad Request", 401: "Unauthorized", 404: "Not Found"][status] ?? "OK"
        let head = "HTTP/1.1 \(status) \(reason)\r\nContent-Type: application/json\r\nContent-Length: \(body.utf8.count)\r\n"
            + "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: content-type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nConnection: close\r\n\r\n"
        c.send(content: Data((head + body).utf8), completion: .contentProcessed { _ in finish() })
    }
}
