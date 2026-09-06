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
// 방에 보관할 채팅 줄 수. 나중에 들어온 사람이 흐름을 따라올 정도만.
const CHAT_KEEP = 20;
const MAX_ROOM_NAME = 32;
// 오브젝트 하나가 차지할 수 있는 글자 수와, 방 하나에 담을 수 있는 개수.
//
// 여러 사람에게 열어두는 서버라 한 사람이 실수하거나 장난치는 것만으로
// 서버가 메모리를 다 쓰고 죽을 수 있다. 정상적인 오브젝트는 길어야 몇백
// 글자이고, 아주 큰 레벨도 수십만 개를 넘지 않는다. 넉넉히 잡아두고 그
// 위는 받지 않는다.
const MAX_OBJECT_DATA = 4000;
const MAX_OBJECTS_PER_ROOM = 300000;
// 레벨 설정(배경, 바닥, 색깔) 한 덩어리의 길이.
const MAX_SETTINGS_DATA = 200000;

// 이 오브젝트를 방에 담아도 되는지.
// 이미 있는 것을 고치는 것은 개수가 늘지 않으므로 언제나 허용한다.
function canStore(room, uid, data) {
    if (data.length > MAX_OBJECT_DATA) return false;
    if (room.objects.has(uid)) return true;
    return room.objects.size < MAX_OBJECTS_PER_ROOM;
}
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

// 혼자 시험할 때, 방금 보낸 것을 "Echo"라는 가짜 상대가 보낸 것처럼 돌려준다.
//
// 기기 두 대를 붙이지 않고도 커서가 그려지는지, 채팅이 오가는지,
// 상대가 잡은 물체에 테두리가 생기는지 확인할 수 있다.
// 오브젝트는 여기에 넣지 않는다. 돌려주면 물건이 두 배가 된다.
function echoBack(client, payload) {
    if (!client.solo) return;
    sendTo(client, { ...payload, from: "echo", name: "Echo" });
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
        // 혼자 시험 중이면 가짜 상대 한 명을 더 있는 것으로 센다.
        sendTo(peer, { type: "peers", count: room.clients.size - 1 + (peer.solo ? 1 : 0) });
    }
}

// 방의 내용을 나눠 보낸다.
//
// 한 덩어리로 보내면 오브젝트가 많은 레벨에서 메시지 하나가 수 MB가 된다.
// 받는 쪽은 그걸 한 번에 해석하고 한 번에 만들다가 그대로 멈춘다.
const STATE_CHUNK = 400;

function sendState(room, client) {
    const all = [...room.objects].map(([uid, data]) => ({ uid, data }));

    // 비어 있어도 한 번은 보낸다. 받는 쪽이 "다 왔다"를 알아야 하기 때문.
    if (all.length === 0) {
        sendTo(client, { type: "state", objects: [], left: 0 });
        return;
    }

    for (let at = 0; at < all.length; at += STATE_CHUNK) {
        const part = all.slice(at, at + STATE_CHUNK);
        sendTo(client, {
            type: "state",
            objects: part,
            // 아직 몇 개가 더 오는지. 받는 쪽이 진행 상황을 보여주는 데 쓴다.
            left: all.length - at - part.length,
        });
    }
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
            // 비밀번호 자체는 절대 내보내지 않는다. 잠겼는지만 알린다.
            locked: room.password ? 1 : 0,
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
        // 아무도 없는 방은 곧바로 없앤다.
        //
        // 예전에는 두 시간 동안 남겨뒀다. 잠깐 나갔다 오는 사람을 위한 것이었는데,
        // 실제로는 아무도 없는 방이 목록에 잔뜩 쌓이고, 그 방에 들어가면 예전에
        // 두고 간 레벨을 손님으로 받게 된다. 방을 만든 사람조차 자기 레벨이
        // 아닌 옛 레벨을 받는다.
        rooms.delete(room.name);
        console.log(`[삭제] 방 "${room.name}" - 마지막 사람이 나가서 없앰`);
    } else if (announce) {
        // 남은 사람들이 이 사람의 커서를 지울 수 있도록 알린다.
        relay(room, client, { type: "left", from: client.clientId });
        announcePeers(room);
    }
}

// 한 레벨에서 같이 작업하는 구조라 들어오는 사람의 역할이 갈린다.
// - host: 방이 비어 있다. 이 사람의 레벨이 이 방의 원본이 된다.
// - guest: 방에 이미 레벨이 있다. 이 사람은 그것을 받아서 쓴다.
function enterRoom(client, room) {
    leaveRoom(client);

    client.roomName = room.name;
    room.clients.add(client);
    room.emptySince = 0;

    const host = room.objects.size === 0 && !room.settings;
    console.log(
        `[입장] 방 "${room.name}" - ${client.playerName}`
        + ` (${host ? "방장" : "손님"}, 현재 ${room.clients.size}명)`
    );

    // 어느 방에 어떤 역할로 들어갔는지 먼저 확실히 알려준다.
    // 모드는 이 답을 받고 나서야 자기 레벨을 올릴지 비울지 정한다.
    sendTo(client, { type: "room", name: room.name, mode: host ? "host" : "guest" });

    if (!host) {
        if (room.settings) sendTo(client, room.settings);
        sendState(room, client);
    }

    for (const line of room.chat) sendTo(client, { type: "chat", ...line });

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
const SERVER_VERSION = "v12 (size limits)";

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
    // 혼자 시험하기. 켜면 커서, 선택, 화면, 상태를 본인에게도 되돌려준다.
    // 오브젝트는 절대 되돌려주지 않는다. 그러면 물건이 두 배가 된다.
    client.solo = false;

    // 서버가 몇 번째 판인지 먼저 알려준다. 모드가 창에 띄워주면
    // 배포가 실제로 갱신됐는지 브라우저를 열지 않고도 알 수 있다.
    sendTo(client, { type: "server", version: SERVER_VERSION });

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

        if (msg.type === "solo") {
            client.solo = !!msg.on;
            const room = rooms.get(client.roomName);
            if (room) announcePeers(room);
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
                if (typeof msg.password === "string") {
                    existing.password = msg.password.slice(0, 32);
                }
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
                // 배경, 바닥, 색깔, 노래. 레벨에 하나뿐인 값이라 통째로 보관한다.
                settings: null,
                chat: [],
                // 빈 문자열이면 누구나 들어올 수 있다.
                password: typeof msg.password === "string" ? msg.password.slice(0, 32) : "",
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

            // 잠긴 방은 열쇠가 맞아야 들어간다.
            // 방이 있는지 확인하기 전에 본다. 없는 방과 잠긴 방을 같은 말로
            // 거절해야 열쇠를 모르는 사람이 방 존재 여부를 떠볼 수 없다.
            if (room && room.password && msg.password !== room.password) {
                sendTo(client, { type: "room", name: client.roomName || "" });
                return fail(client, "Wrong password");
            }

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
                // 설정도 같이 보낸다.
                //
                // 오브젝트만 보내던 것이 색깔이 안 맞던 진짜 원인이었다.
                // 손님은 임시 에디터가 열리는 순간 자기 기록을 비우고 다시
                // 달라고 하는데, 그때 오브젝트만 돌아오고 배경과 색깔은
                // 영영 오지 않았다. 물건은 다시 받고 벽지는 못 받은 셈이다.
                if (room.settings) sendTo(client, room.settings);
                sendState(room, client);
                return;

            // 상대가 잡고 있는 물체, 보고 있는 화면. 계속 바뀌므로 흘려보낸다.
            case "sel":
            case "view":
                relay(room, client, { ...msg, from: client.clientId, name: client.playerName });
                echoBack(client, msg);
                return;

            // 채팅. 나중에 들어온 사람에게도 최근 몇 줄은 보여준다.
            case "chat": {
                if (typeof msg.text !== "string" || msg.text === "") return;
                const line = { name: client.playerName, text: msg.text.slice(0, 120) };
                room.chat.push(line);
                if (room.chat.length > CHAT_KEEP) room.chat.shift();
                // 보낸 사람에게도 돌려준다. 자기 말이 방에 닿았는지 보이도록.
                for (const peer of room.clients) sendTo(peer, { type: "chat", ...line });
                return;
            }

            // 상대가 지금 무엇을 얼마나 주고받고 있는지. 보관하지 않고 흘려보낸다.
            // 두 기기를 오가지 않고 한쪽 화면에서 양쪽 상태를 보려는 것이다.
            case "stats":
                if (typeof msg.text !== "string") return;
                relay(room, client, {
                    type: "stats",
                    from: client.clientId,
                    name: client.playerName,
                    text: msg.text.slice(0, 120),
                });
                echoBack(client, { type: "stats", text: msg.text.slice(0, 120) });
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
                // 혼자 시험할 때는 조금 떨어진 자리에 찍어준다. 같은 자리면
                // 내 손가락에 가려 보이지 않는다.
                echoBack(client, { type: "cursor", x: msg.x + 60, y: msg.y + 40 });
                return;

            // 레벨 설정은 하나뿐이라 덮어쓰고 그대로 전달한다.
            case "settings":
                if (typeof msg.data !== "string" || msg.data === "") return;
                if (msg.data.length > MAX_SETTINGS_DATA) return;
                room.settings = {
                    type: "settings",
                    data: msg.data,
                    song: typeof msg.song === "number" ? msg.song : 0,
                    track: typeof msg.track === "number" ? msg.track : 0,
                    // 트리거로 바꿔 트는 곡들. 받는 쪽이 없는 곡을 내려받는 데 쓴다.
                    songs: typeof msg.songs === "string" ? msg.songs : "",
                };
                relay(room, client, room.settings);
                return;

            case "add":
            case "update":
                if (typeof msg.uid !== "string" || typeof msg.data !== "string") return;
                if (!canStore(room, msg.uid, msg.data)) return;
                room.objects.set(msg.uid, msg.data);
                relay(room, client, { type: msg.type, uid: msg.uid, data: msg.data });
                return;

            // 여러 개를 한 번에. 레벨을 통째로 올릴 때 하나씩 보내면
            // 오브젝트 수만큼 메시지가 생긴다.
            case "addMany": {
                if (!Array.isArray(msg.items)) return;
                const good = [];
                for (const item of msg.items) {
                    if (!item || typeof item.uid !== "string" || typeof item.data !== "string") continue;
                    if (!canStore(room, item.uid, item.data)) continue;
                    room.objects.set(item.uid, item.data);
                    good.push({ uid: item.uid, data: item.data });
                }
                if (good.length) relay(room, client, { type: "addMany", items: good });
                return;
            }

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
