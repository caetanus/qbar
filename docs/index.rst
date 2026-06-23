qbar documentation
===================

**qbar** is a fast, CSS-themable status bar for Wayland (``wlr-layer-shell``) and X11,
built with Qt 6 / QML. It offers waybar-style modules, interactive popups,
hot-reloadable custom QML widgets, and a JSON IPC for scripting.

This reference covers the three things you will configure and extend most:

.. toctree::
   :maxdepth: 2
   :caption: Reference

   configuration
   theming
   custom-tools

Quick start
-----------

#. Build and install (Meson + Ninja, Qt 6.5+)::

      meson setup build
      ninja -C build
      sudo ninja -C build install

#. Drop a config at ``~/.config/qbar/config.json`` (see :doc:`configuration`).
#. Point ``styleSheet`` at one of the bundled themes in ``config/themes`` (see :doc:`theming`).
#. Run ``qbar``.

See also
--------

* Window-manager backends — ``docs/window-manager-backends.md``
* Tray icon resolution — ``docs/tray-icon-resolution.md``
