// Coop Level Editor 중계 서버
//
// 하는 일
// - 같은 방에 있는 사람끼리 오브젝트 변경 내용을 전달한다.
// - 방의 현재 오브젝트 상태를 들고 있다가, 나중에 들어온 사람에게 통째로 보내준다.
//   (이게 없으면 접속 전에 만들어둔 것이 상대에게 보이지 않는다.)
const http = require("http");
const { WebSocketServer } = require("ws");

// 호스팅 업체는 포트를 자기가 정해서 알려준다. 알려주지 않으면(내 폰/PC에서 직접 실행)
// 8787을 쓴다.
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

// 호스팅 업체는 서버가 살아있는지 일반 접속으로 주기적으로 확인한다.
// 웹소켓만 받고 아무 대답도 하지 않으면 죽은 줄 알고 내려버리므로,
// 같은 포트에서 일반 접속에도 짧게 대답해준다.
const httpServer = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    res.end(`중계 서버 동작 중 - 방 ${rooms.size}개\n`);
});

const wss = new WebSocketServer({ server: httpServer });

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
                // 혼자 테스트할 때도 모드가 실제로 보내고 있는지 눈으로 확인할 수 있도록 남긴다.
                console.log(
                    `[${msg.type === "add" ? "생성" : "수정"}] ${msg.uid} -> ${msg.data.slice(0, 40)}`
                    + ` (방 오브젝트 ${room.objects.size}개)`
                );
                return;

            case "remove":
                if (typeof msg.uid !== "string") return;
                room.objects.delete(msg.uid);
                relay(room, client, { type: "remove", uid: msg.uid });
                console.log(`[삭제] ${msg.uid} (방 오브젝트 ${room.objects.size}개)`);
                return;
        }
    });

    client.on("close", () => {
        const name = client.roomName;
        leaveRoom(client);
        if (name) console.log(`[퇴장] 방 "${name}"`);
    });
});

// 0.0.0.0으로 열어야 같은 와이파이의 다른 기기나 호스팅 업체가 접속할 수 있다.
httpServer.listen(PORT, "0.0.0.0", () => {
    console.log(`중계 서버 실행 중 - 포트 ${PORT}`);
});
