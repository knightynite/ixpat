import AppKit
import Security

/// Checks GitHub for a newer release and offers it. A downloaded DMG is opened
/// only when it carries a valid signature from the release Team ID, so a
/// tampered release or a redirected download fails closed.
final class Updater {
    static let shared = Updater()
    static let repo = "knightynite/Thrum"
    /// Team ID on every iXpat release signature. A fork that signs with its own
    /// Developer ID changes this constant and ships its own releases.
    static let releaseTeamID = "D76ZAFG74A"

    let currentVersion = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.0.0"
    private let lastCheckKey = "lastUpdateCheck", skippedKey = "skippedVersion"
    private var busy = false   // main thread only
    private var timer: Timer?

    /// Background checks: one shortly after launch, then every 6 h while the app
    /// runs (menu-bar apps stay up for weeks). The network is hit at most once a day.
    func start(enabled: @escaping () -> Bool) {
        let tick = { [weak self] in if enabled() { self?.check(manual: false) } }
        DispatchQueue.main.asyncAfter(deadline: .now() + 10, execute: tick)
        timer = Timer.scheduledTimer(withTimeInterval: 6 * 3600, repeats: true) { _ in tick() }
    }

    /// `manual` (About window) always queries and reports "up to date" and errors.
    func check(manual: Bool) {
        if !manual, Date().timeIntervalSince1970 - UserDefaults.standard.double(forKey: lastCheckKey) < 24 * 3600 { return }
        guard !busy else { return }
        busy = true
        var req = URLRequest(url: URL(string: "https://api.github.com/repos/\(Self.repo)/releases/latest")!)
        req.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        req.timeoutInterval = 15
        URLSession.shared.dataTask(with: req) { data, resp, err in
            let result: Result<Release, Error>
            if let err { result = .failure(err) }
            else if let code = (resp as? HTTPURLResponse)?.statusCode, code != 200 {
                result = .failure(UpdateError(message: code == 404 ? "No release has been published yet." : "GitHub answered HTTP \(code)."))
            } else { result = Result { try JSONDecoder().decode(Release.self, from: data ?? Data()) } }
            DispatchQueue.main.async { self.busy = false; self.handle(result, manual: manual) }
        }.resume()
    }

    private func handle(_ result: Result<Release, Error>, manual: Bool) {
        switch result {
        case .failure(let e):
            DebugLog.log("update check failed: \(e.localizedDescription)")
            if manual { alert("Update check failed", e.localizedDescription, style: .warning) }
        case .success(let r):
            UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: lastCheckKey)
            let latest = r.tagName.hasPrefix("v") ? String(r.tagName.dropFirst()) : r.tagName
            DebugLog.log("update check: latest \(latest), running \(currentVersion)")
            guard Self.isNewer(latest, than: currentVersion) else {
                if manual { alert("iXpat is up to date", "Version \(currentVersion) is the latest release.") }
                return
            }
            if !manual, UserDefaults.standard.string(forKey: skippedKey) == latest { return }
            offer(r, latest)
        }
    }

    private func offer(_ r: Release, _ latest: String) {
        let a = NSAlert()
        a.messageText = "iXpat \(latest) is available"
        a.informativeText = "You have version \(currentVersion). Download the update now?"
        a.addButton(withTitle: "Download"); a.addButton(withTitle: "Later"); a.addButton(withTitle: "Skip This Version")
        NSApp.activate(ignoringOtherApps: true)
        switch a.runModal() {
        case .alertFirstButtonReturn: download(r)
        case .alertThirdButtonReturn: UserDefaults.standard.set(latest, forKey: skippedKey)
        default: break
        }
    }

    private func download(_ r: Release) {
        guard let asset = r.assets.first(where: { $0.name.hasSuffix(".dmg") }) else { openReleasePage(r); return }
        // The release JSON came off the network, so its asset URL is not trusted on its own.
        guard let url = URL(string: asset.browserDownloadUrl), url.scheme == "https", url.host == "github.com" else {
            DebugLog.log("update rejected: asset URL is not github.com over HTTPS: \(asset.browserDownloadUrl)")
            alert("Update not downloaded", "The download link does not point at GitHub.", style: .warning)
            return
        }
        busy = true
        URLSession.shared.downloadTask(with: url) { tmp, _, err in
            // The temporary file is deleted when this handler returns, so move it here, not on main.
            var dest: URL?, problem: String?
            if let err { problem = err.localizedDescription }
            else if let tmp {
                let d = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask)[0]
                    .appendingPathComponent((asset.name as NSString).lastPathComponent)
                do {
                    try? FileManager.default.removeItem(at: d)
                    try FileManager.default.moveItem(at: tmp, to: d)
                    dest = d
                } catch { problem = error.localizedDescription }
            }
            DispatchQueue.main.async {
                self.busy = false
                guard let dest else {
                    self.alert("Download failed", problem ?? "No file was received.", style: .warning); return
                }
                if let bad = Self.signatureProblem(at: dest) {
                    DebugLog.log("update rejected: \(bad)")
                    try? FileManager.default.removeItem(at: dest)
                    self.alert("Update not opened", "The downloaded file is not signed by the iXpat release certificate (\(bad)). It was deleted. Download the update from the releases page instead.", style: .warning)
                    self.openReleasePage(r)
                    return
                }
                NSWorkspace.shared.open(dest)
                self.alert("Update downloaded", "Quit iXpat, drag the new iXpat to Applications to replace the old one, then open it again.")
            }
        }.resume()
    }

    private func openReleasePage(_ r: Release) {
        if let u = URL(string: r.htmlUrl), u.scheme == "https", u.host == "github.com" { NSWorkspace.shared.open(u) }
    }

    private func alert(_ title: String, _ text: String, style: NSAlert.Style = .informational) {
        let a = NSAlert()
        a.messageText = title; a.informativeText = text; a.alertStyle = style
        NSApp.activate(ignoringOtherApps: true)
        a.runModal()
    }

    static func isNewer(_ new: String, than current: String) -> Bool {
        let n = new.split(separator: ".").map { Int($0) ?? 0 }, c = current.split(separator: ".").map { Int($0) ?? 0 }
        for i in 0..<max(n.count, c.count) {
            let a = i < n.count ? n[i] : 0, b = i < c.count ? c[i] : 0
            if a != b { return a > b }
        }
        return false
    }

    /// nil when the file carries a valid Developer ID signature from `releaseTeamID`
    /// (build.sh dist signs the DMG itself), otherwise a short reason.
    static func signatureProblem(at url: URL) -> String? {
        var code: SecStaticCode?
        let created = SecStaticCodeCreateWithPath(url as CFURL, [], &code)
        guard created == errSecSuccess, let code else { return "no code signature, status \(created)" }
        var req: SecRequirement?
        let text = "anchor apple generic and certificate leaf[subject.OU] = \"\(releaseTeamID)\"" as CFString
        guard SecRequirementCreateWithString(text, [], &req) == errSecSuccess, let req else { return "could not build the signature requirement" }
        let checked = SecStaticCodeCheckValidity(code, SecCSFlags(rawValue: kSecCSStrictValidate), req)
        return checked == errSecSuccess ? nil : "signature check failed, status \(checked)"
    }

    struct UpdateError: LocalizedError { let message: String; var errorDescription: String? { message } }

    struct Release: Decodable {
        let tagName: String, htmlUrl: String, assets: [Asset]
        enum CodingKeys: String, CodingKey { case tagName = "tag_name", htmlUrl = "html_url", assets }
    }
    struct Asset: Decodable {
        let name: String, browserDownloadUrl: String
        enum CodingKeys: String, CodingKey { case name, browserDownloadUrl = "browser_download_url" }
    }
}
