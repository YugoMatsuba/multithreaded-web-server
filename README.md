# Battleship over TCP

COMP30023 Project 2 (2026)

A networked Battleship game written in C. It uses a custom application-layer protocol over TCP and POSIX threads to host multiple two-player games concurrently.

## Client–server communication

```text
Player 1 (C client) <-- IPv4/TCP --> C server <-- IPv4/TCP --> Player 2 (C client)
```

Each client opens its own TCP connection to the server. The server pairs clients that submit the same game ID, applies the game engine's rules, and sends each move's outcome to both players.

```text
Player 1                     Server                       Player 2
    |--- GAME_IDENTIFIER ----->|                              |
    |                          |<------- GAME_IDENTIFIER -----|
    |<-- CONNECTION_STATUS ----|-------- CONNECTION_STATUS -->|
    |         (READY)          |                (READY)       |
    |--- SHIP_PLACEMENT ------>|                              |
    |<-- PLAYER_NUMBER (1) ----|                              |
    |                          |<------- SHIP_PLACEMENT ------|
    |                          |-------- PLAYER_NUMBER (2) -->|
    |--- PLAYER_MOVE --------->|                              |
    |<-- TURN_RESULT ----------|-------- OPPONENT_MOVE ------>|
    |                          |<------- PLAYER_MOVE ---------|
    |<-- OPPONENT_MOVE --------|-------- TURN_RESULT --------->|
```

Player 1 is the player whose connection the server accepted first. The players alternate turns until a winning move ends the game.

## Application-layer protocol

Each message contains two text header lines, a blank line, and a payload.

```text
TYPE:<type>\r\nLENGTH:<bytes>\r\n\r\n<payload bytes>
```

`\r\n` represents a carriage return followed by a line feed. `LENGTH` is the decimal number of payload bytes. Because TCP delivers a byte stream, the receiver reads through the blank line, collects the declared number of payload bytes, and keeps any extra bytes for the next message.

| Message type | Direction | Payload |
| --- | --- | --- |
| `GAME_IDENTIFIER` | Client to server | 32-bit game ID in network byte order |
| `CONNECTION_STATUS` | Server to both players | `READY` |
| `SHIP_PLACEMENT` | Client to server | Four `coordinate,length,direction` records separated by `/`. Direction is `H` or `V`. |
| `PLAYER_NUMBER` | Server to client | `1` or `2` as text |
| `PLAYER_MOVE` | Client to server | Target coordinate as text |
| `PLAYER_MOVE_EXTENDED` | Client to server | Target coordinate as text |
| `TURN_RESULT` | Server to mover | Numeric result as text |
| `TURN_RESULT_EXTENDED` | Server to mover | Binary result from the game engine |
| `OPPONENT_MOVE` | Server to opponent | Coordinate and numeric result as comma-separated text |
| `FAILURE` | Server to client | Empty |

For an extended move, the mover receives `TURN_RESULT_EXTENDED` and the opponent still receives `OPPONENT_MOVE`.

## Server multithreading

```text
Main thread
└── Accepts TCP connections and starts a detached request thread per connection
    ├── Reads the game ID and matches players in the shared game table
    ├── Leaves the first player waiting in the game table
    └── Starts a detached gameplay thread when a second player joins
        ├── Handles ship placement and turns for that match
        └── Calls the shared game engine one call at a time
```

Request threads exit after handling the join. Each active match has its own gameplay thread, so waiting for a move does not block new connections or turns in other matches. `game_array_mutex` protects matchmaking and game status updates. `game_engine_mutex` serializes access to the shared game engine.

## Build and run

From the repository root, build `client.a` and `server.a`:

```sh
make -B
```

Place the course's `project2-bin` dependency in `project2-bin/` so the Makefile can find `librunner.a` and `libengine.a`. Then link the executables:

```sh
make client server
```

Start the server by replacing `PORT` with the port number:

```sh
./server PORT
```
