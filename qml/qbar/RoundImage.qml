import QtQuick
import QtQuick.Effects

// A circular photo that renders on ANY backend. With shaders available it is
// the usual layer-mask MultiEffect; on the software rasterizer (which runs no
// shaders, so a masked MultiEffect paints nothing) it draws the image through
// a circular Canvas clip instead — same round result, pure CPU raster.
Item {
    id: root

    property alias source: img.source
    property alias sourceSize: img.sourceSize
    readonly property bool ready: img.status === Image.Ready

    readonly property bool softwareRenderer: GraphicsInfo.api === GraphicsInfo.Software

    Image {
        id: img
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        cache: true
        visible: false
        onStatusChanged: if (circleFallback.visible) circleFallback.requestPaint()
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: width / 2
        color: "black"
        visible: false
        layer.enabled: !root.softwareRenderer
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        visible: !root.softwareRenderer && root.ready
    }

    Canvas {
        id: circleFallback
        anchors.fill: parent
        visible: root.softwareRenderer && root.ready
        onVisibleChanged: if (visible) ensureLoaded()
        onWidthChanged: if (visible) requestPaint()
        onHeightChanged: if (visible) requestPaint()
        Component.onCompleted: if (visible) ensureLoaded()
        onImageLoaded: requestPaint()

        // drawImage() only accepts sources the canvas itself loaded via
        // loadImage() — handing it the Image ITEM silently paints nothing.
        function ensureLoaded() {
            var src = String(img.source)
            if (src.length === 0)
                return
            if (!isImageLoaded(img.source) && !isImageLoading(img.source))
                loadImage(img.source)
            requestPaint()
        }

        Connections {
            target: img
            function onSourceChanged() { if (circleFallback.visible) circleFallback.ensureLoaded() }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!isImageLoaded(img.source))
                return
            ctx.save()
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, Math.min(width, height) / 2, 0, Math.PI * 2)
            ctx.clip()
            // Centre-crop like Image.PreserveAspectCrop. The hidden Image only
            // supplies the dimensions (with a square sourceSize hint this is a
            // straight fill — fine for the square avatars this renders).
            var iw = Math.max(1, img.implicitWidth)
            var ih = Math.max(1, img.implicitHeight)
            var scale = Math.max(width / iw, height / ih)
            var dw = iw * scale
            var dh = ih * scale
            ctx.drawImage(img.source, (width - dw) / 2, (height - dh) / 2, dw, dh)
            ctx.restore()
        }
    }
}
