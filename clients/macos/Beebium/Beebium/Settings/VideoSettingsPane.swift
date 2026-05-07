// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

import SwiftUI

/// Settings pane for video display defaults applied to new windows.
///
/// Each new emulator window initialises its `VideoSettings` from the keys
/// stored here via `VideoSettings.loadFromUserDefaults()`. Existing windows
/// keep whatever values they currently have - changes here only affect
/// future windows.
struct VideoSettingsPane: View {
    @AppStorage(VideoSettings.defaultStyleIDKey)
    private var defaultStyleID: String = "standard"

    @AppStorage(VideoSettings.defaultPixelShapeKey)
    private var defaultPixelShapeRaw: String = PixelShape.authentic.rawValue

    @AppStorage(VideoSettings.defaultWindowBackgroundKey)
    private var defaultWindowBackgroundHex: String = VideoSettings.defaultWindowBackground.sRGBHex

    /// Adapter binding so the picker can use the typed PixelShape enum even
    /// though @AppStorage holds the raw string.
    private var defaultPixelShape: Binding<PixelShape> {
        Binding(
            get: {
                PixelShape(rawValue: defaultPixelShapeRaw) ?? .authentic
            },
            set: { newValue in
                defaultPixelShapeRaw = newValue.rawValue
            }
        )
    }

    /// Adapter binding so ColorPicker can use a Color even though @AppStorage
    /// holds the sRGB hex string.
    private var defaultWindowBackground: Binding<Color> {
        Binding(
            get: {
                Color(sRGBHex: defaultWindowBackgroundHex)
                    ?? VideoSettings.defaultWindowBackground
            },
            set: { newValue in
                defaultWindowBackgroundHex = newValue.sRGBHex
            }
        )
    }

    var body: some View {
        Form {
            Section {
                Picker("Default style", selection: $defaultStyleID) {
                    Text("Standard").tag("standard")
                    Text("Debug").tag("debug")
                }

                Picker("Default pixels", selection: defaultPixelShape) {
                    ForEach(PixelShape.allCases) { shape in
                        Text(shape.displayName).tag(shape)
                    }
                }

                HStack {
                    Text("Default boxing colour")
                    Spacer()
                    ResettableColorPicker(
                        accessibilityLabel: "Default boxing colour",
                        color: defaultWindowBackground,
                        defaultColor: VideoSettings.defaultWindowBackground
                    )
                }
            } footer: {
                Text("Defaults apply to new emulator windows. Existing "
                     + "windows keep their current settings.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(minWidth: 420, minHeight: 280)
    }
}

#if DEBUG
struct VideoSettingsPane_Previews: PreviewProvider {
    static var previews: some View {
        VideoSettingsPane()
    }
}
#endif
