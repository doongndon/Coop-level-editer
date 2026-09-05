// Coop Level Editor 중계 서버
//
// 방 개념
// - 방은 "만들기"로만 생긴다. 그냥 들어가려고 하는 것만으로는 생기지 않는다.
//   그래야 목록에 뜨는 방 = 실제로 누군가 만든 방이 되어 서로 어긋나지 않는다.
// - 만들어진 방은 비어 있어도 목록에 남는다. 잠깐 나갔다 와도 이어서 작업할 수 있다.
// - 대신 오래 비어 있으면 알아서 치운다. 안 그러면 목록이 유령 방으로 가득 찬다.
//
// 하는 일
// - 같은 방에 있는 사람끼리 오브젝트 변경 내용을 전달한다.
// - 방의 현재 오브젝트 상태를 들고 있다가, 나중에 들어온 사람에게 통째로 보내준다.
// - 방 목록을 알려준다.
// - 상대가 어디를 만지고 있는지(커서) 전달한다. 이건 보관하지 않고 흘려보낸다.
const http = require("http");
const { WebSocketServer } = require("ws");

// 호스팅 업체는 포트를 자기가 정해서 알려준다. 알려주지 않으면(내 폰/PC에서 직접 실행)
// 8787을 쓴다.
const PORT = process.env.PORT || 8787;

// 방 이름 -> { clients, objects, owner, createdAt, emptySince }
const rooms = new Map();

const MAX_ROOMS = 200;
const MAX_ROOM_NAME = 32;
// 비어 있는 방을 얼마나 붙들고 있을지. 잠깐 게임을 껐다 켜는 정도는 버텨야 한다.
const EMPTY_ROOM_TTL_MS = 2 * 60 * 60 * 1000;
const SWEEP_INTERVAL_MS = 5 * 60 * 1000;

let nextClientId = 1;

function cleanRoomName(value) {
    if (typeof value !== "string") return "";
    return value.trim().slice(0, MAX_ROOM_NAME);
}

function sendTo(client, payload) {
    if (client.readyState === client.OPEN) {
        client.send(JSON.stringify(payload));
    }
}

function fail(client, reason) {
    sendTo(client, { type: "error", reason });
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

// 만들어진 방은 비어 있어도 전부 알려준다. 그래야 방을 만든 사람이
// 상대에게 "그냥 목록에서 눌러"라고 할 수 있다.
function sendRoomList(client) {
    const list = [];
    for (const [name, room] of rooms) {
        list.push({
            name,
            count: room.clients.size,
            owner: room.owner,
            objects: room.objects.size,
        });
    }
    // 사람이 있는 방을 위로, 그다음은 이름순.
    list.sort((a, b) => b.count - a.count || a.name.localeCompare(b.name));
    sendTo(client, { type: "rooms", list });
}

function leaveRoom(client, announce = true) {
    const room = rooms.get(client.roomName);
    client.roomName = null;
    if (!room) return;

    room.clients.delete(client);

    if (room.clients.size === 0) {
        room.emptySince = Date.now();
        console.log(`[비었음] 방 "${room.name}" - 오브젝트 ${room.objects.size}개 보관 중`);
    } else if (announce) {
        // 남은 사람들이 이 사람의 커서를 지울 수 있도록 알린다.
        relay(room, client, { type: "left", from: client.clientId });
        announcePeers(room);
    }
}

function enterRoom(client, room) {
    leaveRoom(client);

    client.roomName = room.name;
    room.clients.add(client);
    room.emptySince = 0;

    console.log(`[입장] 방 "${room.name}" - ${client.playerName} (현재 ${room.clients.size}명)`);

    // 어느 방에 들어갔는지 먼저 확실히 알려준다. 모드는 이 답을 받고 나서야
    // 자기가 방 안에 있다고 판단한다.
    sendTo(client, { type: "room", name: room.name });
    sendState(room, client);
    announcePeers(room);
    relay(room, client, { type: "joined", name: client.playerName });
}

// 아무도 없고 만든 것도 없이 오래 방치된 방을 치운다.
function sweepRooms() {
    const now = Date.now();
    for (const [name, room] of rooms) {
        if (room.clients.size === 0 && room.emptySince && now - room.emptySince > EMPTY_ROOM_TTL_MS) {
            rooms.delete(name);
            console.log(`[정리] 오래 비어 있던 방 "${name}" 삭제`);
        }
    }
}

setInterval(sweepRooms, SWEEP_INTERVAL_MS).unref?.();

// 브라우저로 주소를 열었을 때 보이는 화면.
// 배포가 실제로 갱신됐는지 눈으로 확인할 수 있도록 버전을 같이 찍는다.
const SERVER_VERSION = "v3 (explicit rooms)";

const httpServer = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    const names = [...rooms.keys()].map(n => ` - ${n} (${rooms.get(n).clients.size}명)`).join("\n");
    res.end(`중계 서버 동작 중 ${SERVER_VERSION}\n방 ${rooms.size}개\n${names}\n`);
});

const wss = new WebSocketServer({ server: httpServer });

wss.on("connection", client => {
    client.roomName = null;
    client.clientId = String(nextClientId++);
    client.playerName = "Player";

    client.on("message", raw => {
        let msg;
        try {
            msg = JSON.parse(raw.toString());
        } catch {
            return;
        }

        // 이름은 방에 들어가기 전에 미리 알려둔다. 방을 만든 사람 표시에 쓴다.
        if (msg.type === "hello") {
            if (typeof msg.name === "string" && msg.name !== "") {
                client.playerName = msg.name.slice(0, 32);
            }
            return;
        }

        // 방 밖에서도 목록은 볼 수 있어야 한다.
        if (msg.type === "rooms") {
            sendRoomList(client);
            return;
        }

        if (msg.type === "create") {
            const name = cleanRoomName(msg.room);
            if (!name) return fail(client, "Room name is empty");

            const existing = rooms.get(name);
            if (existing) {
                // 아무도 없는 방이면 내가 두고 간 방일 수 있으니 그냥 이어서 쓴다.
                if (existing.clients.size > 0) {
                    return fail(client, "That room name is already in use");
                }
                existing.owner = client.playerName;
                enterRoom(client, existing);
                sendRoomList(client);
                return;
            }

            if (rooms.size >= MAX_ROOMS) {
                sweepRooms();
                if (rooms.size >= MAX_ROOMS) return fail(client, "Server is full, try later");
            }

            const room = {
                name,
                clients: new Set(),
                objects: new Map(),
                owner: client.playerName,
                createdAt: Date.now(),
                emptySince: 0,
            };
            rooms.set(name, room);
            console.log(`[생성] 방 "${name}" - ${client.playerName}`);

            enterRoom(client, room);
            sendRoomList(client);
            return;
        }

        if (msg.type === "join") {
            const name = cleanRoomName(msg.room);
            if (!name) return fail(client, "Room name is empty");

            const room = rooms.get(name);
            // 없는 방에 몰래 들어가면서 방이 생기지 않도록 한다.
            // 이게 예전에 목록과 실제 방이 어긋나던 원인이었다.
            if (!room) {
                // 지금 있는 방을 그대로 다시 알려준다. 여기서 무조건 ""를 보내면
                // 이미 방에 있던 사람이 헛손질 한 번에 방 밖으로 보이게 된다.
                sendTo(client, { type: "room", name: client.roomName || "" });
                return fail(client, "No such room - make one first");
            }

            enterRoom(client, room);
            return;
        }

        if (msg.type === "leave") {
            leaveRoom(client);
            sendTo(client, { type: "room", name: "" });
            return;
        }

        const room = rooms.get(client.roomName);
        if (!room) return;

        switch (msg.type) {
            // 에디터에 새로 들어왔으니 현재 상태를 다시 달라는 요청
            case "resync":
                sendState(room, client);
                return;

            // 커서는 계속 바뀌는 값이라 보관하지 않고 그대로 흘려보낸다.
            case "cursor":
                if (typeof msg.x !== "number" || typeof msg.y !== "number") return;
                relay(room, client, {
                    type: "cursor",
                    from: client.clientId,
                    name: client.playerName,
                    x: msg.x,
                    y: msg.y,
                });
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
        if (name) console.log(`[퇴장] 방 "${name}" - ${client.playerName}`);
    });
});

// 0.0.0.0으로 열어야 같은 와이파이의 다른 기기나 호스팅 업체가 접속할 수 있다.
httpServer.listen(PORT, "0.0.0.0", () => {
    console.log(`중계 서버 실행 중 - 포트 ${PORT}`);
});
