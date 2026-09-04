import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property string name: ""
    property color color: KodosiTheme.textSecondary
    property real strokeWidth: 1.8

    readonly property string path1: {
        switch (name) {
        case "plus": return "M12 5 V19 M5 12 H19"
        case "folder": return "M3 7.5 Q3 6 4.5 6 H9 L11 8 H19.5 Q21 8 21 9.5 V18 Q21 20 19 20 H5 Q3 20 3 18 Z"
        case "folder-plus": return "M3 8 Q3 6.5 4.5 6.5 H9 L11 8.5 H20 V19 H3 Z M16 3 V9 M13 6 H19"
        case "archive": return "M4 7 H20 V20 H4 Z M3 4 H21 V8 H3 Z M9 12 H15"
        case "clock": return "M12 3 A9 9 0 1 1 5.64 5.64 M12 7 V12 L16 14"
        case "command": return "M9 7 A3 3 0 1 0 6 10 H18 A3 3 0 1 0 15 7 V17 A3 3 0 1 0 18 14 H6 A3 3 0 1 0 9 17 Z"
        case "people": return "M8.5 11 A3 3 0 1 0 8.5 5 A3 3 0 1 0 8.5 11 M15.5 10 A2.5 2.5 0 1 0 15.5 5 A2.5 2.5 0 1 0 15.5 10 M3.5 19 Q3.5 13.5 8.5 13.5 Q13.5 13.5 13.5 19 M13 13 Q20.5 12.5 20.5 18"
        case "devices": return "M5 4 H16 Q18 4 18 6 V15 Q18 17 16 17 H5 Q3 17 3 15 V6 Q3 4 5 4 M8 20 H13 M10.5 17 V20 M19 8 H21 V18 H16"
        case "settings": return "M12 8.5 A3.5 3.5 0 1 0 12 15.5 A3.5 3.5 0 1 0 12 8.5 M12 3 V5 M12 19 V21 M3 12 H5 M19 12 H21 M5.64 5.64 L7.05 7.05 M16.95 16.95 L18.36 18.36 M18.36 5.64 L16.95 7.05 M7.05 16.95 L5.64 18.36"
        case "bell": return "M6 16 H18 L16.5 14 V10 A4.5 4.5 0 0 0 7.5 10 V14 Z M10 19 Q12 21 14 19"
        case "account": return "M12 12 A4 4 0 1 0 12 4 A4 4 0 1 0 12 12 M4.5 21 Q5 15 12 15 Q19 15 19.5 21"
        case "chevron-right": return "M9 5 L16 12 L9 19"
        case "chevron-left": return "M15 5 L8 12 L15 19"
        case "chevron-down": return "M5 9 L12 16 L19 9"
        case "chevron-up": return "M5 15 L12 8 L19 15"
        case "close": return "M6 6 L18 18 M18 6 L6 18"
        case "more": return "M6 12 A1 1 0 1 0 6 12.1 M12 12 A1 1 0 1 0 12 12.1 M18 12 A1 1 0 1 0 18 12.1"
        case "terminal": return "M4 5 H20 V19 H4 Z M7 9 L10 12 L7 15 M12 15 H17"
        case "sessions": return "M6 5 H20 V17 H6 Z M3 8 V20 H17"
        case "server": return "M5 4 H19 V9 H5 Z M5 10 H19 V15 H5 Z M5 16 H19 V21 H5 Z M8 6.5 H8.1 M8 12.5 H8.1 M8 18.5 H8.1 M11 6.5 H17 M11 12.5 H17 M11 18.5 H17"
        case "agents": return "M12 4 A4 4 0 1 0 12 12 A4 4 0 1 0 12 4 M5 21 Q5.5 15 12 15 Q18.5 15 19 21 M19 6 V10 M17 8 H21"
        case "shield": return "M12 3 L20 6 V11 Q20 17 12 21 Q4 17 4 11 V6 Z M8.5 12 L11 14.5 L16 9"
        case "cpu": return "M7 7 H17 V17 H7 Z M10 10 H14 V14 H10 Z M9 3 V7 M15 3 V7 M9 17 V21 M15 17 V21 M3 9 H7 M17 9 H21 M3 15 H7 M17 15 H21"
        case "refresh": return "M19 8 A8 8 0 1 0 20 14 M19 4 V8 H15"
        case "login": return "M13 5 H19 V19 H13 M4 12 H15 M11 8 L15 12 L11 16"
        case "logout": return "M11 5 H5 V19 H11 M20 12 H9 M13 8 L9 12 L13 16"
        case "intel": return "M4 6 H20 M4 12 H20 M4 18 H20 M7 4 V8 M14 10 V14 M10 16 V20"
        case "interrupt": return "M7 5 H10 V19 H7 Z M14 5 H17 V19 H14 Z"
        case "focus": return "M4 9 V4 H9 M15 4 H20 V9 M20 15 V20 H15 M9 20 H4 V15"
        case "grid": return "M4 4 H10 V10 H4 Z M14 4 H20 V10 H14 Z M4 14 H10 V20 H4 Z M14 14 H20 V20 H14 Z"
        case "share": return "M8 12 H4 V20 H20 V12 H16 M12 16 V4 M7 9 L12 4 L17 9"
        case "trash": return "M5 7 H19 M9 7 V4 H15 V7 M7 7 L8 20 H16 L17 7 M10 10 V17 M14 10 V17"
        case "eye": return "M3 12 Q7 6 12 6 Q17 6 21 12 Q17 18 12 18 Q7 18 3 12 M12 9 A3 3 0 1 0 12 15 A3 3 0 1 0 12 9"
        case "history": return "M4 5 V10 H9 M4.5 9.5 A8 8 0 1 1 6 17 M12 8 V12 L15 14"
        case "memory": return "M8 4 Q5 4 5 8 Q3 9 5 12 Q3 15 6 17 Q6 20 10 20 Q12 20 12 17 V7 Q12 4 8 4 M16 4 Q19 4 19 8 Q21 9 19 12 Q21 15 18 17 Q18 20 14 20 Q12 20 12 17"
        case "check": return "M5 12.5 L10 17 L19 7"
        case "warning": return "M12 3 L22 20 H2 Z M12 9 V14 M12 17 V17.1"
        case "mission": return "M12 3 L20 7 V17 L12 21 L4 17 V7 Z M8 9 L12 7 L16 9 V15 L12 17 L8 15 Z"
        case "sliders": return "M4 7 H20 M8 4 V10 M4 17 H20 M16 14 V20"
        case "sidebar": return "M4 5 H20 V19 H4 Z M9 5 V19"
        case "diagnostics": return "M8 4 V10 A4 4 0 0 0 16 10 V4 M6 4 H10 M14 4 H18 M12 14 V17 A4 4 0 0 0 20 17 V14 M20 14 A2 2 0 1 0 20 10 A2 2 0 1 0 20 14"
        case "document": return "M6 3 H14 L19 8 V21 H6 Z M14 3 V8 H19 M9 12 H16 M9 16 H16"
        case "send": return "M3 11.5 L21 3 L14 21 L11 13 Z M11 13 L16 8"
        default: return ""
        }
    }

    implicitWidth: 18
    implicitHeight: 18

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: Math.min(root.width, root.height) / 24
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.path1 }
        }
    }
}
