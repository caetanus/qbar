Configuration
=============

qbar reads JSON from ``$XDG_CONFIG_HOME/qbar/config.json`` (default
``~/.config/qbar/config.json``). The file is either a **single bar object**, or an
**array of bar objects** for multi-monitor / top-and-bottom setups. A JSON Schema ships
at ``config.schema.json`` and an example at ``data/config.example.json``.

.. code-block:: json

   {
     "height": 30,
     "position": "top",
     "styleSheet": "~/.config/qbar/themes/tokyo-night.css",
     "windowManager": { "backend": "auto" },
     "modules-left":   ["Workspaces", "CPU", "Memory", "Network"],
     "modules-center": ["Clock"],
     "modules-right":  ["Temperature", "Sound", "Battery", "Tray"]
   }

Bar options
-----------

.. list-table::
   :header-rows: 1
   :widths: 22 14 64

   * - Key
     - Type
     - Description
   * - ``layer``
     - string
     - ``wlr-layer-shell`` layer: ``background`` | ``bottom`` | ``top`` | ``overlay``.
   * - ``position``
     - string
     - Bar edge: ``top`` | ``bottom``.
   * - ``height``
     - integer
     - Bar height in pixels.
   * - ``margin`` / ``marginTop`` / ``marginBottom`` / ``marginLeft`` / ``marginRight``
     - integer
     - Outer margins around the bar.
   * - ``spacing``
     - integer
     - Gap between applets.
   * - ``x`` / ``y``
     - integer
     - Explicit position (``-1`` = auto).
   * - ``exclusiveZone``
     - boolean
     - Reserve screen space so windows don't overlap the bar.
   * - ``waylandLayerShell``
     - boolean
     - Use the ``wlr-layer-shell`` integration.
   * - ``popupKeyboardFocus``
     - boolean
     - Let popups grab the keyboard (enables Esc-to-close).
   * - ``popupReuse``
     - boolean
     - Keep popup shells alive (hidden) across open/close and reuse them.
       Avoids Qt Quick's graphics-pipeline cache growth on every open
       (see :doc:`known issues <index>`). Default ``false`` while validating.
   * - ``background`` / ``foreground`` / ``accent``
     - string
     - Base colors (``#rrggbb``, ``#aarrggbb``, ``rgb()``, ``rgba()``, or a name). Overridden by the stylesheet.
   * - ``fontFamily`` / ``fontSize``
     - string / integer
     - Default font.
   * - ``trayItemPadding``
     - integer
     - Padding around tray icons.
   * - ``animationDuration`` / ``animationEasing``
     - integer / string
     - Popup/transition animation (e.g. ``linear``, ``inoutquad``, ``outcubic``).
   * - ``windowManagerBackend``
     - string
     - ``auto`` | ``i3`` | ``sway`` | ``hyprland`` | ``bspwm`` | ``ewmh`` | ``none``. (Also accepts ``{"windowManager": {"backend": ...}}``.)
   * - ``styleSheet``
     - string
     - Path to a CSS theme (see :doc:`theming`).
   * - ``output``
     - string
     - Target monitor name (e.g. ``DP-1``).
   * - ``modules-left`` / ``modules-center`` / ``modules-right``
     - array
     - Applet names placed in each region (see below). ``applets`` is an alias.
   * - ``customTools``
     - object
     - Map of ``"custom/<name>"`` → tool definition (see below and :doc:`custom-tools`).
   * - ``taskbar``
     - object
     - Taskbar options: ``scope`` (``workspace`` | ``all`` | ``monitor``), ``middleClickClose``, ``rightClickMenu``.
   * - ``notifications``
     - object
     - The opt-in ``org.freedesktop.Notifications`` daemon (``"enabled": true`` required) — see :doc:`notifications`.
   * - ``cpu`` / ``memory`` / ``network``
     - object
     - Per-applet display options (see *Composable displays*).

Modules
-------

Each region (``modules-left`` / ``-center`` / ``-right``) is an ordered list of applet
names. Built-in modules:

``Workspaces``, ``Title``, ``Taskbar``, ``CPU``, ``Memory``, ``Network``,
``NetworkManager``, ``Disk``, ``Temperature``, ``Sound``, ``Battery``, ``Brightness``,
``Bluetooth``, ``PowerProfiles``, ``UPower`` (peripheral batteries), ``User`` (avatar +
name + uptime), ``Privacy`` (mic/camera in-use), ``Caffeine``, ``XInput``, ``Media`` (MPRIS),
``Clock``, ``Tray``.

A custom tool is referenced as ``"CustomTool:custom/<name>"`` and defined under
``customTools`` — see :doc:`custom-tools`.

Groups and drawers
------------------

A *group* bundles several modules into one styled container (waybar-compatible). Style it
by its name (``#<name>`` in CSS — background, ``border-radius``, ``padding``). There are two
equivalent ways to declare one.

**Inline** — an object inside a module list. The first element may be an options object;
the rest are child module names:

.. code-block:: json

   "modules-left": [
     { "navgroup": ["Workspaces", "Clock", "CPU"] }
   ]

**waybar form** — a ``"group/<name>"`` reference in the list plus a top-level definition:

.. code-block:: json

   "modules-right": [ "group/tools" ],
   "group/tools": {
     "orientation": "horizontal",
     "modules": ["Battery", "CPU", "Memory"],
     "drawer": { "transition-duration": 300 }
   }

Adding a ``drawer`` turns the group into a **hover-expand carousel**: only the first child
shows at rest; the rest slide out on hover and collapse on leave.

The animation is **CSS-driven** — set a ``transition`` on the group:

.. code-block:: css

   #tools { transition: 300ms ease-in-out; }
   #tools.drawer-child:hover { background-color: rgba(255,255,255,0.1); }

waybar's ``drawer.transition-duration`` (ms) is honoured only as a legacy fallback, used
when the theme sets no CSS transition. Each child applet gets the ``children-class`` (default
``drawer-child``) as a CSS class, so ``.drawer-child`` / ``#<module>.drawer-child`` rules
style the grouped modules.

Composable displays (CPU / Memory / Network)
--------------------------------------------

The ``cpu``, ``memory``, and ``network`` applets always draw a graph; the ``format`` array
chooses which value parts appear beside it. ``cycle`` is a slot the mouse wheel cycles
through.

.. code-block:: json

   "cpu": { "format": ["percentage", "cycle"], "text": "CPU" }

* ``format`` items: ``text`` | ``percentage`` | ``clock`` | ``absolute`` | ``used`` | ``cycle``. An empty list means *graph only*.
* ``text`` — the literal label shown by the ``text`` / ``cycle`` parts.

Custom tools (overview)
-----------------------

``customTools`` maps an id to a tool. A tool is **either** an external script (``exec``)
**or** a runtime QML widget (``source``).

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Key
     - Description
   * - ``source``
     - Path to a runtime QML widget (``.qml``), resolved relative to the config dir. Used **instead of** ``exec``. See :doc:`custom-tools`.
   * - ``exec``
     - Command to run for a script-driven (waybar-format) tool.
   * - ``command`` / ``arguments`` / ``workingDirectory``
     - Alternative to ``exec`` (argv form) and the working directory.
   * - ``interval``
     - Seconds between runs.
   * - ``return-type``
     - ``json`` for the waybar object format; omit for plain text.
   * - ``format``
     - Output template, e.g. ``"{} ({percentage:.1f}%)"``.
   * - ``format-icons``
     - Object mapping states/ranges to glyphs.
   * - ``tooltip`` / ``show-empty`` / ``exec-if``
     - Tooltip toggle, hide-when-empty, and a guard command.
   * - ``on-click`` / ``on-click-middle`` / ``on-click-right`` / ``on-scroll-up`` / ``on-scroll-down``
     - Commands run on the corresponding mouse action.

.. code-block:: json

   "customTools": {
     "custom/btc": { "source": "widgets/Bitcoin.qml" },
     "custom/weather-script": {
       "exec": "~/.config/qbar/scripts/weather.sh",
       "return-type": "json", "interval": 600,
       "format": "{}", "tooltip": true
     }
   }
