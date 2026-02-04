// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

//
//  BeebiumTouchBarManager.swift
//  Beebium
//
//  Core TouchBar infrastructure and management
//

import Cocoa
import Foundation

// MARK: - Protocols

// Protocol for TouchBar layout elements that have width in key units
protocol BeebiumTouchBarLayoutElement {
    var widthInKeyUnits: CGFloat { get }
}

/// Protocol for TouchBar item creation responsibility
@MainActor
protocol BeebiumTouchBarItemProvider {
    func createTouchBarItem() -> NSCustomTouchBarItem
}

/// Protocol for state management responsibility
@MainActor
protocol BeebiumTouchBarStateManager {
    func updateState()
}

/// Combined protocol for full TouchBar components
@MainActor
protocol BeebiumTouchBarComponent: BeebiumTouchBarItemProvider, BeebiumTouchBarStateManager {
    var componentName: String { get }
}

// MARK: - Diagnostic Infrastructure

// Custom view class that tracks layout changes for diagnosis
class BeebiumDiagnosticView: NSView {
    var itemName: String = ""
    var intendedWidth: CGFloat = 0
    var minimumWidth: CGFloat = 0
    var actualWidth: CGFloat = 0  // Store the measured width for drawing
    weak var widthLabel: NSTextField?  // Reference to label showing actual width

    init(name: String, minimumWidth: CGFloat = 0, contentHuggingPriority: Float? = nil, compressionResistancePriority: Float? = nil) {
        self.itemName = name
        self.minimumWidth = minimumWidth
        super.init(frame: NSRect(x: 0, y: 0, width: intendedWidth, height: 30))

        // Add minimum width constraint if specified
        if minimumWidth > 0 {
            self.translatesAutoresizingMaskIntoConstraints = false
            let minWidthConstraint = self.widthAnchor.constraint(greaterThanOrEqualToConstant: minimumWidth)
            minWidthConstraint.priority = NSLayoutConstraint.Priority(1000) // Required
            minWidthConstraint.isActive = true
        }

        // Set content hugging priority if specified
        if let priority = contentHuggingPriority {
            self.setContentHuggingPriority(NSLayoutConstraint.Priority(priority), for: .horizontal)
        }

        // Set compression resistance priority if specified
        if let priority = compressionResistancePriority {
            self.setContentCompressionResistancePriority(NSLayoutConstraint.Priority(priority), for: .horizontal)
        }
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    override func layout() {
        super.layout()

        let measuredWidth = self.frame.width

        // Store the measured width for drawing
        self.actualWidth = measuredWidth

        // Update the label with actual measured width if available
        if let label = widthLabel {
            label.stringValue = "\(Int(measuredWidth)) lpx"
        }

        // Trigger redraw with new width
        setNeedsDisplay(bounds)
    }

    override func viewDidMoveToSuperview() {
        super.viewDidMoveToSuperview()
    }
}

// MARK: - Main TouchBar Manager

@available(macOS 10.12.2, *)
@MainActor
class BeebiumTouchBarManager: NSObject {

    // MARK: - Properties

    private var touchBar: NSTouchBar?

    // SVG renderer with injected TouchBar glyph resource
    internal let svgRenderer = BeebiumSvgGlyphRenderer(svgResourceName: "TouchBarGlyphs")

    // LED component for TouchBar status indicators
    internal let leds: BeebiumTouchBarLEDs

    // Keys component for TouchBar key handling
    internal let keys: BeebiumTouchBarKeys

    // Escape key component for TouchBar escape key replacement
    internal let escapeKey: BeebiumTouchBarEscapeKey

    // TouchBar identifiers
    private static let touchBarIdentifier = NSTouchBar.CustomizationIdentifier("com.beebium.touchbar")

    // Grouped layout identifiers for Control Strip positioning strategy
    internal static let keyRegionIdentifier = NSTouchBarItem.Identifier("com.beebium.touchbar.keyregion")
    internal static let spacerIdentifier = NSTouchBarItem.Identifier("com.beebium.touchbar.spacer")
    internal static let controlStripRegionIdentifier = NSTouchBarItem.Identifier("com.beebium.touchbar.controlstripregion")

    // LED region identifier
    internal static let ledRegionIdentifier = NSTouchBarItem.Identifier("com.beebium.touchbar.ledregion")

    // Escape key replacement identifier (for Gen 1 Touch Bar with virtual Escape)
    internal static let escapeKeyIdentifier = NSTouchBarItem.Identifier("com.beebium.touchbar.escape")

    // TouchBar identifiers for customization (only group identifiers, no individual keys)
    private static let allTouchBarIdentifiers: [NSTouchBarItem.Identifier] =
        [ledRegionIdentifier, keyRegionIdentifier]

    // MARK: - Initialization

    init(keyboardClient: KeyboardClient?, indicatorClient: IndicatorClient?, bbcKeyCache: BBCKeyCache?) {
        // Initialize components with dependencies and identifiers
        self.leds = BeebiumTouchBarLEDs(
            identifier: BeebiumTouchBarManager.ledRegionIdentifier,
            indicatorClient: indicatorClient
        )
        self.keys = BeebiumTouchBarKeys(
            svgRenderer: svgRenderer,
            keyboardClient: keyboardClient,
            bbcKeyCache: bbcKeyCache,
            identifier: BeebiumTouchBarManager.keyRegionIdentifier
        )
        self.escapeKey = BeebiumTouchBarEscapeKey(
            identifier: BeebiumTouchBarManager.escapeKeyIdentifier,
            keyboardClient: keyboardClient,
            bbcKeyCache: bbcKeyCache
        )

        super.init()

        setupTouchBar()
    }

    // MARK: - TouchBar Setup

    private func setupTouchBar() {
        touchBar = NSTouchBar()
        touchBar?.delegate = self
        touchBar?.customizationIdentifier = BeebiumTouchBarManager.touchBarIdentifier

        // PROPER LAYOUT: Mandatory group + flexibleSpace + optional group
        var layoutIdentifiers: [NSTouchBarItem.Identifier] = []

        // Mandatory group: Key area (minimum 685 lpx, can expand)
        layoutIdentifiers.append(BeebiumTouchBarManager.keyRegionIdentifier)

        // Flexible space pushes LED area to far right
        layoutIdentifiers.append(.flexibleSpace)

        // Optional group: LED area (dynamic width based on content, dropped when space constrained)
        layoutIdentifiers.append(BeebiumTouchBarManager.ledRegionIdentifier)

        touchBar?.defaultItemIdentifiers = layoutIdentifiers
        touchBar?.customizationAllowedItemIdentifiers = BeebiumTouchBarManager.allTouchBarIdentifiers

        // Set escape key replacement (for Gen 1 Touch Bar with virtual Escape key)
        // On Gen 2+ Touch Bars with physical Escape, this has no effect
        touchBar?.escapeKeyReplacementItemIdentifier = BeebiumTouchBarManager.escapeKeyIdentifier
    }

    // MARK: - Public Interface

    func getTouchBar() -> NSTouchBar? {
        return touchBar
    }
}

// MARK: - NSTouchBarDelegate

@available(macOS 10.12.2, *)
extension BeebiumTouchBarManager: NSTouchBarDelegate {

    func touchBar(_ touchBar: NSTouchBar, makeItemForIdentifier identifier: NSTouchBarItem.Identifier) -> NSTouchBarItem? {

        // Check for escape key replacement
        if identifier == BeebiumTouchBarManager.escapeKeyIdentifier {
            return escapeKey.createTouchBarItem()
        }

        // Check for LED region
        if identifier == BeebiumTouchBarManager.ledRegionIdentifier {
            return leds.createTouchBarItem()
        }

        // Check for key region group
        if identifier == BeebiumTouchBarManager.keyRegionIdentifier {
            return keys.createTouchBarItem()
        }

        return nil
    }

    // MARK: - LED Delegation

    /// Update LED states by delegating to LED component
    func updateLEDStates() {
        leds.updateState()
    }

    /// Update key states by delegating to Keys component
    func updateKeyStates() {
        keys.updateState()
    }
}
