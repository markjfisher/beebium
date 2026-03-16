/**
 * Keyboard input interface for the Beebium client.
 */

import { promisify } from "./call-utils.js";
import {
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    charToMatrix,
} from "./keyboard-map.js";
import type {
    AllKeyMappingsResponse,
    BreakDownResponse,
    BreakKeyState,
    BreakUpResponse,
    ClearTypingResponse,
    KeyMappingEntry,
    KeyResponse,
    KeyboardServiceClient,
    KeyboardState as KeyboardStateProto,
    LinksState,
    SetLinksResponse,
    SetStartupAutoBootResponse,
    SetStartupScreenModeResponse,
    StartupAutoBootState,
    StartupScreenModeState,
    TypeQuicklyResponse,
    TypingStatus,
} from "./generated/keyboard.js";

export {
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    charToMatrix,
    matrixToChar,
} from "./keyboard-map.js";

/**
 * Current state of the keyboard matrix.
 */
export interface KeyboardState {
    /** Bitmap of pressed keys (10 rows). */
    pressedRows: number[];

    /** Check if a specific key is pressed. */
    isPressed(row: number, column: number): boolean;
}

function createKeyboardState(pressedRows: number[]): KeyboardState {
    return {
        pressedRows,
        isPressed(row: number, column: number): boolean {
            if (row >= 0 && row < pressedRows.length) {
                const rowValue = pressedRows[row];
                return rowValue !== undefined && (rowValue & (1 << column)) !== 0;
            }
            return false;
        },
    };
}

/**
 * Keyboard input interface.
 *
 * Provides both low-level matrix access and high-level text input.
 *
 * Usage:
 *     // Type text (cycle-paced, use \r for RETURN)
 *     await keyboard.type("PRINT 42\r");
 *
 *     // Press specific keys
 *     await keyboard.keyDown('A');
 *     await keyboard.keyUp('A');
 *
 *     // Matrix-level access
 *     await keyboard.matrixDown(4, 1);  // 'A' key
 */
export class Keyboard {
    private readonly _stub: KeyboardServiceClient;
    private readonly _pressedKeys = new Set<string>();

    constructor(stub: KeyboardServiceClient) {
        this._stub = stub;
    }

    // =========================================================================
    // High-level text input (cycle-paced via server-side TypeAheadQueue)
    // =========================================================================

    /**
     * Type a string of text, paced in emulator cycles.
     *
     * The text is enqueued and typed character-by-character as the
     * emulator runs. Use `\r` for RETURN, `\x1b` for ESCAPE,
     * `\x7f` for DELETE, and `\t` for TAB.
     *
     * Returns immediately; the emulator must be running for the
     * keystrokes to be processed.
     *
     * @param text - The text to type.
     * @param cyclesPerKey - CPU cycles per keystroke (0 = use server default).
     * @returns Total pending characters in queue after enqueue.
     * @throws Error if text contains unmappable characters.
     */
    async type(text: string, cyclesPerKey: number = 0): Promise<number> {
        const response = await promisify<
            { text: string; cyclesPerKey: number },
            TypeQuicklyResponse
        >(this._stub as unknown as Record<string, Function>, "typeQuickly", {
            text,
            cyclesPerKey,
        });
        if (!response.accepted) {
            throw new Error(`Text rejected: ${response.error}`);
        }
        return response.pendingCharacters;
    }

    /** Enqueue a RETURN keypress. */
    async pressReturn(): Promise<void> {
        await this.type("\r");
    }

    /** Enqueue an ESCAPE keypress. */
    async pressEscape(): Promise<void> {
        await this.type("\x1b");
    }

    /** Enqueue a DELETE keypress. */
    async pressDelete(): Promise<void> {
        await this.type("\x7f");
    }

    /** Enqueue a SPACE keypress. */
    async pressSpace(): Promise<void> {
        await this.type(" ");
    }

    // =========================================================================
    // Character-level input
    // =========================================================================

    /**
     * Press a key for the given character.
     *
     * Automatically handles SHIFT for uppercase and shifted symbols.
     *
     * @param char - The character to press.
     * @returns True if the character is mapped, false otherwise.
     */
    async keyDown(char: string): Promise<boolean> {
        const mapping = charToMatrix(char);
        if (mapping === undefined) {
            return false;
        }

        const [row, column, needsShift] = mapping;

        if (needsShift) {
            await this.matrixDown(...SHIFT_KEY);
        }
        await this.matrixDown(row, column);

        this._pressedKeys.add(`${char}:${needsShift}`);
        return true;
    }

    /**
     * Release a key for the given character.
     *
     * @param char - The character to release.
     * @returns True if the character is mapped, false otherwise.
     */
    async keyUp(char: string): Promise<boolean> {
        const mapping = charToMatrix(char);
        if (mapping === undefined) {
            return false;
        }

        const [row, column, needsShift] = mapping;

        await this.matrixUp(row, column);

        // Only release shift if no other shifted keys are pressed
        if (needsShift) {
            this._pressedKeys.delete(`${char}:true`);
            let anyShifted = false;
            for (const key of this._pressedKeys) {
                if (key.endsWith(":true")) {
                    anyShifted = true;
                    break;
                }
            }
            if (!anyShifted) {
                await this.matrixUp(...SHIFT_KEY);
            }
        } else {
            this._pressedKeys.delete(`${char}:false`);
        }

        return true;
    }

    /** Release all currently pressed keys. */
    async releaseAll(): Promise<void> {
        const keys = [...this._pressedKeys];
        for (const key of keys) {
            const char = key.substring(0, key.lastIndexOf(":"));
            await this.keyUp(char);
        }
    }

    // =========================================================================
    // Modifier keys
    // =========================================================================

    /** Press the SHIFT key. */
    async shiftDown(): Promise<void> {
        await this.matrixDown(...SHIFT_KEY);
    }

    /** Release the SHIFT key. */
    async shiftUp(): Promise<void> {
        await this.matrixUp(...SHIFT_KEY);
    }

    /** Press the CTRL key. */
    async ctrlDown(): Promise<void> {
        await this.matrixDown(...CTRL_KEY);
    }

    /** Release the CTRL key. */
    async ctrlUp(): Promise<void> {
        await this.matrixUp(...CTRL_KEY);
    }

    // =========================================================================
    // Matrix-level input
    // =========================================================================

    /**
     * Press a key by BBC keyboard matrix position.
     *
     * @param row - The keyboard matrix row (0-7).
     * @param column - The keyboard matrix column (0-9).
     * @returns True if accepted by server.
     */
    async matrixDown(row: number, column: number): Promise<boolean> {
        const response = await promisify<{ ikNumber: number }, KeyResponse>(
            this._stub as unknown as Record<string, Function>,
            "keyDown",
            { ikNumber: (row << 4) | column },
        );
        return response.accepted;
    }

    /**
     * Release a key by BBC keyboard matrix position.
     *
     * @param row - The keyboard matrix row (0-7).
     * @param column - The keyboard matrix column (0-9).
     * @returns True if accepted by server.
     */
    async matrixUp(row: number, column: number): Promise<boolean> {
        const response = await promisify<{ ikNumber: number }, KeyResponse>(
            this._stub as unknown as Record<string, Function>,
            "keyUp",
            { ikNumber: (row << 4) | column },
        );
        return response.accepted;
    }

    /**
     * Get current keyboard state (pressed keys bitmap).
     *
     * @returns The current keyboard state.
     */
    async getState(): Promise<KeyboardState> {
        const response = await promisify<Record<string, never>, KeyboardStateProto>(
            this._stub as unknown as Record<string, Function>,
            "getState",
            {},
        );
        return createKeyboardState([...response.pressedRows]);
    }

    // =========================================================================
    // Type-ahead status
    // =========================================================================

    /**
     * Get type-ahead queue status.
     *
     * @returns Object with idle, pendingCharacters, and stringsQueued.
     */
    async typingStatus(): Promise<{ idle: boolean; pendingCharacters: number; stringsQueued: number }> {
        const response = await promisify<Record<string, never>, TypingStatus>(
            this._stub as unknown as Record<string, Function>,
            "getTypingStatus",
            {},
        );
        return {
            idle: response.idle,
            pendingCharacters: response.pendingCharacters,
            stringsQueued: response.stringsQueued,
        };
    }

    /**
     * Clear the type-ahead queue.
     *
     * @returns Number of characters that were cleared.
     */
    async clearTyping(): Promise<number> {
        const response = await promisify<Record<string, never>, ClearTypingResponse>(
            this._stub as unknown as Record<string, Function>,
            "clearTyping",
            {},
        );
        return response.charactersCleared;
    }

    /**
     * Wait until the type-ahead queue is empty.
     *
     * @param pollIntervalMs - Milliseconds between status checks (default 10).
     */
    async waitForTyping(pollIntervalMs: number = 10): Promise<void> {
        while (true) {
            const status = await this.typingStatus();
            if (status.idle) {
                return;
            }
            await new Promise(r => setTimeout(r, pollIntervalMs));
        }
    }

    // =========================================================================
    // Character-to-key mapping (server-side canonical mapping)
    // =========================================================================

    /**
     * Get the BBC key mapping for a character from the server.
     *
     * This queries the canonical mapping table maintained by the server.
     *
     * @param char - A single character.
     * @returns Object with ikNumber, needsShift, and name, or undefined if unmappable.
     */
    async getKeyMapping(char: string): Promise<{ ikNumber: number; needsShift: boolean; name: string } | undefined> {
        const response = await promisify<{ character: string }, KeyMappingEntry>(
            this._stub as unknown as Record<string, Function>,
            "getKeyMapping",
            { character: char },
        );
        if (!response.found) {
            return undefined;
        }
        return {
            ikNumber: response.ikNumber,
            needsShift: response.needsShift,
            name: response.name,
        };
    }

    /**
     * Get the complete key mapping table from the server.
     *
     * @returns Array of mapping entries with character, ikNumber, needsShift, and name.
     *          Character is empty string for named-only keys (function keys, etc.).
     */
    async getAllMappings(): Promise<Array<{ character: string; ikNumber: number; needsShift: boolean; name: string }>> {
        const response = await promisify<Record<string, never>, AllKeyMappingsResponse>(
            this._stub as unknown as Record<string, Function>,
            "getAllKeyMappings",
            {},
        );
        return response.mappings.map(entry => ({
            character: entry.character,
            ikNumber: entry.ikNumber,
            needsShift: entry.needsShift,
            name: entry.name,
        }));
    }

    /**
     * Check if all characters in text can be typed (using server mapping).
     *
     * @param text - The text to check.
     * @returns True if all characters are typeable according to the server.
     */
    async isTypeableOnServer(text: string): Promise<boolean> {
        for (const char of text) {
            const mapping = await this.getKeyMapping(char);
            if (mapping === undefined) {
                return false;
            }
        }
        return true;
    }

    // =========================================================================
    // Break key operations
    // =========================================================================
    // The Break key is NOT part of the keyboard matrix. It is directly
    // connected to the reset circuit (IC16 NE555 timer) via pin 4.
    //
    // While Break is held: CPU is halted (reset line held low)
    // On Break release: Soft reset sequence begins
    //
    // Note: Hardware always does a soft reset. MOS checks if Ctrl is held
    // during its reset sequence to decide between warm/cold reset behavior.

    /**
     * Hold the Break key (halt the CPU).
     *
     * This asserts the reset line, halting the CPU. The CPU remains
     * halted until breakUp() is called.
     *
     * @returns True if successful.
     */
    async breakDown(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakDownResponse>(
            this._stub as unknown as Record<string, Function>,
            "breakDown",
            {},
        );
        return response.success;
    }

    /**
     * Release the Break key (begin soft reset sequence).
     *
     * This releases the reset line. The CPU will execute the 7-cycle
     * reset sequence and then begin execution from the reset vector.
     *
     * MOS checks if Ctrl is held at this moment to determine reset type:
     * - If Ctrl pressed: MOS performs "hard reset" (clears VIA config)
     * - If Ctrl not pressed: MOS performs "warm reset" (preserves state)
     *
     * @returns True if successful.
     */
    async breakUp(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakUpResponse>(
            this._stub as unknown as Record<string, Function>,
            "breakUp",
            {},
        );
        return response.success;
    }

    /**
     * Check if the Break key is currently held.
     *
     * @returns True if Break is currently held (CPU halted).
     */
    async isBreakHeld(): Promise<boolean> {
        const response = await promisify<Record<string, never>, BreakKeyState>(
            this._stub as unknown as Record<string, Function>,
            "getBreakState",
            {},
        );
        return response.isHeld;
    }

    /**
     * Press and release the Break key (perform soft reset).
     *
     * This is a convenience method that calls breakDown(), waits
     * briefly, then calls breakUp().
     *
     * @param holdTimeMs - How long to hold Break in milliseconds (default 20).
     * @returns True if both operations succeeded.
     */
    async pressBreak(holdTimeMs: number = 20): Promise<boolean> {
        const downOk = await this.breakDown();
        await new Promise(r => setTimeout(r, holdTimeMs));
        const upOk = await this.breakUp();
        return downOk && upOk;
    }

    /**
     * Press Ctrl-Break (trigger MOS hard reset).
     *
     * This holds Ctrl while pressing Break, which causes MOS to
     * detect a "hard reset" and perform full reinitialization.
     *
     * Note: The hardware always performs a soft reset. MOS checks
     * the keyboard matrix during its reset routine and clears the
     * VIA configuration if Ctrl is held, simulating a hard reset.
     *
     * @param holdTimeMs - How long to hold Break in milliseconds (default 20).
     * @returns True if all operations succeeded.
     */
    async ctrlBreak(holdTimeMs: number = 20): Promise<boolean> {
        await this.ctrlDown();
        await new Promise(r => setTimeout(r, 10)); // Brief delay to ensure Ctrl is registered
        const downOk = await this.breakDown();
        await new Promise(r => setTimeout(r, holdTimeMs));
        const upOk = await this.breakUp();
        await new Promise(r => setTimeout(r, 10)); // Brief delay before releasing Ctrl
        await this.ctrlUp();
        return downOk && upOk;
    }

    // =========================================================================
    // Keyboard links (DIP switches)
    // =========================================================================
    // The BBC Micro has 8 keyboard links (row 0, columns 2-9) that form a
    // startup options byte. Active-low: bit SET = link broken (open),
    // bit CLEAR = link made.
    //
    // Bit layout:
    //   Bits 0-2: Screen mode (XOR'd with 7 by MOS, so 0x07 = Mode 7)
    //   Bit 3: SHIFT-BREAK action (1 = normal, 0 = reversed/auto-boot)
    //   Bits 4-7: ROM-dependent (disc timing, filing system, etc.)
    //
    // Default value 0xFF (all links broken) = Mode 7, normal SHIFT-BREAK.

    /**
     * Get the raw keyboard links byte (8 bits).
     *
     * @returns The current 8-bit links value (0-255).
     */
    async getLinks(): Promise<number> {
        const response = await promisify<Record<string, never>, LinksState>(
            this._stub as unknown as Record<string, Function>,
            "getLinks",
            {},
        );
        return response.value;
    }

    /**
     * Set the raw keyboard links byte (8 bits).
     *
     * @param value - The 8-bit links value (0-255).
     * @returns True if successful.
     * @throws Error if value is not in range 0-255 or server rejects.
     */
    async setLinks(value: number): Promise<boolean> {
        if (value < 0 || value > 255) {
            throw new Error("Links value must be 0-255");
        }
        const response = await promisify<{ value: number }, SetLinksResponse>(
            this._stub as unknown as Record<string, Function>,
            "setLinks",
            { value },
        );
        if (!response.success) {
            throw new Error(response.error);
        }
        return true;
    }

    /**
     * Get the startup screen mode (0-7).
     *
     * @returns The configured startup screen mode.
     */
    async getStartupScreenMode(): Promise<number> {
        const response = await promisify<Record<string, never>, StartupScreenModeState>(
            this._stub as unknown as Record<string, Function>,
            "getStartupScreenMode",
            {},
        );
        return response.mode;
    }

    /**
     * Set the startup screen mode (0-7).
     *
     * @param mode - The screen mode (0-7).
     * @returns True if successful.
     * @throws Error if mode is not in range 0-7 or server rejects.
     */
    async setStartupScreenMode(mode: number): Promise<boolean> {
        if (mode < 0 || mode > 7) {
            throw new Error("Mode must be 0-7");
        }
        const response = await promisify<{ mode: number }, SetStartupScreenModeResponse>(
            this._stub as unknown as Record<string, Function>,
            "setStartupScreenMode",
            { mode },
        );
        if (!response.success) {
            throw new Error(response.error);
        }
        return true;
    }

    /**
     * Check if auto-boot on SHIFT-BREAK is enabled.
     *
     * @returns True if SHIFT-BREAK triggers auto-boot.
     */
    async getStartupAutoBoot(): Promise<boolean> {
        const response = await promisify<Record<string, never>, StartupAutoBootState>(
            this._stub as unknown as Record<string, Function>,
            "getStartupAutoBoot",
            {},
        );
        return response.enabled;
    }

    /**
     * Enable or disable auto-boot on SHIFT-BREAK.
     *
     * When enabled, SHIFT-BREAK causes MOS to load and run !Boot.
     *
     * @param enabled - True to enable auto-boot.
     * @returns True if successful.
     */
    async setStartupAutoBoot(enabled: boolean): Promise<boolean> {
        const response = await promisify<{ enabled: boolean }, SetStartupAutoBootResponse>(
            this._stub as unknown as Record<string, Function>,
            "setStartupAutoBoot",
            { enabled },
        );
        return response.success;
    }
}
