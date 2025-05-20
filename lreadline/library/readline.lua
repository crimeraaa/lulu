---@meta

---@class readline
local readline = {}

---Read a line of text, ending in a newline, from the standard input.
---This wraps a call to `readline(const char *prompt)` from *GNU Readline*.
---This also calls `add_history()` should the resulting line be non-empty.
---
---@param prompt? string If `nil` the no prompt is issued.
---@return string? line If `nil`, then EOF with no other text was entered.
function readline.readline(prompt) end

---This wraps a call to `add_history(const char *text)` from *GNU Readline*.
---@param text string
function readline.add_history(text) end

---This wraps a call to `clear_history(void)` from *GNU Readline*.
function readline.clear_history() end

---Not a function from *GNU Readline*. Rather this helps set up the global
---state of the library so it knows what words it can us to autocomplete.
---
---@param c Completer Most likely watches a 'dynamic' environment, e.g. `_G`.
---@return Completer c
function readline.set_completer(c) end

return readline
