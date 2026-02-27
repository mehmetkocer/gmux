// SPDX-License-Identifier: AGPL-3.0-or-later
// gmux - Built-in terminal color themes
// Color values sourced from Gogh (MIT) and official theme repositories

#ifndef GMUX_THEMES_H
#define GMUX_THEMES_H

typedef struct {
    const char *name;
    const char *variant;  // "dark" or "light"
    const char *foreground;
    const char *background;
    const char *cursor;
    const char *palette[16];
} ThemePreset;

static const ThemePreset builtin_themes[] = {
    {
        .name = "Dracula",
        .variant = "dark",
        .foreground = "#F8F8F2",
        .background = "#282A36",
        .cursor = "#F8F8F2",
        .palette = {
            "#262626", "#FF5555", "#50FA7B", "#F1FA8C",
            "#BD93F9", "#FF79C6", "#8BE9FD", "#F8F8F2",
            "#7A7A7A", "#FF6E6E", "#69FF94", "#FFFFA5",
            "#D6ACFF", "#FF92DF", "#A4FFFF", "#FFFFFF",
        },
    },
    {
        .name = "Nord",
        .variant = "dark",
        .foreground = "#D8DEE9",
        .background = "#2E3440",
        .cursor = "#D8DEE9",
        .palette = {
            "#3B4252", "#BF616A", "#A3BE8C", "#EBCB8B",
            "#81A1C1", "#B48EAD", "#88C0D0", "#E5E9F0",
            "#4C566A", "#BF616A", "#A3BE8C", "#EBCB8B",
            "#81A1C1", "#B48EAD", "#8FBCBB", "#ECEFF4",
        },
    },
    {
        .name = "Nord Light",
        .variant = "light",
        .foreground = "#2E3440",
        .background = "#ECEFF4",
        .cursor = "#2E3440",
        .palette = {
            "#2E3440", "#BF616A", "#A3BE8C", "#EBCB8B",
            "#81A1C1", "#B48EAD", "#88C0D0", "#D8DEE9",
            "#4C566A", "#BF616A", "#A3BE8C", "#EBCB8B",
            "#81A1C1", "#B48EAD", "#8FBCBB", "#ECEFF4",
        },
    },
    {
        .name = "Catppuccin Mocha",
        .variant = "dark",
        .foreground = "#CDD6F4",
        .background = "#1E1E2E",
        .cursor = "#F5E0DC",
        .palette = {
            "#45475A", "#F38BA8", "#A6E3A1", "#F9E2AF",
            "#89B4FA", "#F5C2E7", "#94E2D5", "#BAC2DE",
            "#585B70", "#F38BA8", "#A6E3A1", "#F9E2AF",
            "#89B4FA", "#F5C2E7", "#94E2D5", "#A6ADC8",
        },
    },
    {
        .name = "Catppuccin Latte",
        .variant = "light",
        .foreground = "#4C4F69",
        .background = "#EFF1F5",
        .cursor = "#DC8A78",
        .palette = {
            "#5C5F77", "#D20F39", "#40A02B", "#DF8E1D",
            "#1E66F5", "#EA76CB", "#179299", "#ACB0BE",
            "#6C6F85", "#D20F39", "#40A02B", "#DF8E1D",
            "#1E66F5", "#EA76CB", "#179299", "#BCC0CC",
        },
    },
    {
        .name = "Catppuccin Frappe",
        .variant = "dark",
        .foreground = "#C6D0F5",
        .background = "#303446",
        .cursor = "#F2D5CF",
        .palette = {
            "#51576D", "#E78284", "#A6D189", "#E5C890",
            "#8CAAEE", "#F4B8E4", "#81C8BE", "#B5BFE2",
            "#626880", "#E78284", "#A6D189", "#E5C890",
            "#8CAAEE", "#F4B8E4", "#81C8BE", "#A5ADCE",
        },
    },
    {
        .name = "Catppuccin Macchiato",
        .variant = "dark",
        .foreground = "#CAD3F5",
        .background = "#24273A",
        .cursor = "#F4DBD6",
        .palette = {
            "#494D64", "#ED8796", "#A6DA95", "#EED49F",
            "#8AADF4", "#F5BDE6", "#8BD5CA", "#B8C0E0",
            "#5B6078", "#ED8796", "#A6DA95", "#EED49F",
            "#8AADF4", "#F5BDE6", "#8BD5CA", "#A5ADCB",
        },
    },
    {
        .name = "Gruvbox Dark",
        .variant = "dark",
        .foreground = "#EBDBB2",
        .background = "#282828",
        .cursor = "#EBDBB2",
        .palette = {
            "#282828", "#CC241D", "#98971A", "#D79921",
            "#458588", "#B16286", "#689D6A", "#A89984",
            "#928374", "#FB4934", "#B8BB26", "#FABD2F",
            "#83A598", "#D3869B", "#8EC07C", "#EBDBB2",
        },
    },
    {
        .name = "Gruvbox Light",
        .variant = "light",
        .foreground = "#3C3836",
        .background = "#FBF1C7",
        .cursor = "#3C3836",
        .palette = {
            "#FBF1C7", "#CC241D", "#98971A", "#D79921",
            "#458588", "#B16286", "#689D6A", "#7C6F64",
            "#928374", "#9D0006", "#79740E", "#B57614",
            "#076678", "#8F3F71", "#427B58", "#3C3836",
        },
    },
    {
        .name = "Solarized Dark",
        .variant = "dark",
        .foreground = "#839496",
        .background = "#002B36",
        .cursor = "#839496",
        .palette = {
            "#073642", "#DC322F", "#859900", "#B58900",
            "#268BD2", "#D33682", "#2AA198", "#EEE8D5",
            "#002B36", "#CB4B16", "#586E75", "#657B83",
            "#839496", "#6C71C4", "#93A1A1", "#FDF6E3",
        },
    },
    {
        .name = "Solarized Light",
        .variant = "light",
        .foreground = "#657B83",
        .background = "#FDF6E3",
        .cursor = "#657B83",
        .palette = {
            "#073642", "#DC322F", "#859900", "#B58900",
            "#268BD2", "#D33682", "#2AA198", "#EEE8D5",
            "#002B36", "#CB4B16", "#586E75", "#657B83",
            "#839496", "#6C71C4", "#93A1A1", "#FDF6E3",
        },
    },
    {
        .name = "One Dark",
        .variant = "dark",
        .foreground = "#ABB2BF",
        .background = "#282C34",
        .cursor = "#528BFF",
        .palette = {
            "#282C34", "#E06C75", "#98C379", "#E5C07B",
            "#61AFEF", "#C678DD", "#56B6C2", "#ABB2BF",
            "#545862", "#E06C75", "#98C379", "#E5C07B",
            "#61AFEF", "#C678DD", "#56B6C2", "#C8CCD4",
        },
    },
    {
        .name = "One Light",
        .variant = "light",
        .foreground = "#383A42",
        .background = "#FAFAFA",
        .cursor = "#526FFF",
        .palette = {
            "#383A42", "#E45649", "#50A14F", "#C18401",
            "#4078F2", "#A626A4", "#0184BC", "#A0A1A7",
            "#696C77", "#E45649", "#50A14F", "#C18401",
            "#4078F2", "#A626A4", "#0184BC", "#090A0B",
        },
    },
    {
        .name = "Tokyo Night",
        .variant = "dark",
        .foreground = "#A9B1D6",
        .background = "#1A1B26",
        .cursor = "#C0CAF5",
        .palette = {
            "#15161E", "#F7768E", "#9ECE6A", "#E0AF68",
            "#7AA2F7", "#BB9AF7", "#7DCFFF", "#A9B1D6",
            "#414868", "#F7768E", "#9ECE6A", "#E0AF68",
            "#7AA2F7", "#BB9AF7", "#7DCFFF", "#C0CAF5",
        },
    },
    {
        .name = "Tokyo Night Storm",
        .variant = "dark",
        .foreground = "#A9B1D6",
        .background = "#24283B",
        .cursor = "#C0CAF5",
        .palette = {
            "#1D202F", "#F7768E", "#9ECE6A", "#E0AF68",
            "#7AA2F7", "#BB9AF7", "#7DCFFF", "#A9B1D6",
            "#414868", "#F7768E", "#9ECE6A", "#E0AF68",
            "#7AA2F7", "#BB9AF7", "#7DCFFF", "#C0CAF5",
        },
    },
    {
        .name = "Rose Pine",
        .variant = "dark",
        .foreground = "#E0DEF4",
        .background = "#191724",
        .cursor = "#524F67",
        .palette = {
            "#26233A", "#EB6F92", "#31748F", "#F6C177",
            "#9CCFD8", "#C4A7E7", "#EBBCBA", "#E0DEF4",
            "#6E6A86", "#EB6F92", "#31748F", "#F6C177",
            "#9CCFD8", "#C4A7E7", "#EBBCBA", "#E0DEF4",
        },
    },
    {
        .name = "Rose Pine Moon",
        .variant = "dark",
        .foreground = "#E0DEF4",
        .background = "#232136",
        .cursor = "#59546D",
        .palette = {
            "#393552", "#EB6F92", "#3E8FB0", "#F6C177",
            "#9CCFD8", "#C4A7E7", "#EA9A97", "#E0DEF4",
            "#6E6A86", "#EB6F92", "#3E8FB0", "#F6C177",
            "#9CCFD8", "#C4A7E7", "#EA9A97", "#E0DEF4",
        },
    },
    {
        .name = "Rose Pine Dawn",
        .variant = "light",
        .foreground = "#575279",
        .background = "#FAF4ED",
        .cursor = "#9893A5",
        .palette = {
            "#F2E9E1", "#B4637A", "#286983", "#EA9D34",
            "#56949F", "#907AA9", "#D7827E", "#575279",
            "#9893A5", "#B4637A", "#286983", "#EA9D34",
            "#56949F", "#907AA9", "#D7827E", "#575279",
        },
    },
    {
        .name = "Everforest Dark",
        .variant = "dark",
        .foreground = "#D3C6AA",
        .background = "#2D353B",
        .cursor = "#D3C6AA",
        .palette = {
            "#343F44", "#E67E80", "#A7C080", "#DBBC7F",
            "#7FBBB3", "#D699B6", "#83C092", "#D3C6AA",
            "#475258", "#E67E80", "#A7C080", "#DBBC7F",
            "#7FBBB3", "#D699B6", "#83C092", "#D3C6AA",
        },
    },
    {
        .name = "Monokai Dark",
        .variant = "dark",
        .foreground = "#F8F8F2",
        .background = "#272822",
        .cursor = "#F8F8F0",
        .palette = {
            "#272822", "#F92672", "#A6E22E", "#F4BF75",
            "#66D9EF", "#AE81FF", "#A1EFE4", "#F8F8F2",
            "#75715E", "#F92672", "#A6E22E", "#F4BF75",
            "#66D9EF", "#AE81FF", "#A1EFE4", "#F9F8F5",
        },
    },
    {
        .name = "Kanagawa",
        .variant = "dark",
        .foreground = "#DCD7BA",
        .background = "#1F1F28",
        .cursor = "#C8C093",
        .palette = {
            "#16161D", "#C34043", "#76946A", "#C0A36E",
            "#7E9CD8", "#957FB8", "#6A9589", "#C8C093",
            "#727169", "#E82424", "#98BB6C", "#E6C384",
            "#7FB4CA", "#938AA9", "#7AA89F", "#DCD7BA",
        },
    },
    {
        .name = "Zenburn",
        .variant = "dark",
        .foreground = "#DCDCCC",
        .background = "#3F3F3F",
        .cursor = "#DCDCCC",
        .palette = {
            "#1E2320", "#CC9393", "#7F9F7F", "#F0DFAF",
            "#8CD0D3", "#DC8CC3", "#93E0E3", "#DCDCCC",
            "#709080", "#DCA3A3", "#BFEBBF", "#F0DFAF",
            "#8CD0D3", "#DC8CC3", "#93E0E3", "#FFFFFF",
        },
    },
};

#define BUILTIN_THEME_COUNT (sizeof(builtin_themes) / sizeof(builtin_themes[0]))

#endif // GMUX_THEMES_H
