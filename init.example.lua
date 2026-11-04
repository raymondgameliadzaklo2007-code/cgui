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
cgui.keymap.bind("F2", "theme:picker")
cgui.keymap.bind("Alt+T", "theme:toggle")
cgui.keymap.bind("Ctrl+Shift+P", "palette:open")

cgui.log("cgui Lua configuration loaded successfully.")
