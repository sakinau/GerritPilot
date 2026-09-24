import QtQuick

Item {
    id: root
    implicitWidth: 38
    implicitHeight: 38
    Accessible.name: "GerritPilot"

    Canvas {
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.save()
            ctx.scale(width / 64, height / 64)
            ctx.clearRect(0, 0, 64, 64)
            const fill = ctx.createLinearGradient(0, 0, 64, 64)
            fill.addColorStop(0, "#42B5FF")
            fill.addColorStop(1, "#0A84FF")
            ctx.fillStyle = fill
            ctx.beginPath()
            ctx.moveTo(17, 1)
            ctx.lineTo(47, 1)
            ctx.quadraticCurveTo(63, 1, 63, 17)
            ctx.lineTo(63, 47)
            ctx.quadraticCurveTo(63, 63, 47, 63)
            ctx.lineTo(17, 63)
            ctx.quadraticCurveTo(1, 63, 1, 47)
            ctx.lineTo(1, 17)
            ctx.quadraticCurveTo(1, 1, 17, 1)
            ctx.fill()

            ctx.strokeStyle = "#FFFFFF"
            ctx.fillStyle = "#FFFFFF"
            ctx.lineWidth = 4.5
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(20, 20)
            ctx.lineTo(20, 46)
            ctx.moveTo(20, 29)
            ctx.lineTo(45, 29)
            ctx.lineTo(45, 43)
            ctx.stroke()
            for (const point of [[20, 18], [20, 46], [45, 45]]) {
                ctx.beginPath()
                ctx.arc(point[0], point[1], 5, 0, Math.PI * 2)
                ctx.fill()
            }
            ctx.restore()
        }
    }
}
