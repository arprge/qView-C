Personal fork of qView with very minimal image editing functionality

------

<h1 align=center>qView-C</h1>

<p align=center>qView is an image viewer designed with minimalism and usability in mind.</p>

<p align=center>
    <a href="https://github.com/jurplel/qView">Original Upstream Repository</a>
</p>

<p align=center>
   <img width="1907" height="1028" alt="screenshot_2026-11-09_14-47-05" src="https://github.com/user-attachments/assets/437cd2f9-c00e-4713-a2f0-c9cb91e7e8de" />
</p>

_______
All fork-specific editing code is isolated in the [`src/edit`](src/edit) directory:

- [`src/edit/qveditpanel.cpp`](src/edit/qveditpanel.cpp) / [`.h`](src/edit/qveditpanel.h) &mdash; Floating/collapsible edit toolbar panel with tabbed controls for Crop, Adjust, and Draw.
- [`src/edit/qvgraphicsviewedit.cpp`](src/edit/qvgraphicsviewedit.cpp) &mdash; Interactive viewport editing implementation (crop selection overlay with 8-point resize handles and canvas drawing interaction).
- [`src/edit/qvimagecoreedit.cpp`](src/edit/qvimagecoreedit.cpp) &mdash; Backend image manipulation (LUT-based tone curves for brightness/contrast, saturation and warmth shifts, pixel cropping, and safe file saving/replacement).
- [`src/edit/mainwindowedit.cpp`](src/edit/mainwindowedit.cpp) &mdash; Edit panel setup, session lifecycle, dirty-state tracking, and save/replace workflows.

### Editing Features

- **Crop** (Shortcut: <kbd>C</kbd>): Interactive crop rectangle with 8 drag handles, aspect ratio presets (Free, 1:1, 4:3, 16:9, 3:2, etc.), live dimension readouts, and reset capability.
- **Adjust** (Shortcut: <kbd>B</kbd>): Real-time adjustment sliders for Brightness, Contrast, Saturation, and Warmth with tone curve preview and reset.
- **Draw** (Shortcut: <kbd>D</kbd>): Annotation and sketching tools including Pen (freehand), Line, Rectangle, Circle, and Arrow with customizable brush size, color picker, eraser, and multi-step undo/redo.
- **Saving**: Overwrite the original file in-place or export as a new file ("Save Copy As...").

### Other changes 

- **Interface scaling**: Adjust the interface font and widget sizes from 50% to 200%

## License

qView is free and open-source software licensed under the **[GNU General Public License v3.0](LICENSE)** (GPL-3.0-or-later).

- Original qView copyright &copy; 2018&ndash;present jurplel and qView contributors.
- Fork additions and modifications copyright &copy; 2026 the fork contributors.
- In accordance with the GNU GPLv3, this fork and all modifications are distributed under the same license terms. See [LICENSE](LICENSE) for details.
