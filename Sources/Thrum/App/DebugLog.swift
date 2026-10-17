import Foundation

/// Append-only text logs next to the settings file, capped in size: when a log
/// passes `limit` it is rotated to `<name>.1` and started afresh.
enum FileLog {
    static let limit = 512 * 1024
    static func append(_ line: String, to url: URL, on q: DispatchQueue) {
        q.async {
            let data = line.data(using: .utf8)!
            if let size = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int), size > limit {
                let old = url.appendingPathExtension("1")
                try? FileManager.default.removeItem(at: old)
                try? FileManager.default.moveItem(at: url, to: old)
            }
            if let h = try? FileHandle(forWritingTo: url) { h.seekToEndOfFile(); h.write(data); try? h.close() }
            else { try? data.write(to: url) }
        }
    }
}

/// Diagnostics log for launches where stderr is not visible (Finder / login
/// item): device attach/detach, permission results, the event tap state, action
/// button gestures. No key, menu or text content is written.
enum DebugLog {
    static let url = Settings.url.deletingLastPathComponent().appendingPathComponent("debug.log")
    private static let q = DispatchQueue(label: "com.alcybr.thrum.log")
    static func log(_ s: String) {
        FileLog.append("\(ISO8601DateFormatter().string(from: Date())) \(s)\n", to: url, on: q)
    }
}
