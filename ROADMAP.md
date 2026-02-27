# gmux - GTK4 Terminal Multiplexer

A modern terminal multiplexer built with GTK4 and VTE, featuring vertical tabs and project management capabilities.

## Current Status: ✅ WORKING

gmux is now fully functional with a clean VTE-based implementation.

## What We've Accomplished

### Architecture Decisions
- **Abandoned libghostty approach**: libghostty doesn't support Linux/GTK yet (only macOS/iOS)
- **Switched to VTE (libvte-2.91-gtk4)**: Mature, stable terminal widget library used by GNOME Terminal, Tilix, and other major Linux terminals
- **Clean C implementation**: 306 lines of well-structured C code

### Current Features
✅ **Vertical tabs sidebar** (200px width)
- List of terminal instances on the left
- Click to switch between terminals
- Clear visual separation from terminal content

✅ **Terminal management**
- Add new terminal tabs with + button
- Remove terminals with - button
- Each terminal spawns your default shell ($SHELL)
- Proper cleanup on tab close

✅ **Full terminal functionality**
- Complete VTE terminal emulator in each tab
- 10,000 lines scrollback buffer
- Mouse support (autohide)
- Scrollable terminal area

✅ **Dynamic tab titles**
- Tab names update automatically when terminal sets title
- Falls back to "Terminal N" naming

✅ **Stable and reliable**
- No crashes or segfaults
- Proper GTK4 event handling
- Clean memory management

## Building

### Dependencies
```bash
sudo apt install build-essential pkg-config
sudo apt install libgtk-4-dev libvte-2.91-gtk4-dev
```

### Compile
```bash
make
```

### Run
```bash
./gmux
```

## Project Structure
```
gmux/
├── src/
│   └── main.c          # Main application code (306 lines)
├── Makefile            # Build configuration
├── ROADMAP.md          # This file
└── ghostty/            # Ghostty submodule (not used - kept for reference)
```

## Future Roadmap

### High Priority

#### 1. Horizontal Tab Support in Terminals
- Add built-in tab support within each terminal
- Show tabs horizontally at the top of each terminal area
- Allow Ctrl+Shift+T to create new tabs within a terminal
- Each terminal instance can have multiple tabs

#### 2. Theme Support
- Theme picker UI in preferences
- Control VTE terminal colors from gmux
- Support for popular terminal themes:
  - Dracula
  - Solarized Dark/Light
  - Nord
  - One Dark
  - Gruvbox
  - Custom themes
- Save theme preferences per project

#### 3. Project Management
- Define projects with:
  - Name
  - Default directory
  - Color/icon for identification
  - List of default terminals to open
- Project switcher in left panel
- Quick project selection dropdown

#### 4. Default Project Directories
- Open terminals in project's root directory automatically
- Support for multiple directories per project
- Remember last working directory per project

#### 5. Enhanced Left Panel
- **Two-mode panel**:
  - Project list mode (default)
  - Terminal list mode (current implementation)
- Toggle between project view and terminal view
- Visual indicators:
  - Active project highlighted
  - Number of terminals per project
  - Project icons/colors
- Search/filter projects
- Drag-and-drop terminal reordering

### Medium Priority

#### 6. Terminal Customization
- Font size control (per terminal or global)
- Font family selection
- Terminal opacity/transparency
- Cursor style options
- Bell behavior configuration

#### 7. Session Management
- Save workspace sessions
- Restore terminals on startup
- Per-project session persistence
- Export/import session configurations

#### 8. Split Panes
- Horizontal/vertical terminal splits
- Multi-pane layouts within a single tab
- Resize panes with mouse

### Low Priority

#### 9. Keyboard Shortcuts
- Configurable keybindings
- Quick terminal switching (Ctrl+1-9)
- Global shortcuts
- Vim-style navigation option

#### 10. Advanced Features
- SSH connection manager
- Command history across terminals
- Terminal broadcast (send to all terminals)
- Custom terminal profiles
- Startup commands per project

## Technical Notes

### Why VTE instead of libghostty?
- **libghostty** is designed for macOS/iOS embedding only
- The C API only exposes `GHOSTTY_PLATFORM_MACOS` and `GHOSTTY_PLATFORM_IOS`
- Linux/GTK support in libghostty is marked as "in progress" (⚠️)
- **VTE** is the standard solution for Linux terminal embedding

### VTE Advantages
- Mature and battle-tested (used by GNOME Terminal since 2002)
- Full GTK4 support
- Excellent documentation
- Active development
- Complete terminal emulation (xterm compatible)

## Development History

### Failed Approach: X11 Window Reparenting
- Initial attempt tried to spawn Ghostty processes and reparent their windows using XReparentWindow
- This failed because reparenting GTK4 windows breaks GTK's internal state
- Resulted in visual glitches, crashes, and broken layouts

### Failed Approach: libghostty Direct API
- Second attempt tried to use libghostty C API like cmux does on macOS
- Built libghostty successfully with `-Dapp-runtime=none`
- Failed because libghostty lacks Linux/GTK platform support:
  - Only `GHOSTTY_PLATFORM_MACOS` and `GHOSTTY_PLATFORM_IOS` available
  - No `GHOSTTY_PLATFORM_GTK` or `GHOSTTY_PLATFORM_LINUX`
  - `ghostty_surface_new()` always failed on Linux

### Successful Approach: VTE Terminal Widget
- Third attempt using VTE (Virtual Terminal Emulator) widget library
- Works perfectly - this is the standard Linux solution
- Simple, clean, stable implementation

## License

AGPL-3.0-or-later

## Credits

Built with:
- GTK4 - Modern toolkit for creating graphical user interfaces
- VTE - Virtual Terminal Emulator widget library

Inspired by:
- cmux - Terminal multiplexer for macOS using libghostty
- tmux - Terminal multiplexer
- GNOME Terminal - VTE reference implementation
