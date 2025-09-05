# GWidgetSuite

**GWidgetSuite** is a collection of noodly, GTK3-based C++ widgets designed for Linux desktops.  
Think of it as **“my shell, but blurred, rounded, and reborn”** — a fun, in-progress, playground for GNOME Ricers. ❀⸜(˶´ ˘ `˶)⸝❀

---

## Features

- **Clock Widget** – Your classic clock, but elegant.  
- **Dashboard** – Elegant calendar and to-do note-taking widget.  
- **GIF Player** – Because static images are boring and ricing your desktop is fun (ദ്ദി˙ᗜ˙)  
- **Weather Widget** – Real-time weather info... Currently working on improving, quite disappointing.

All widgets are **GTK3/C++**, fully customizable, and designed to work on GNOME with optional blur & rounded window corners. Eh... It will be the opposite of elegant without said "Blur My Shell" and "Window Corners Reborn" GNOME Extensions. Pardon me, work in progress.

---

## Screenshots

### Widgets
![Example Widget](screenshots/Screenshot%20From%202025-09-05%2009-44-20.png)

### Widgets in action
[Watch Screencast](screenshots/Screencast%20From%202025-09-05%2009-43-17.mp4)

---

## Dependencies

Install GTK3 and related libraries depending on your distro:

### Ubuntu / Debian
```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev \
    libcairo2-dev libpango1.0-dev libgdk-pixbuf2.0-dev libcurl4-openssl-dev
```

### Fedora
```bash
sudo dnf install -y gcc-c++ pkg-config gtk3-devel cairo-devel pango-devel \
    gdk-pixbuf2-devel libcurl-devel
```

### Arch / Manjaro
```bash
sudo pacman -S --needed base-devel pkg-config gtk3 cairo pango gdk-pixbuf2 curl
```

---

## Build

Each widget can be compiled separately:

```bash
# Clock Widget
g++ -O3 -march=native -std=c++17 -Wall -Wextra \
    `pkg-config --cflags gtk+-3.0` clock_widget.cpp \
    `pkg-config --libs gtk+-3.0` -o clock_widget

# Dashboard
g++ -O3 -march=native -std=c++17 -Wall -Wextra \
    `pkg-config --cflags gtk+-3.0` dashboard.cpp \
    `pkg-config --libs gtk+-3.0` -o dashboard

# GIF Player
g++ -O3 -march=native -std=c++17 -Wall -Wextra \
    `pkg-config --cflags gtk+-3.0 gdk-pixbuf-2.0` GIF_Player.cpp \
    `pkg-config --libs gtk+-3.0 gdk-pixbuf-2.0` -o gif_player

# Weather Widget
g++ -O3 -march=native -std=c++17 -Wall -Wextra -flto -ffast-math \
    -funroll-loops -finline-functions -fomit-frame-pointer -DNDEBUG \
    weather.cpp \
    `pkg-config --cflags --libs gtk+-3.0 cairo pangocairo` -lcurl \
    -Wl,--strip-all -Wl,--gc-sections -o weather
```

---

## Notes

- Designed for GNOME / GTK3 desktops.  
- Rounded corners & blur effects require **supporting window manager / compositor** (Mutter/GShell extensions).  
- Widgets are “nood as hell” — perfect for tinkering and personalising your desktop.
- Add binaries to startup and have widgets on login.  

## Known Issues

### Bugs
- Dashboard may segfault if multiple instances are opened.
- Weather widget may display a random line.
- Implementing native rounding in GIF Player causes GIF playback to lag.
- Clock widget requires NTP; systems using only RTC may break certain features.
- Positioning only works on X11, Wayland positioning is yet to be implemented.
- Windows may be blurry for 1-3 seconds during loading on Login.

### Missing Features
- No native window rounding implemented yet.
- Fonts like SF Pro Display are not found on all systems.
- Weather widget icon fetching may fail on non-Ubuntu (Ubuntu GNOME) systems.
- Memory optimisation is currently poor; much improvement needed.
- Positioning only works on X11, Wayland positioning is yet to be implemented.
- To add widget position locking feature (Disable drag)

### Yet to Address / Work in Progress
- Improve compatibility with various fonts.
- Optimise memory usage across all widgets.
- Add workaround for RTC-based clocks.
- Implement smooth native rounding for all widgets without performance hit.

---

## License

MIT License — do whatever you like, just don’t be surprised if your shell cries a little.

---

## Contributing

- Fork it, add new widgets, fix bugs, or improve or implement native blur/round effects.  
- Pull requests are welcome, but make it noodly.

Written by a Linux Noob for Aesthetic Customisation on GNOME  人(_ _*)
