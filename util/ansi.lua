-- ANSI escape sequences and colored output for terminal emulators.
--
-- LINKS:
--  https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
--  https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797#general-ascii-codes
--  https://notes.burke.libbey.me/ansi-escape-codes/

ANSI = {}
ANSI.ESC = "\27"

-- Control Sequence Introducer: `ESC[`. Prefixes many escape sequences.
ANSI.OSC = ANSI.ESC .. '['

-- `Select Graphic Rendition`, a.k.a. the "function" `m`, sets color and style
-- of the characters that are printed after this. It is always "called" in the
-- form: `\x1b[<args>m`, where `<args>` is context-depentdent.
--
-- You can combine styling modes and basic colors, e.g:
-- `"\27[1;31m]"` will set the text output to bold and colored red.
SGR = {
    STYLES = {
        RESET     = 0, -- Turns off all colors and styling modes.
        BOLD      = 1, -- Sets style to bold, can come before colors.
        DIM       = 2, -- Dims/fades output.
        ITALIC    = 3, -- Sets style to italic, can come before colors.
        UNDERLINE = 4, -- Sets style to underlined, can come before colors.
        REVERSE   = 7,
        HIDE      = 8,
        STRIKETHROUGH = 9,
    },
    -- The 8 basic foreground colors that most terminals *should* support.
    -- These are supposed to be called in the form `\x1b[<color-code>m`.
    COLORS = {
        BLACK   = 30, RED  = 31, GREEN = 32, YELLOW  = 33, BLUE  = 34,
        MAGENTA = 35, CYAN = 36, WHITE = 37, DEFAULT = 39, RESET = 0,
    },
    BGCOLORS = {
        BLACK   = 40, RED  = 41, GREEN = 42, YELLOW  = 43, BLUE  = 44,
        MAGENTA = 45, CYAN = 46, WHITE = 47, DEFAULT = 49, RESET = 0,
    },
}

-- Use this to query the `SGR.STYLES` table for string names like `"BOLD"` and
-- `"ITALIC"`.
---@param style string
---@nodiscard
function SGR.get_style(style)
    local value = SGR.STYLES[string.upper(style)]
    if not value then
        io.stderr:write("Invalid SGR style name '", style, "'.\n")
        return nil
    end
    return value
end

-- Turn on simple styles like bold, italic, strikethrough and such.
---@param style integer
---@nodiscard
function SGR.set_style(style)
    return ANSI.OSC .. style .. 'm'
end

-- Turns off all styles and resets all colors back to default.
---@nodiscard
function SGR.reset_styles()
    return SGR.set_style(SGR.STYLES.RESET)
end

-- Use this to quickly look up simply ANSI foreground colors. See the members
-- for the tables `SGR.COLORS` and `SGR.BGCOLORS`.
---@param color string
---@param is_bg boolean
---@nodiscard
function SGR.get_color(color, is_bg)
    local colors = SGR[is_bg and "BGCOLORS" or "COLORS"]
    local code   = colors[string.upper(color)]
    if not code then
        io.stderr:write("Invalid color name '", color, "'.\n")
        return nil
    end
    return code
end

-- Query one of `SGR.COLORS` or `SGR.BGCOLORS`.
---@param color string|integer  Key or a number literal.
---@param is_bg boolean         If true, query `SGR.BGCOLORS`.
local function validate_color(color, is_bg)
    if type(color) == "number" then
        return color
    else
        return SGR.get_color(color, is_bg)
    end
end

-- Do not pass `nil` as we implicitly resort to `SGR.get_style` which will only
-- work with strings. For explicitly `nil` inputs please check beforehand.
---@param style string|integer  Key or a number literal.
local function validate_style(style)
    if type(style) == "number" then
        return style
    else
        return SGR.get_style(style)
    end
end

-- Generic-ish function to change the foreground or background color.
-- Uses the 8 basic ANSI colors, although this assumes that your terminal
-- supports that which is not a guarantee.
---@param color integer  Color code from `SGR.COLORS` or `SGR.BGCOLORS`.
---@param mode  integer? Style mode from `SGR.STYLES`.
---@nodiscard
function SGR.set_color(color, mode)
    -- If no `mode` passed, just print empty string to not mess up the command.
    return ANSI.OSC .. (mode and (mode .. ';') or '') .. color .. 'm'
end

-- Changes the foreground/text color using only the basic 8 ANSI colors.
-- `mode` can modify the text to be bold, italic, underlined, etc.
---@param color string|integer
---@param mode  string|integer?
---@nodiscard
function SGR.set_fg_color(color, mode)
    return SGR.set_color(validate_color(color, false),
                         mode and validate_style(mode) or nil)
end

-- Changes the background color using only the basic 8 ANSI colors.
-- Note that `mode` likely won't affect anything here.
---@param color string|integer
---@param mode  string|integer?
---@nodiscard
function SGR.set_bg_color(color, mode)
    return SGR.set_color(validate_color(color, true),
                         mode and validate_style(mode) or nil)
end

---@nodiscard
function SGR.reset_fg_color()
    return SGR.set_fg_color(SGR.COLORS.DEFAULT)
end

---@nodiscard
function SGR.reset_bg_color()
    return SGR.set_bg_color(SGR.BGCOLORS.DEFAULT)
end

-- Brings both foreground and background colors back to their defaults.
---@nodiscard
function SGR.reset_colors()
    return SGR.reset_fg_color() .. SGR.reset_bg_color()
end
