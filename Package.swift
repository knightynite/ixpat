// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "iXpat",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "iXpat",
            path: "Sources/iXpat",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
