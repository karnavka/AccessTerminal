# AccessTerminal
Console application that simulates an access control terminal for a room or building.
A person taps a card, enters a PIN, and the door unlocks. An administrator manages
users, cards and the access log.

### How to run?
through terminal (with CMake):

Linux / macOS:
```bash
cmake -S . -B build
cmake --build build
./build/AccessTerminal
```

Windows (use *Developer PowerShell for VS*):

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\AccessTerminal.exe
```

Run it from the project root.
Use a real terminal (not IDE "run". If usin CLine - use "simulate terminal")

### First run / resetting the database

The database is created from `data/schema.sql` and filled from `data/seed.sql`.
To start from scratch, delete `data/db.sqlite3` and run the program.

to remove the db:
```bash
rm data/db.sqlite3
```

## What is what

The program has two parts that work on the same database.

### Terminal (`TerminalController`)

Simulates the physical device on the door: card reader, keypad, lock, door sensor and siren.


Since there is no real hardware, the door sensor is simulated with commands typed
into the terminal:

| Command     | Meaning                       |
| ----------- | ----------------------------- |
| `<card id>` | tap a card                    |
| `<pin>`     | enter the PIN (when asked)    |
| `!open`     | the door is physically opened |
| `!close`    | the door is physically closed |
| `0`         | cancel / exit                 |

How the door rules work:
- After a correct PIN the door is **unlocked**. Use `!open` to walk in, then `!close`;
  the door locks again and the session ends.
- If nobody opens the door within the entry timeout, it locks again automatically.
  (The timeout is checked when the next input arrives.)
- `!open` while the door is **locked** is a **forced open**: it is logged as an alarm
  and the siren is triggered. This works in any state of the terminal.
- Too many wrong PINs block the card for a while and trigger an alarm.

Access is allowed only if the card exists, is not revoked or blocked, and the current
time is inside one of the card's time windows.

### Admin (`AdminController`)

A console interface for the administrator. It acts as remote access to the system
(like an SSH session), not as a device on the door. Log in with the card ID and PIN of
a user who has the Admin role.

The admin can:
- view users, cards and the access log (with filters by result, date or card ID)
- add users
- issue cards (the card ID is generated automatically, the PIN is 4-8 digits)
- change a card's PIN, block / unblock / revoke it, and edit its time windows

An admin cannot block or revoke the card they are logged in with.

## Security notes

- PINs are stored as Argon2id hashes with a random salt, never in plain text.

## Test data

|       | login    | password |
| ----- | -------- | -------- |
| Admin | ADM-001  | 0000     |
| User  | testcard | 0000     |

## run tests
**Linux / macOS:**

build:
```bash
cmake -S . -B build
cmake --build build --target ControllerTests
```
run
```bash
./build/ControllerTests
```

**Windows** (use *Developer PowerShell for VS*):

build: 
```powershell
cmake -S . -B build
cmake --build build --target ControllerTests
```

run
```powershell
.\build\Debug\ControllerTests.exe
```