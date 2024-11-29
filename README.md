# Systems Programming Lab 4: Chat System

This repository contains the implementation for **Lab 4** of Systems Programming, where a server and client are built for an internet text-based chat system.

## Overview

The project consists of a **server** that handles multiple clients simultaneously and supports multiple "chat rooms." Clients can log in, join rooms, and communicate with other users in real time.

## Features

- Multiple clients can connect simultaneously using `poll`.
- Users can create and join chat rooms.
- Messages include a timestamp and sender's username.
- Clients automatically attempt to reconnect if disconnected.
- Passwords are masked with `*` during input for secure login.
- Server shutdown is handled cleanly with `Ctrl+C`, informing all connected clients.

## How to Run

### Prerequisites

- A Linux environment (e.g., Ubuntu, macOS).
- A C compiler (e.g., `gcc`).
- A `users.txt` file containing valid usernames and passwords.

### Compilation

Compile the program:

```bash
gcc -o chat_system chat_system.c


Start the server:

./chat_system server

Start the client:

./chat_system client

login <username>	Logs in a user after prompting for a password.
create <room>	Creates a new chat room.
enter <room>	Joins an existing chat room.
who	Lists all users and the rooms they are in.
logout	Logs out and disconnects from the server.
<message>	Sends a message to all users in the same room.
broadcast <message>	Sends a message to all users across all rooms.
