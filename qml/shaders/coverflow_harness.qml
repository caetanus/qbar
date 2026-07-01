import QtQuick
Rectangle {
    id: root; width: 1000; height: 300; color: "#15161e"
    property var angles: [-1.0,-0.6,-0.3,0.0,0.3,0.6,1.0]
    Image { id: ico; source: "file:///home/caetano/lab/qbar/config/widgets/crypto-icons/btc.svg"
            sourceSize: Qt.size(220,220); width:118; height:118; visible: false }
    ShaderEffectSource { id: icoTex; sourceItem: ico; hideSource: true; live: true }
    // sanity: raw icon top-left
    Image { source: "file:///home/caetano/lab/qbar/config/widgets/crypto-icons/btc.svg"; width:40; height:40; x:8; y:8 }
    Row {
        anchors.centerIn: parent; spacing: 14
        Repeater {
            model: root.angles
            ShaderEffect {
                width: 118; height: 118
                property variant source: icoTex
                property real angle: modelData
                property real depth: 1.4
                property real edge: 0.02
                fragmentShader: "file:///tmp/coverflow.frag.qsb"
            }
        }
    }
    Timer { running: true; interval: 600; onTriggered: root.grabToImage(function(g){ g.saveToFile("/tmp/cfshader_2.png"); Qt.quit() }) }
}
// NOTE: ShaderEffect needs a GPU/RHI scenegraph. The `offscreen` QPA falls back to the
// software backend (no ShaderEffect support) — validate on a GL platform instead:
//   qml6 coverflow_harness.qml            (under a running Wayland/X session)
// The bare `QT_QPA_PLATFORM=offscreen` render is BLANK by design (software backend).
