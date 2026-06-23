Custom tools (QML widgets)
==========================

A **custom tool** is an entry under ``customTools`` referenced from a module list as
``"CustomTool:custom/<name>"``. It is either an external **script** (``exec`` — the
waybar format) or a **runtime QML widget** (``source``). This page documents the QML
widget API; for the script form and the full key list see :doc:`configuration`.

A QML widget is a ``.qml`` file **loaded from disk at runtime** — it is *not* compiled
into qbar, and it **hot-reloads** when you save it (the widget and its sibling ``.qml`` /
``.js`` files in the same directory are watched).

.. code-block:: json

   "modules-right": ["CustomTool:custom/btc"],
   "customTools": { "custom/btc": { "source": "widgets/Bitcoin.qml" } }

``source`` is resolved relative to the config directory (``~/.config/qbar``), or given as
an absolute / ``file://`` path. The bundled ``config/widgets/Bitcoin.qml`` and
``Weather.qml`` are complete examples.

The widget contract
-------------------

A widget is an ``Item`` (commonly ``QBar.CssRect`` so it is themable). Conventions:

* **Identify for theming** — set ``cssId: "custom-<name>"`` so the theme's ``#custom-<name>``
  rules apply (see :doc:`theming`).
* **Receive your id** — declare ``property string toolId``. qbar assigns it; read your own
  config with ``customTools[toolId]`` to pick up extra keys you defined.
* **Drive your width** — the bar's ``Loader`` resizes the item, which *clears* a plain
  ``width:`` binding. Expose ``property int preferredWidth`` (your content width) and emit
  ``preferredWidthUpdated(width)`` when it changes; qbar reads ``preferredWidth``:

  .. code-block:: qml

     property int preferredWidth: Math.max(1, content.implicitWidth + 14)
     signal preferredWidthUpdated(int width)
     onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)
     Component.onCompleted: preferredWidthUpdated(preferredWidth)

Imports available
-----------------

.. code-block:: qml

   import QtQuick
   import "qrc:/qbar" as QBar              // themed primitives
   import "qrc:/qbar/Fetch.js" as Fetch    // async HTTP (WHATWG-style fetch)
   import "qrc:/qbar/Json.js" as QJson     // async JSON.parse (off the GUI thread)

**QBar primitives** (``qrc:/qbar``): ``CssRect``, ``CssText``, ``CssFill``, ``CssIcon``,
``CssEnable``, ``MarqueeText``, ``Tooltip``, and ``Popup`` (below).

**JS helpers**: ``Fetch.js`` (``Fetch.fetch(url, opts).then(r => …)``), ``Json.js``
(``QJson.parse(text).then(obj => …)``), ``Format.js``, ``Contrast.js``.

Context objects and models
--------------------------

Available globally to every widget (context properties on the engine):

* ``theme`` — base config colors/fonts. **Colors are HexArgb strings**; parse them with
  ``cssTheme.parseColor(str)`` before reading ``.r/.g/.b`` (a raw ``.r`` is ``undefined``).
* ``cssTheme`` — the theme engine: ``cssTheme.loaded``, ``cssTheme.resolve(id)``,
  ``cssTheme.resolvePart(id, part)``, ``cssTheme.parseColor(str)``, ``cssTheme.parseLength(str, fallback)``.
* ``configDir`` — the config directory path.
* **Data models**: ``cpuModel``, ``memoryModel``, ``networkModel``, ``networkManagerModel``,
  ``soundModel``, ``mprisModel``, ``batteryModel``, ``diskModel``, ``bluetoothModel``,
  ``brightnessModel``, ``temperatureModel``, ``powerProfilesModel``, ``calendarModel``,
  ``capsLock``, ``workspaceModel``, ``windowModel``, ``wm``, ``trayModel``.
* ``qbarPopups`` / ``qbarIpc`` — the popup service and the IPC registry.

**Web-style globals** (qbar adds these; Qt's QML engine has no native ``fetch``/timers):
``setTimeout`` / ``setInterval`` / ``clearTimeout`` / ``clearInterval``, and the ``Http``
transport (use the ``Fetch.js`` wrapper rather than ``Http`` directly), plus
``LocalStorage`` for persistent key/value data.

Persistent local storage
------------------------

``LocalStorage`` is an asynchronous SQLite-backed string store, similar to the browser API.
Its database is ``$XDG_DATA_HOME/qbar/localstorage.db`` (normally
``~/.local/share/qbar/localstorage.db``). Prefix keys with the widget name because the
store is shared by all widgets. Calls return a request id; results arrive through signals.
SQLite work runs serially on a dedicated thread, and ``QJson`` parses/stringifies on the
worker pool:

.. code-block:: qml

   import "qrc:/qbar/Json.js" as QJson

   property string storageKey: "bitcoin.drawings"

   function save() {
       QJson.stringify(root.drawings)
           .then(function (text) { LocalStorage.setItem(root.storageKey, text) })
   }

   Component.onCompleted: LocalStorage.getItem(storageKey)
   Connections {
       target: LocalStorage
       function onItemLoaded(requestId, key, value, found) {
           if (key !== root.storageKey || !found) return
           QJson.parse(String(value)).then(function (data) { root.drawings = data })
       }
   }

Available methods are ``getItem(key)``, ``setItem(key, value)``, ``removeItem(key)``,
``contains(key)``, ``keys()`` and ``clear()``; ``LocalStorage.length`` is a cached count.
Result signals are ``itemLoaded``, ``itemStored``, ``itemRemoved``, ``containsLoaded``,
``keysLoaded`` and ``storageCleared``. Values are strings; use ``QJson`` for structured
data. Successful mutations also emit ``changed``, ``removed`` or ``cleared``.

Async I/O
---------

Do network and JSON work **off the GUI thread** so the bar stays smooth:

.. code-block:: qml

   Fetch.fetch("https://api.example.com/data")
       .then(function (r) { return r.json() })   // parsed on a worker thread
       .then(function (data) { root.value = data.value })
       .catch(function (e) { console.warn("fetch failed:", e) })

``r.json()`` is backed by the async ``QJson`` decoder, so even large payloads don't block
the event loop. Use a ``Timer`` (or ``setInterval``) to refresh on a schedule.

Popups
------

Attach a ``QBar.Popup`` to open a panel anchored to the widget:

.. code-block:: qml

   QBar.Popup {
       id: popup
       name: "btc"                              // (optional) exposes it to the IPC
       anchorItem: root
       source: Qt.resolvedUrl("BitcoinPopup.qml")
       payload: ({ price: root.price })         // passed to the popup as properties
       popupWidth: 520; popupHeight: 360
       placement: "below"; horizontalAlignment: "center"
   }

   MouseArea { anchors.fill: parent; onClicked: popup.toggle() }

The popup ``source`` is a sibling ``.qml`` (also hot-reloaded). It receives ``payload``
properties and has the full context above. ``popup.open()`` / ``close()`` / ``toggle()``
control it.

A popup can become an independent utility window with ``popup.detach()``. From inside
the popup content itself, use ``qbarPopups.detachPopup(popupId)``. Detaching immediately
releases the anchored popup id; the new window has its own lifetime and closes normally —
there is intentionally no reattach operation. The window carries floating/tool hints on
X11 and Wayland; a tiling compositor may still require a user rule, typically matching a
title beginning with ``QBar Detached``.

IPC registration
----------------

Setting ``name`` on a ``QBar.Popup`` registers it with qbar's :ref:`JSON IPC <ipc>` — so a
custom tool's popup can be opened from a keyboard shortcut just like a built-in one:

.. code-block:: bash

   qbar-ipc toggle btc
   qbar-ipc toggle weather

This works because the ``qbarIpc`` context object reaches disk-loaded widgets; no extra
wiring is needed beyond the ``name``.

.. _ipc:

The IPC
-------

qbar listens on a ``QLocalSocket`` at ``$XDG_RUNTIME_DIR/qbar.sock`` (override with
``$QBAR_IPC_SOCKET``). Use the bundled ``qbar-ipc`` client (ideal for compositor keybinds):

.. code-block:: bash

   qbar-ipc toggle cpu          # open|close|toggle <popup>
   qbar-ipc list                # registered popup names
   qbar-ipc set-css PATH        # live theme swap (testing)
   qbar-ipc close-all
   qbar-ipc ping

The wire protocol is one JSON object per line, e.g. ``{"command":"toggle","popup":"cpu"}``
→ ``{"ok":true}``. ``qbar-ipc`` exits ``0`` when the reply is ``{"ok":true}``, so it
composes in shell keybindings.

A minimal widget
----------------

.. code-block:: qml

   import QtQuick
   import "qrc:/qbar" as QBar
   import "qrc:/qbar/Fetch.js" as Fetch

   QBar.CssRect {
       id: root
       property string toolId: ""
       cssId: "custom-stars"
       height: theme.height
       property int preferredWidth: Math.max(1, label.implicitWidth + 14)
       width: Math.max(1, preferredWidth)
       signal preferredWidthUpdated(int width)
       onPreferredWidthChanged: preferredWidthUpdated(preferredWidth)

       property int stars: 0
       function refresh() {
           Fetch.fetch("https://api.github.com/repos/qbar/qbar")
               .then(function (r) { return r.json() })
               .then(function (d) { root.stars = d.stargazers_count })
       }
       Component.onCompleted: { preferredWidthUpdated(preferredWidth); refresh() }
       Timer { interval: 600000; running: true; repeat: true; onTriggered: root.refresh() }

       QBar.CssText {
           id: label
           cssId: "custom-stars"
           anchors.centerIn: parent
           text: "★ " + root.stars
       }
   }
