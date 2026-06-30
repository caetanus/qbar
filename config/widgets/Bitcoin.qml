import QtQuick

// Compatibility entry point for old configs using:
//   "CustomTool:custom/btc" + "source": "widgets/Bitcoin.qml"
//
// The implementation moved to Crypto.qml. With no explicit `ticks` in the old config,
// Crypto.qml falls back to BTC + ETH + XMR.
Crypto {
    toolId: "custom/btc"
}
