// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Thrum",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "Thrum",
            path: "Sources/Thrum",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
