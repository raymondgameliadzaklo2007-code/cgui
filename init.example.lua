-- Example ~/.config/cgui/init.lua for cgui
local cgui = require("cgui") -- or access global 'cgui'

-- ============================================================================
-- Option B: Editor Settings
-- ============================================================================
cgui.config = {
  theme = "onedark",       -- "onedark", "catppuccin", "tokyonight", "nord", "gruvbox", "light"
  font_size = 15,          -- Default editor font size
  soft_wrap = false,       -- Toggle soft line wrapping
  line_numbers = true,     -- Show or hide gutter line numbers
  lsp_server = "clangd",   -- LSP server executable ("clangd", "rust-analyzer", "gopls")
}

-- ============================================================================
-- Option C: Custom Commands and Keybindings
-- ============================================================================

-- Register custom actions that appear in the Command Palette (Ctrl+Shift+P)
cgui.commands.register("custom:hello", function()
  cgui.log("Hello from Lua extension!")
end, "Say Hello from Lua", "Ctrl+Alt+H")

cgui.commands.register("custom:toggle_dark_light", function()
  cgui.theme.toggle()
end, "Theme: Toggle Dark / Light", "Alt+T")

-- Bind direct keyboard shortcuts to registered commands or custom functions
cgui.keymap.bind("F2", "lsp:rename")
cgui.keymap.bind("F5", "theme:picker")
cgui.keymap.bind("Alt+T", "theme:toggle")
cgui.keymap.bind("Ctrl+Shift+P", "palette:open")
cgui.keymap.bind("Ctrl+Space", "lsp:complete")
cgui.keymap.bind("Ctrl+Shift+Space", "lsp:signature_help")
cgui.keymap.bind("F12", "lsp:definition")
cgui.keymap.bind("Shift+F12", "lsp:references")
cgui.keymap.bind("Alt+K", "lsp:hover")
cgui.keymap.bind("Ctrl+Shift+I", "lsp:format")
cgui.keymap.bind("Ctrl+`", "terminal:toggle")

cgui.log("cgui Lua configuration loaded successfully.")
