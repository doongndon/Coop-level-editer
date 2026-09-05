// Coop Level Editor 중계 서버
// 모드가 보낸 오브젝트 배치 정보를 같은 방의 다른 사람에게 그대로 전달만 한다.
const { WebSocketServer } = require("ws");

const PORT = process.env.PORT || 8787;

// 방 이름 -> 그 방에 들어와 있는 접속자 목록
const rooms = new Map();

function leaveRoom(client) {
    const room = rooms.get(client.roomName);
    if (!room) return;

    room.delete(client);
    if (room.size === 0) rooms.delete(client.roomName);
}

const wss = new WebSocketServer({ port: PORT });

wss.on("connection", client => {
    client.roomName = null;

    client.on("message", raw => {
        let msg;
        try {
            msg = JSON.parse(raw.toString());
        } catch {
            return;
        }

        if (msg.type === "join") {
            if (typeof msg.room !== "string" || msg.room === "") return;

            leaveRoom(client);
            client.roomName = msg.room;

            if (!rooms.has(msg.room)) rooms.set(msg.room, new Set());
            rooms.get(msg.room).add(client);

            console.log(`[입장] 방 "${msg.room}" - 현재 ${rooms.get(msg.room).size}명`);
            return;
        }

        if (msg.type === "add") {
            const room = rooms.get(client.roomName);
            if (!room) return;

            // 보낸 사람에게 되돌려주면 오브젝트가 두 번 생기므로 반드시 제외한다
            const data = JSON.stringify(msg);
            for (const peer of room) {
                if (peer !== client && peer.readyState === peer.OPEN) {
                    peer.send(data);
                }
            }
        }
    });

    client.on("close", () => {
        const name = client.roomName;
        leaveRoom(client);
        if (name) console.log(`[퇴장] 방 "${name}"`);
    });
});

console.log(`중계 서버 실행 중 - 포트 ${PORT}`);
