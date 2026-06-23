Theming (CSS)
=============

A qbar theme is a **single standard-CSS file** pointed to by the ``styleSheet`` config key.
Edit it and the bar restyles **live** — no restart. 27 themes ship in ``config/themes``.

.. code-block:: json

   { "styleSheet": "~/.config/qbar/themes/tokyo-night.css" }

qbar parses real CSS: id selectors, classes/states, parts, ``@define-color``, ``@keyframes``,
gradients, ``box-shadow``, per-corner ``border-radius`` and transitions. Property-level
rules are pushed onto the matching QML element; rich rendering (gradients, shadows, bevels,
background images) is drawn by a Shape-based renderer.

Selectors
---------

**Applet ids.** Each applet is selected by its id. qbar keeps the **waybar id aliases** so
existing waybar themes mostly work as-is:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Applet
     - id (and waybar alias)
     - Notes
   * - The bar window
     - ``window#waybar``
     - Root background / rounding.
   * - Region containers
     - ``#left`` / ``#center`` / ``#right``
     - Style the three module groups (pill backgrounds, margins).
   * - Clock
     - ``#clock``
     -
   * - CPU / Memory
     - ``#cpu`` / ``#memory``
     - See *Parts*.
   * - Network
     - ``#network-io`` / ``#nm-applet``
     - IO graph / NetworkManager.
   * - Sound
     - ``#pulseaudio``
     - State ``.muted``.
   * - Brightness
     - ``#backlight``
     -
   * - Keyboard layout
     - ``#keyboard``
     -
   * - Disk / Bluetooth / Power
     - ``#disk`` / ``#bluetooth`` / ``#power-profiles-daemon``
     -
   * - Temperature / Battery
     - ``#temperature`` / ``#battery``
     -
   * - Caffeine
     - ``#caffeine``
     - State ``.active``.
   * - Title
     - ``#title``
     -
   * - Tray
     - ``#tray``
     -
   * - Custom tools
     - ``#custom-<name>``
     - e.g. ``#custom-btc``.
   * - Overlay / popups
     - ``#overlay`` / ``#popup`` / ``#<applet>-popup``
     - Backdrop and popup chrome (e.g. ``#cpu-popup``).

**States** are CSS classes on the id:

.. code-block:: css

   #battery.charging { color: #9ece6a; }
   #battery.critical { color: #f7768e; }
   #pulseaudio.muted { color: #565f89; }
   #caffeine.active  { color: #e0af68; }

**Workspaces** use button sub-selectors and states:

.. code-block:: css

   #workspaces button          { color: #565f89; background-color: #16161e; }
   #workspaces button.focused  { color: #c0caf5; background-color: #283457; }
   #workspaces button.visible  { color: #a9b1d6; }
   #workspaces button.urgent   { background-color: #f7768e; }

Parts
-----

Sub-elements of an applet are addressed as ``#id.part`` so they can be styled
independently of the applet's text/background:

.. code-block:: css

   #cpu.graph        { background-color: rgba(25,20,38,0.66); color: #f59d55;
                       fill: rgba(245,157,85,0.25); width: 34px; }
   #memory.swap      { color: #ffd28a; }
   #network-io.download { color: #67a9ff; fill: rgba(103,169,255,0.20); }
   #network-io.upload   { color: #7ad7c4; fill: rgba(122,215,196,0.22); }
   #network-io.arrowUp   { color: #7ad7c4; }
   #network-io.arrowDown { color: #67a9ff; }

* ``fill`` — the area fill under a graph/sparkline.
* ``width`` — fixed width for the part (e.g. the graph column).

Supported properties
---------------------

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Property
     - Notes
   * - ``color``
     - Foreground (text / glyph / line).
   * - ``background-color`` / ``background``
     - Solid color, or ``linear-gradient(...)`` via ``background``.
   * - ``background-image`` / ``background-size`` / ``background-image-opacity``
     - ``url("...")``; ``cover`` etc.; opacity of the image layer.
   * - ``border`` / ``border-color`` / ``border-width``
     - Border shorthand and parts.
   * - ``border-radius``
     - Single value or per-corner.
   * - ``padding`` / ``margin`` (+ ``-top``/``-bottom``/``-left``/``-right``)
     - Box spacing (1–4 value shorthand supported).
   * - ``box-shadow``
     - Drop and ``inset`` (bevel) shadows.
   * - ``text-shadow``
     - Text shadow.
   * - ``opacity``
     - Element opacity (also the popup's resting opacity on ``#popup``).
   * - ``fill``
     - Graph/sparkline area fill (parts).
   * - ``width``
     - Fixed part width.
   * - ``icon-name``
     - Themed icon name for icon applets (e.g. ``network-wireless-symbolic``).

At-rules and pseudo-elements
----------------------------

* ``@define-color name #hex;`` then reference it as ``@name`` — reusable palette.
* ``@keyframes`` + ``animation`` — keyframe animations (e.g. a pulsing critical battery).
* ``::before`` — a pseudo-element layer (e.g. an animated accent behind an applet).

.. code-block:: css

   @define-color rose #d56a83;
   @keyframes battery-pulse { 0% { opacity: 0; } 50% { opacity: 1; } 100% { opacity: 0; } }
   #battery.critical::before { background: @rose; animation: battery-pulse 1800ms ease-in-out infinite; }

Notes
-----

* **Hot reload:** saving the stylesheet re-applies it immediately.
* **Translucency:** translucent fills are rendered so they don't over-blend/darken — set
  the color with full alpha and carry transparency via ``opacity`` if you hand-author
  Shape-like layers; the built-in renderer already handles this for theme rules.
* Start from a bundled theme in ``config/themes`` and tweak.
