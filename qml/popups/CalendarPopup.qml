import QtQuick

Item {
    id: root

    property date selectedDate: new Date()
    property date displayedDate: new Date()
    property date transitionDate: displayedDate
    property bool monthTransitioning: false
    property real monthTransitionProgress: 1.0
    property int monthTransitionDirection: 1
    property bool showDetails: calendarModel ? calendarModel.available : false
    property int expandedEventIndex: -1

    implicitWidth: 560
    implicitHeight: 380

    Component.onCompleted: {
        displayedDate = root.startOfMonth(root.selectedDate)
        transitionDate = displayedDate
        if (calendarModel) {
            calendarModel.selectedDate = root.selectedDate
        }
    }

    onSelectedDateChanged: {
        if (root.selectedDate) {
            root.displayedDate = root.startOfMonth(root.selectedDate)
            root.transitionDate = root.displayedDate
            if (calendarModel) {
                calendarModel.selectedDate = root.selectedDate
            }
        }
    }

    function startOfMonth(dateValue) {
        return new Date(dateValue.getFullYear(), dateValue.getMonth(), 1)
    }

    function daysInMonth(dateValue) {
        return new Date(dateValue.getFullYear(), dateValue.getMonth() + 1, 0).getDate()
    }

    function firstWeekdayIndex(dateValue) {
        return startOfMonth(dateValue).getDay()
    }

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear()
            && a.getMonth() === b.getMonth()
            && a.getDate() === b.getDate()
    }

    function changeMonth(offset) {
        if (monthTransitioning) {
            return
        }

        var next = new Date(displayedDate.getFullYear(), displayedDate.getMonth() + offset, 1)
        transitionDate = next
        monthTransitionDirection = offset >= 0 ? 1 : -1
        monthTransitioning = true
        monthTransitionProgress = 0
        monthTransitionAnimation.restart()
    }

    SequentialAnimation {
        id: monthTransitionAnimation

        NumberAnimation {
            target: root
            property: "monthTransitionProgress"
            from: 0.0
            to: 1.0
            duration: theme.animationDuration > 0 ? theme.animationDuration : 180
            easing.type: Easing.InOutCubic
        }

        ScriptAction {
            script: {
                root.displayedDate = root.transitionDate
                root.monthTransitioning = false
                root.monthTransitionProgress = 1.0
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 2
        color: theme.background
        border.color: Qt.rgba(1, 1, 1, 0.18)
        border.width: 1
    }

    Row {
        anchors.fill: parent
        anchors.margins: 10
        spacing: root.showDetails ? 12 : 0

        Item {
            width: root.showDetails ? 300 : parent.width
            height: parent.height

            Column {
                anchors.fill: parent
                spacing: 8

                Row {
                    width: parent.width
                    height: 24
                    spacing: 8

                    Text {
                        width: 24
                        height: parent.height
                        text: "\u2039"
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 1
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.changeMonth(-1)
                        }
                    }

                    Text {
                        width: parent.width - 56
                        height: parent.height
                        text: Qt.formatDate(root.monthTransitioning ? root.transitionDate : root.displayedDate, "MMMM yyyy")
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    Text {
                        width: 24
                        height: parent.height
                        text: "\u203A"
                        color: theme.foreground
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 1
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.changeMonth(1)
                        }
                    }
                }

                Row {
                    width: parent.width
                    height: 18
                    spacing: 0

                    Repeater {
                        model: ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]

                        delegate: Text {
                            width: 38
                            height: parent.height
                            text: modelData
                            color: theme.foreground
                            opacity: 0.86
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize - 1
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Item {
                    id: gridViewport
                    width: parent.width
                    height: 180
                    clip: true

                    Repeater {
                        model: root.monthTransitioning ? 2 : 1

                        delegate: Item {
                            width: gridViewport.width
                            height: gridViewport.height

                            property date monthDate: index === 0 ? root.displayedDate : root.transitionDate
                            property real progress: root.monthTransitioning ? root.monthTransitionProgress : 1.0
                            property int direction: root.monthTransitioning ? root.monthTransitionDirection : 0

                            x: root.monthTransitioning
                                ? (index === 0 ? -direction * 24 * progress : direction * 24 * (1.0 - progress))
                                : 0
                            opacity: root.monthTransitioning ? (index === 0 ? 1.0 - progress : progress) : 1.0

                            Grid {
                                width: parent.width
                                columns: 7
                                rows: 6
                                spacing: 0

                                Repeater {
                                    model: 42

                                    delegate: Item {
                                        width: 38
                                        height: 30

                                        property int cellIndex: index
                                        property int offset: cellIndex - root.firstWeekdayIndex(monthDate)
                                        property bool validDay: offset >= 0 && offset < root.daysInMonth(monthDate)
                                        property date cellDate: new Date(monthDate.getFullYear(), monthDate.getMonth(), offset + 1)
                                        property string cellKey: Qt.formatDate(cellDate, "yyyy-MM-dd")
                                        property bool selected: validDay && root.sameDay(cellDate, root.selectedDate)
                                        property bool today: validDay && root.sameDay(cellDate, new Date())
                                        property int eventCount: validDay && calendarModel && calendarModel.eventCountsByDate
                                            ? Number(calendarModel.eventCountsByDate[cellKey] || 0)
                                            : 0

                                        Rectangle {
                                            anchors.fill: parent
                                            color: selected ? theme.accent : "transparent"
                                            opacity: selected ? 0.96 : 1.0
                                            radius: 2
                                            visible: selected
                                        }

                                        Rectangle {
                                            anchors.fill: parent
                                            color: "transparent"
                                            radius: 2
                                            border.color: today && !selected ? theme.accent : "transparent"
                                            border.width: today && !selected ? 1 : 0
                                            visible: today || selected
                                        }

                                        Text {
                                            anchors.centerIn: parent
                                            text: validDay ? cellDate.getDate() : ""
                                            color: selected ? theme.accentForeground : theme.foreground
                                            opacity: validDay ? 1.0 : 0.0
                                            font.family: theme.fontFamily
                                            font.pointSize: theme.fontSize
                                            font.bold: validDay
                                        }

                                        Rectangle {
                                            width: 5
                                            height: 5
                                            radius: 2.5
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.bottom
                                            anchors.bottomMargin: 3
                                            color: selected ? theme.eventDotOnAccent : theme.eventDot
                                            visible: eventCount > 0
                                            opacity: selected ? 0.98 : 0.9
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            enabled: validDay
                                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: root.selectedDate = cellDate
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: calendarModel ? calendarModel.statusText : ""
                    color: theme.foreground
                    opacity: 0.82
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                    elide: Text.ElideRight
                }
            }
        }

        Item {
            width: root.showDetails ? parent.width - 312 : 0
            height: parent.height
            visible: root.showDetails

            Column {
                anchors.fill: parent
                spacing: 8

                Text {
                    width: parent.width
                    text: Qt.formatDate(root.selectedDate, "dddd, d MMM yyyy")
                    color: theme.foreground
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize + 1
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: "Selected day"
                    color: theme.foreground
                    opacity: 0.85
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }

                ListView {
                    id: selectedEventsView
                    width: parent.width
                    height: 192
                    clip: true
                    spacing: 6
                    model: calendarModel ? calendarModel.selectedDayEvents : []

                    delegate: Item {
                        width: selectedEventsView.width
                        height: root.expandedEventIndex === index ? 92 : 44
                        property bool expanded: root.expandedEventIndex === index

                        Rectangle {
                            anchors.fill: parent
                            radius: 2
                            color: Qt.rgba(1, 1, 1, 0.04)
                            border.color: Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                        }

                        Rectangle {
                            width: 3
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: theme.accent
                            opacity: 0.9
                            radius: 1.5
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            y: expanded ? 8 : Math.max(0, (parent.height - implicitHeight) / 2)
                            spacing: 2

                            Text {
                                width: parent.width
                                text: modelData.title
                                color: theme.foreground
                                opacity: 0.98
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: (modelData.cancelled ? "Cancelled | " : "") + modelData.rangeText + (modelData.location !== "" ? " | " + modelData.location : "") + (modelData.sourceLabel !== "" ? " | " + modelData.sourceLabel : "")
                                color: theme.foreground
                                opacity: 0.88
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: modelData.dayText + (modelData.url !== "" ? " | link available" : "")
                                color: modelData.cancelled ? "#fda4af" : theme.foreground
                                opacity: 0.82
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                visible: expanded
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: modelData.sourceLabel
                                color: theme.foreground
                                opacity: 0.72
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                visible: expanded && modelData.sourceLabel !== ""
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: modelData.location
                                color: theme.foreground
                                opacity: 0.72
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                visible: expanded && modelData.location !== ""
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: modelData.url
                                color: theme.eventDot
                                opacity: 0.96
                                font.family: theme.fontFamily
                                font.pointSize: theme.fontSize - 1
                                visible: expanded && modelData.url !== ""
                                elide: Text.ElideRight
                                font.underline: true

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: parent.visible
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(modelData.url)
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.expandedEventIndex = expanded ? -1 : index
                        }
                    }

                    footer: Item {
                        width: selectedEventsView.width
                        height: selectedEventsView.count === 0 ? 44 : 0

                        Text {
                            anchors.centerIn: parent
                            text: calendarModel && calendarModel.loading ? "Loading..." : "No events"
                            color: theme.foreground
                            opacity: 0.8
                            font.family: theme.fontFamily
                            font.pointSize: theme.fontSize
                            visible: parent.height > 0
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: "Next"
                    color: theme.foreground
                    opacity: 0.85
                    font.family: theme.fontFamily
                    font.pointSize: theme.fontSize - 1
                }

                ListView {
                    width: parent.width
                    height: 56
                    clip: true
                    spacing: 4
                    model: calendarModel ? calendarModel.upcomingEvents : []

                    delegate: Text {
                        width: ListView.view.width
                        text: modelData.dayText + " | " + modelData.startText + "  " + modelData.title + (modelData.cancelled ? " | Cancelled" : "")
                        color: theme.foreground
                        opacity: 0.98
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize - 1
                        elide: Text.ElideRight
                    }
                }

                Item {
                    width: parent.width
                    height: root.showDetails && barWindow && barWindow.calendarAppAvailable ? 30 : 0
                    visible: height > 0

                    Rectangle {
                        anchors.fill: parent
                        radius: 2
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.color: Qt.rgba(1, 1, 1, 0.10)
                        border.width: 1
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "abrir calendário"
                        color: theme.foreground
                        opacity: 0.96
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize - 1
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: barWindow.openEvolutionCalendar()
                    }
                }
            }
        }
    }
}
