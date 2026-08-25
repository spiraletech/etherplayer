import SwiftUI
import UIKit

enum EPTheme {
    static let black = Color(red: 0.015, green: 0.015, blue: 0.018)
    static let panel = Color(red: 0.025, green: 0.024, blue: 0.026)
    static let gold = Color(red: 0.96, green: 0.76, blue: 0.20)
    static let muted = Color(red: 0.56, green: 0.52, blue: 0.43)
    static let border = Color(red: 0.30, green: 0.24, blue: 0.10)
}

struct EPPanelModifier: ViewModifier {
    var radius: CGFloat = 24
    func body(content: Content) -> some View {
        content
            .background(EPTheme.panel.opacity(0.94))
            .clipShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: radius, style: .continuous).stroke(EPTheme.border, lineWidth: 1))
    }
}

extension View {
    func epPanel(radius: CGFloat = 24) -> some View { modifier(EPPanelModifier(radius: radius)) }
}

struct FrequencyBars: View {
    let bands: [Float]
    var compact = false

    var body: some View {
        GeometryReader { geo in
            let spacing: CGFloat = compact ? 1 : 1.5
            let count = max(1, bands.count)
            let width = max(1, (geo.size.width - spacing * CGFloat(count - 1)) / CGFloat(count))
            HStack(alignment: .bottom, spacing: spacing) {
                ForEach(Array(bands.enumerated()), id: \.offset) { _, value in
                    RoundedRectangle(cornerRadius: width > 2 ? 1 : 0.4)
                        .fill(EPTheme.gold)
                        .frame(width: width, height: max(2, geo.size.height * CGFloat(0.05 + value * 0.95)))
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
        }
        .accessibilityLabel("Live frequency analyzer")
    }
}

struct TrackArtwork: View {
    let url: URL?
    var size: CGFloat

    var body: some View {
        Group {
            if let url, let image = UIImage(contentsOfFile: url.path) {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .background(Color.white.opacity(0.04))
            } else {
                ZStack {
                    Color.black
                    Text("E")
                        .font(.system(size: size * 0.42, weight: .semibold, design: .rounded))
                        .foregroundStyle(EPTheme.gold)
                }
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: size * 0.09, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: size * 0.09, style: .continuous).stroke(EPTheme.border, lineWidth: 1))
        .shadow(color: EPTheme.gold.opacity(0.10), radius: 16, y: 8)
    }
}

struct GoldCapsuleButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 13, weight: .bold, design: .rounded))
            .foregroundStyle(configuration.isPressed ? .black : .white)
            .padding(.horizontal, 18)
            .padding(.vertical, 12)
            .background(configuration.isPressed ? EPTheme.gold : EPTheme.panel)
            .clipShape(Capsule())
            .overlay(Capsule().stroke(EPTheme.border, lineWidth: 1))
    }
}

func epTime(_ seconds: Double) -> String {
    guard seconds.isFinite, seconds >= 0 else { return "0:00" }
    let total = Int(seconds.rounded(.down))
    return String(format: "%d:%02d", total / 60, total % 60)
}
