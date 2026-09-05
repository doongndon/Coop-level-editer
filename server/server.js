// Coop Level Editor 중계 서버
//
// 하는 일
// - 같은 방에 있는 사람끼리 오브젝트 변경 내용을 전달한다.
// - 방의 현재 오브젝트 상태를 들고 있다가, 나중에 들어온 사람에게 통째로 보내준다.
//   (이게 없으면 접속 전에 만들어둔 것이 상대에게 보이지 않는다.)
const { WebSocketServer } = require("ws");

const PORT = process.env.PORT || 8787;

// 방 이름 -> { clients: Set, objects: Map<uid, 저장문자열> }
const rooms = new Map();

function getRoom(name) {
    if (!rooms.has(name)) {
        rooms.set(name, { clients: new Set(), objects: new Map() });
    }
    return rooms.get(name);
}

function sendTo(client, payload) {
    if (client.readyState === client.OPEN) {
        client.send(JSON.stringify(payload));
    }
}

// 보낸 사람을 뺀 나머지에게 전달한다.
// 보낸 사람에게 되돌려주면 오브젝트가 두 번 생기므로 반드시 제외한다.
function relay(room, sender, payload) {
    const data = JSON.stringify(payload);
    for (const peer of room.clients) {
        if (peer !== sender && peer.readyState === peer.OPEN) {
            peer.send(data);
        }
    }
}

function announcePeers(room) {
    for (const peer of room.clients) {
        sendTo(peer, { type: "peers", count: room.clients.size - 1 });
    }
}

function sendState(room, client) {
    const objects = [];
    for (const [uid, data] of room.objects) {
        objects.push({ uid, data });
    }
    sendTo(client, { type: "state", objects });
}

function leaveRoom(client) {
    const room = rooms.get(client.roomName);
    if (!room) return;

    room.clients.delete(client);

    if (room.clients.size === 0) {
        // 아무도 없어도 만들어둔 내용은 남겨둔다. 다시 들어오면 이어서 작업할 수 있다.
        console.log(`[비었음] 방 "${client.roomName}" - 오브젝트 ${room.objects.size}개 보관 중`);
    } else {
        announcePeers(room);
    }
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

            const room = getRoom(msg.room);
            room.clients.add(client);

            console.log(`[입장] 방 "${msg.room}" - 현재 ${room.clients.size}명`);
            sendState(room, client);
            announcePeers(room);
            return;
        }

        const room = rooms.get(client.roomName);
        if (!room) return;

        switch (msg.type) {
            // 에디터에 새로 들어왔으니 현재 상태를 다시 달라는 요청
            case "resync":
                sendState(room, client);
                return;

            case "add":
            case "update":
                if (typeof msg.uid !== "string" || typeof msg.data !== "string") return;
                room.objects.set(msg.uid, msg.data);
                relay(room, client, { type: msg.type, uid: msg.uid, data: msg.data });
                return;

            case "remove":
                if (typeof msg.uid !== "string") return;
                room.objects.delete(msg.uid);
                relay(room, client, { type: "remove", uid: msg.uid });
                return;
        }
    });

    client.on("close", () => {
        const name = client.roomName;
        leaveRoom(client);
        if (name) console.log(`[퇴장] 방 "${name}"`);
    });
});

console.log(`중계 서버 실행 중 - 포트 ${PORT}`);
