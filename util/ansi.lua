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
        BLACK   = 30,
        RED     = 31,
        GREEN   = 32,
        YELLOW  = 33,
        BLUE    = 34,
        MAGENTA = 35,
        CYAN    = 36,
        WHITE   = 37,
        DEFAULT = 39,
        RESET   = 0,
    },
    BGCOLORS = {
        BLACK   = 40,
        RED     = 41,
        GREEN   = 42,
        YELLOW  = 43,
        BLUE    = 44,
        MAGENTA = 45,
        CYAN    = 46,
        WHITE   = 47,
        DEFAULT = 49,
        RESET   = 0,
    },
}

-- If `style` is a string use it to query into the `SGR` table.
-- Otherwise, if it is an integer, return it directly as we can assume it
-- was an already indexed value e.g. `SGR.STYLES.`.
---@param style string|integer
---@nodiscard
function SGR:get_style(style)
    if type(style) == "string" then
        local value = self.STYLES[string.upper(style)]
        if not value then
            io.stderr:write("Invalid SGR style name '", style, "'.\n")
            return nil
        end
        return value
    end
    return style
end

-- Turn on simple styles like bold, italic, strikethrough and such.
---@param style string|integer
---@nodiscard
function SGR:set_style(style)
    style = self:get_style(style)
    if not style then
        return nil
    end
    return ANSI.OSC .. style .. 'm'
end

-- Turns off all styles and resets all colors back to default.
---@nodiscard
function SGR:reset_styles()
    return self:set_style(self.STYLES.RESET)
end

-- Query the one of the `SGR` color tables (depending on `is_bg`) for the
-- given `color` if it is a string. If it is an integer, we just return it.
---@param color string|integer
---@param is_bg boolean?
---@nodiscard
function SGR:get_color(color, is_bg)
    if type(color) == "string" then
        local colors = self[is_bg and "BGCOLORS" or "COLORS"]
        local code   = colors[string.upper(color)]
        if not code then
            io.stderr:write("Invalid color name '", color, "'.\n")
            return nil
        end
        return code
    end
    return color
end

-- Generic-ish function to change the foreground or background color.
-- Uses the 8 basic ANSI colors, although this assumes that your terminal
-- supports that which is not a guarantee.
---@param color string|integer  Color code or key into `SGR.COLORS`.
---@param mode  string|integer? If non-nil, prepend this and append ';'.
---@param is_bg boolean?        If true, use `SGR.BGCOLORS` instead.
---@nodiscard
function SGR:set_color(color, mode, is_bg)
    color = self:get_color(color, is_bg)
    if not color then
        return nil
    end
    -- Allow mode passed to this function to be nil.
    if mode then
        mode = self:get_style(mode)
        if not mode then
            return nil
        end
    end
    -- If no `mode` passed, just print empty string to not mess up the command.
    return ANSI.OSC .. (mode and (mode .. ';') or '') .. color .. 'm'
end

-- Changes the foreground/text color using only the basic 8 ANSI colors.
-- `mode` can modify the text to be bold, italic, underlined, etc.
---@param color string|integer
---@param mode  string|integer?
---@nodiscard
function SGR:set_fg_color(color, mode)
    return self:set_color(color, mode, false)
end

-- Changes the background color using only the basic 8 ANSI colors.
-- Note that `mode` likely won't affect anything here.
---@param color string|integer
---@param mode  string|integer?
---@nodiscard
function SGR:set_bg_color(color, mode)
    return self:set_color(color, mode, true)
end

---@nodiscard
function SGR:reset_fg_color()
    return self:set_fg_color(self.COLORS.DEFAULT)
end

---@nodiscard
function SGR:reset_bg_color()
    return self:set_bg_color(self.BGCOLORS.DEFAULT)
end

-- Brings both foreground and background colors back to their defaults.
---@nodiscard
function SGR:reset_colors()
    return self:reset_fg_color() .. self:reset_bg_color()
end
